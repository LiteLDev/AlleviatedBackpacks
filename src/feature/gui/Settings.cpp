// GUI options (per backpack / per player / server defaults), memory and no-sort slot data, and the
// inventory hooks that make memory slots work.

#include <algorithm>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

ABP_REGISTER_CONFIG("gui", abp::feature::gui::GuiConfig);

namespace abp::feature::gui {

namespace {
constexpr char const* kSection = "gui"; ///< our key in `BackpackData::settings`

// ---- player settings store (world DB, key abp:player:<uuid>:gui) ---------------------------------
std::unordered_map<std::string, nlohmann::json>& playerCache() {
    static std::unordered_map<std::string, nlohmann::json> cache;
    return cache;
}

std::string playerKey(Player& player) { return "abp:player:" + player.getUuid().asString() + ":gui"; }

LevelStorage* levelStorage() {
    auto level = ll::service::getLevel();
    return level ? &level->getLevelStorage() : nullptr;
}

nlohmann::json& playerJson(Player& player) {
    auto key = playerKey(player);
    auto it  = playerCache().find(key);
    if (it != playerCache().end()) return it->second;
    nlohmann::json j = nlohmann::json::object();
    if (auto* db = levelStorage()) {
        std::string buffer;
        if (db->loadData(key, buffer, DBHelpers::Category::Uncategorized)) {
            auto parsed = nlohmann::json::parse(buffer, nullptr, false);
            if (parsed.is_object()) j = std::move(parsed);
            else log().warn("player settings {} unreadable; using defaults", key);
        }
    }
    return playerCache().emplace(key, std::move(j)).first->second;
}

void savePlayer(Player& player) {
    auto* db = levelStorage();
    if (!db) return;
    db->saveData(playerKey(player), playerJson(player).dump(), DBHelpers::Category::Uncategorized);
}

/// Clears the cache when a world stops (the client loads many worlds per game session).
class PlayerSettingsModule {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerStoppingEvent>(
            [](ll::event::ServerStoppingEvent&) { playerCache().clear(); }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        playerCache().clear();
    }

private:
    ll::event::ListenerPtr mListener;
};

// ---- backpack settings ------------------------------------------------------------------------
nlohmann::json& section(game::Backpack& bp) {
    auto& s = bp.data().settings;
    if (!s.is_object()) s = nlohmann::json::object();
    auto& g = s[kSection];
    if (!g.is_object()) g = nlohmann::json::object();
    return g;
}

nlohmann::json const* sectionOf(game::Backpack const& bp) {
    auto const& s = bp.data().settings;
    if (!s.is_object()) return nullptr;
    auto it = s.find(kSection);
    return (it != s.end() && it->is_object()) ? &*it : nullptr;
}

/// Parsed memory / no-sort data, cached per loaded backpack (invalidated by our setters).
struct SlotCache {
    bool                             valid = false;
    std::vector<core::ItemStackData> memory;
    std::vector<bool>                noSort;
};
constexpr char const* kCacheKey = "gui:slots";

SlotCache& slotCache(game::Backpack& bp) {
    auto& c    = bp.transient<SlotCache>(kCacheKey);
    auto  size = static_cast<std::size_t>(bp.inventory().size());
    if (c.valid && c.memory.size() == size) return c;
    c.memory.assign(size, {});
    c.noSort.assign(size, false);
    if (auto const* g = sectionOf(bp)) {
        if (auto it = g->find("memory"); it != g->end() && it->is_object()) {
            for (auto const& [k, v] : it->items()) {
                int slot = -1;
                try {
                    slot = std::stoi(k);
                } catch (...) {
                    continue;
                }
                if (slot < 0 || slot >= static_cast<int>(size)) continue;
                c.memory[static_cast<std::size_t>(slot)] = core::ItemStackData::fromJson(v);
            }
        }
        if (auto it = g->find("noSort"); it != g->end() && it->is_array()) {
            for (auto const& v : *it) {
                if (!v.is_number_integer()) continue;
                int slot = v.get<int>();
                if (slot >= 0 && slot < static_cast<int>(size)) c.noSort[static_cast<std::size_t>(slot)] = true;
            }
        }
    }
    c.valid = true;
    return c;
}

void invalidate(game::Backpack& bp) { bp.transient<SlotCache>(kCacheKey).valid = false; }

constexpr char const* kIgnoreNbtKey = "memoryIgnoreNbt";

/// A memory entry: SC remembers the Item only (ignore NBT) or the whole stack (match NBT).
core::ItemStackData memoryEntry(core::ItemStackData const& item, bool ignoreNbt) {
    if (item.empty()) return {};
    if (!ignoreNbt) return item.withCount(1);
    return core::ItemStackData::of(item.id, 1, item.maxStack, item.aux);
}

void writeMemory(game::Backpack& bp, std::vector<core::ItemStackData> const& mem) {
    nlohmann::json j = nlohmann::json::object();
    for (std::size_t i = 0; i < mem.size(); ++i) {
        if (!mem[i].empty()) j[std::to_string(i)] = mem[i].withCount(1).toJson();
    }
    auto& g = section(bp);
    if (j.empty()) g.erase("memory");
    else g["memory"] = std::move(j);
    bp.markDirty();
    invalidate(bp);
    bp.rebuildHooks();
}

void writeNoSort(game::Backpack& bp, std::vector<bool> const& ns) {
    nlohmann::json j = nlohmann::json::array();
    for (std::size_t i = 0; i < ns.size(); ++i) {
        if (ns[i]) j.push_back(static_cast<int>(i));
    }
    auto& g = section(bp);
    if (j.empty()) g.erase("noSort");
    else g["noSort"] = std::move(j);
    bp.markDirty();
    invalidate(bp);
}

} // namespace

// ---- options --------------------------------------------------------------------------------------

Options serverDefaults() {
    auto const& cfg = config::get<GuiConfig>();
    Options     o;
    o.sortBy        = core::sortByFromString(cfg.defaultSortBy).value_or(core::SortBy::Name);
    o.openTabFirst  = cfg.defaultOpenTabFirst;
    o.notifications = cfg.defaultNotifications;
    o.keepHotbar    = cfg.defaultKeepHotbar;
    return o;
}

Options apply(Options o, nlohmann::json const& j) {
    if (!j.is_object()) return o;
    if (auto it = j.find(kSortBy); it != j.end() && it->is_string()) {
        if (auto by = core::sortByFromString(it->get<std::string>())) o.sortBy = *by;
    }
    auto flag = [&](char const* key, bool& out) {
        if (auto it = j.find(key); it != j.end() && it->is_boolean()) out = it->get<bool>();
    };
    flag("openTabFirst", o.openTabFirst); // phase-1 name of the option, read as a fallback
    flag(kOpenTabFirst, o.openTabFirst);
    flag(kNotifications, o.notifications);
    flag(kKeepHotbar, o.keepHotbar);
    return o;
}

nlohmann::json playerOverrides(Player& player) { return playerJson(player); }

Options playerOptions(Player& player) { return apply(serverDefaults(), playerJson(player)); }

void setPlayerOption(Player& player, std::string const& key, std::optional<nlohmann::json> value) {
    nlohmann::json patch = nlohmann::json::object();
    patch[key]           = value ? std::move(*value) : nlohmann::json(nullptr);
    setPlayerOptions(player, patch);
}

void setPlayerOptions(Player& player, nlohmann::json const& patch) {
    if (!patch.is_object() || patch.empty()) return;
    auto& j = playerJson(player);
    for (auto const& [key, value] : patch.items()) {
        if (value.is_null()) j.erase(key);
        else j[key] = value;
    }
    savePlayer(player);
}

nlohmann::json backpackOverrides(game::Backpack const& backpack) {
    nlohmann::json out = nlohmann::json::object();
    if (auto const* g = sectionOf(backpack)) {
        for (auto const* key : {kSortBy, "openTabFirst", kOpenTabFirst, kNotifications, kKeepHotbar}) {
            if (auto it = g->find(key); it != g->end()) out[key] = *it;
        }
    }
    return out;
}

void setBackpackOption(game::Backpack& backpack, std::string const& key, std::optional<nlohmann::json> value) {
    auto& g = section(backpack);
    if (value) g[key] = std::move(*value);
    else g.erase(key);
    backpack.markDirty();
}

Options effective(Player* player, game::Backpack const& backpack) {
    Options base = player ? playerOptions(*player) : serverDefaults();
    return apply(base, backpackOverrides(backpack));
}

std::string sortByLabel(Player& player, core::SortBy by) {
    switch (by) {
    case core::SortBy::Count:
        return trFor(player, "abp.gui.sort_by.count", "Count");
    case core::SortBy::Mod:
        return trFor(player, "abp.gui.sort_by.mod", "Mod");
    case core::SortBy::Tag:
        return trFor(player, "abp.gui.sort_by.tag", "Tag");
    case core::SortBy::Name:
    default:
        return trFor(player, "abp.gui.sort_by.name", "Name");
    }
}

// ---- memory / no-sort -------------------------------------------------------------------------------

std::vector<core::ItemStackData> const& memory(game::Backpack& backpack) { return slotCache(backpack).memory; }

void setMemory(game::Backpack& backpack, int slot, core::ItemStackData const& item) {
    auto mem = slotCache(backpack).memory;
    if (slot < 0 || slot >= static_cast<int>(mem.size())) return;
    mem[static_cast<std::size_t>(slot)] = item.empty() ? core::ItemStackData{} : memoryEntry(item, memoryIgnoresNbt(backpack));
    writeMemory(backpack, mem);
}

void memorizeAll(game::Backpack& backpack) {
    // SC MemorySettingsCategory.selectSlots(0, slots): every slot with an item remembers it.
    auto  mem    = slotCache(backpack).memory;
    auto& inv    = backpack.inventory();
    bool  ignore = memoryIgnoresNbt(backpack);
    for (int i = 0; i < inv.size() && i < static_cast<int>(mem.size()); ++i) {
        if (!inv.at(i).empty()) mem[static_cast<std::size_t>(i)] = memoryEntry(inv.at(i), ignore);
    }
    writeMemory(backpack, mem);
}

bool memoryIgnoresNbt(game::Backpack const& backpack) {
    if (auto const* g = sectionOf(backpack)) {
        if (auto it = g->find(kIgnoreNbtKey); it != g->end() && it->is_boolean()) return it->get<bool>();
    }
    // Older data (G2 companion section): "memoryMatchNbt".
    auto const& s = backpack.data().settings;
    if (s.is_object()) {
        if (auto c = s.find("companion"); c != s.end() && c->is_object()) {
            if (auto it = c->find("memoryMatchNbt"); it != c->end() && it->is_boolean()) return !it->get<bool>();
        }
    }
    return true;
}

void setMemoryIgnoreNbt(game::Backpack& backpack, bool ignore) {
    if (memoryIgnoresNbt(backpack) == ignore) return;
    // SC setIgnoreNbt: Ignore -> Match takes the stack in the slot (or the plain item); Match ->
    // Ignore keeps the item only. Then every selected slot is selected again.
    auto  mem = slotCache(backpack).memory;
    auto& inv = backpack.inventory();
    for (int i = 0; i < static_cast<int>(mem.size()); ++i) {
        auto& m = mem[static_cast<std::size_t>(i)];
        if (m.empty()) continue;
        auto const& cur = i < inv.size() ? inv.at(i) : core::ItemStackData{};
        m               = memoryEntry(cur.empty() ? m : cur, ignore); // selectSlot again: the slot's stack wins
    }
    auto& g = section(backpack);
    g[kIgnoreNbtKey] = ignore;
    writeMemory(backpack, mem);
}

bool memoryAccepts(bool ignoreNbt, core::ItemStackData const& remembered, core::ItemStackData const& item) {
    if (remembered.empty() || item.empty()) return false;
    return ignoreNbt ? core::memoryMatches(remembered, item) : remembered.sameItem(item);
}

namespace detail {
nlohmann::json&       guiSection(game::Backpack& backpack) { return section(backpack); }
nlohmann::json const* guiSectionOf(game::Backpack const& backpack) { return sectionOf(backpack); }
void                  slotSettingsChanged(game::Backpack& backpack, bool rebuildHooks) {
    backpack.markDirty();
    invalidate(backpack);
    if (rebuildHooks) backpack.rebuildHooks();
}
} // namespace detail

void clearMemory(game::Backpack& backpack) { writeMemory(backpack, {}); }

int memoryCount(game::Backpack& backpack) {
    int n = 0;
    for (auto const& m : memory(backpack)) n += m.empty() ? 0 : 1;
    return n;
}

std::vector<bool> const& noSort(game::Backpack& backpack) { return slotCache(backpack).noSort; }

void setNoSort(game::Backpack& backpack, int slot, bool on) {
    auto ns = slotCache(backpack).noSort;
    if (slot < 0 || slot >= static_cast<int>(ns.size())) return;
    ns[static_cast<std::size_t>(slot)] = on;
    writeNoSort(backpack, ns);
}

void setNoSortAll(game::Backpack& backpack, bool on) {
    std::vector<bool> ns(static_cast<std::size_t>(backpack.inventory().size()), on);
    writeNoSort(backpack, ns);
}

int noSortCount(game::Backpack& backpack) {
    int n = 0;
    for (bool b : noSort(backpack)) n += b ? 1 : 0;
    return n;
}

} // namespace abp::feature::gui

ABP_REGISTER_MODULE(abp::feature::gui::PlayerSettingsModule);

// Memory slots: only the remembered item may go into a memory slot, and automatic insertion
// (pickup, shift-click, transfer buttons) fills memory slots first.
ABP_REGISTER_BACKPACK_HOOKS([](abp::game::Backpack& bp) {
    using namespace abp;
    auto const& mem = feature::gui::memory(bp);
    if (std::none_of(mem.begin(), mem.end(), [](core::ItemStackData const& m) { return !m.empty(); })) return;
    auto& inv    = bp.inventory();
    bool  ignore = feature::gui::memoryIgnoresNbt(bp);
    inv.addInsertFilter("gui:memory", 100, [mem, ignore](core::InsertQuery const& q) {
        if (q.slot < 0 || q.ctx.source == core::Source::Internal) return core::InsertVerdict::Allow;
        if (q.slot >= static_cast<int>(mem.size())) return core::InsertVerdict::Allow;
        auto const& m = mem[static_cast<std::size_t>(q.slot)];
        return (m.empty() || feature::gui::memoryAccepts(ignore, m, q.stack)) ? core::InsertVerdict::Allow : core::InsertVerdict::Deny;
    });
    inv.addSlotPreference("gui:memory", 0, [mem, ignore](core::ItemStackData const& stack) {
        std::vector<int> out;
        for (std::size_t i = 0; i < mem.size(); ++i) {
            if (feature::gui::memoryAccepts(ignore, mem[i], stack)) out.push_back(static_cast<int>(i));
        }
        return out;
    });
});
