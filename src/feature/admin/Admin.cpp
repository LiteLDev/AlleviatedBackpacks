// Admin unit: server rules (SB `disallowedItems` / `containerItemsDisallowed` /
// `maxUpgradesPerStorage`, the AlleviatedBackpacks extras disabled upgrades, nesting depth and keep
// on death) and the bookkeeping behind the admin commands (SB's access log, holder tracking,
// listing, cleanup). The commands live in SbpCommands.cpp (`/sbp`) and AdminCommands.cpp
// (`/backpack` extras).

#include "feature/admin/Admin.h"
#include "feature/admin/AccessLog.h"
#include "feature/admin/SbConfig.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <exception>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "core/Codec.h"
#include "feature/placed/PlacedBackpacks.h"
#include "core/Format.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/stack/StackUpgrade.h"

ABP_REGISTER_CONFIG("admin", abp::admin::AdminConfig);

namespace abp::admin {

namespace {

constexpr std::string_view kSettingsKey    = "admin";
constexpr std::string_view kLegacyKey      = "abp:admin:legacy";
constexpr std::string_view kDeathKeyPrefix = "abp:admin:death:";
constexpr std::int64_t     kSeenRefreshSec = 600; ///< rewrite "last seen" at most every 10 minutes
constexpr int              kRestoreTicks   = 5;

AdminConfig const& cfg() { return config::get<AdminConfig>(); }

LevelStorage* db() {
    auto level = ll::service::getLevel();
    return level ? &level->getLevelStorage() : nullptr;
}

std::int64_t nowSeconds() { return static_cast<std::int64_t>(std::time(nullptr)); }

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string playerUuid(Player const& player) { return player.getUuid().asString(); }

// ---- legacy set: backpacks stored before this unit tracked holders ----------------------------
// Written once per world (the first time the unit sees it) so `removenonplayer` never deletes
// backpacks players had before access tracking existed.

struct LegacyState {
    bool                            loaded = false;
    std::unordered_set<std::string> uuids;
};
LegacyState& legacy() {
    static LegacyState s;
    return s;
}

void loadOrMarkLegacy() {
    auto& st = legacy();
    st       = {};
    auto* d  = db();
    if (!d) return;
    std::string buffer;
    if (d->loadData(kLegacyKey, buffer, DBHelpers::Category::Uncategorized)) {
        auto j = nlohmann::json::parse(buffer, nullptr, false);
        if (j.is_array()) {
            for (auto const& u : j)
                if (u.is_string()) st.uuids.insert(u.get<std::string>());
        }
    } else {
        auto uuids = game::Storage::get().storedUuids();
        st.uuids.insert(uuids.begin(), uuids.end());
        d->saveData(std::string(kLegacyKey), nlohmann::json(uuids).dump(), DBHelpers::Category::Uncategorized);
        if (!uuids.empty()) log().info("admin: {} existing backpacks marked as player-owned (legacy)", uuids.size());
    }
    st.loaded = true;
}

bool isLegacy(std::string const& uuid) {
    auto& st = legacy();
    if (!st.loaded) loadOrMarkLegacy();
    return st.uuids.contains(uuid);
}

// ---- death stash ------------------------------------------------------------------------------

struct StashEntry {
    game::ItemLocation  where;
    core::ItemStackData item;
};
std::unordered_map<std::string, std::vector<StashEntry>>& stashes() {
    static std::unordered_map<std::string, std::vector<StashEntry>> s;
    return s;
}

std::string deathKey(std::string const& uuid) { return std::string(kDeathKeyPrefix) + uuid; }

void persistStash(std::string const& uuid) {
    auto* d = db();
    if (!d) return;
    auto it = stashes().find(uuid);
    if (it == stashes().end() || it->second.empty()) {
        d->deleteData(deathKey(uuid), DBHelpers::Category::Uncategorized);
        return;
    }
    auto arr = nlohmann::json::array();
    for (auto const& e : it->second) {
        arr.push_back({
            {"kind", static_cast<int>(e.where.kind)},
            {"slot", e.where.slot},
            {"item", e.item.toJson()},
        });
    }
    d->saveData(deathKey(uuid), arr.dump(), DBHelpers::Category::Uncategorized);
}

void loadStash(Player const& player) {
    auto* d = db();
    if (!d) return;
    auto        uuid = playerUuid(player);
    std::string buffer;
    if (!d->loadData(deathKey(uuid), buffer, DBHelpers::Category::Uncategorized)) return;
    auto j = nlohmann::json::parse(buffer, nullptr, false);
    if (!j.is_array()) return;
    std::vector<StashEntry> entries;
    for (auto const& e : j) {
        if (!e.is_object()) continue;
        auto item = core::ItemStackData::fromJson(e.value("item", nlohmann::json()));
        if (item.empty()) continue;
        int kind = std::clamp(e.value("kind", 0), 0, 2);
        entries.push_back({{static_cast<game::ItemLocation::Kind>(kind), e.value("slot", 0)}, std::move(item)});
    }
    if (!entries.empty()) stashes()[uuid] = std::move(entries);
}

bool keepInventoryOn() {
    auto level = ll::service::getLevel();
    if (!level) return false;
    GameRuleId id;
    id.mValue = static_cast<int>(GameRules::GameRulesIndex::KeepInventory);
    return level->getGameRules().getBool(id, false);
}

// ---- rules ------------------------------------------------------------------------------------

constexpr int kMaxWalk = 32; ///< recursion guard for nested-backpack walks

std::optional<std::string> parentOf(game::Backpack const& bp) {
    auto const& settings = bp.data().settings;
    if (!settings.is_object()) return std::nullopt;
    auto it = settings.find(kSettingsKey);
    if (it == settings.end() || !it->is_object()) return std::nullopt;
    auto p = it->find("parent");
    if (p == it->end() || !p->is_string()) return std::nullopt;
    return p->get<std::string>();
}

bool holdsBackpack(game::Backpack& parent, std::string const& child) {
    auto const& inv = parent.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& item = inv.at(i);
        if (item.empty() || !game::isBackpack(item)) continue;
        if (game::backpackUuid(item) == child) return true;
    }
    return false;
}

/// How many backpacks `uuid` sits inside, following the recorded parent links (verified against the
/// parents' contents, so stale links from backpacks taken out again don't count).
int ancestorDepth(std::string const& uuid) {
    int         depth = 0;
    std::string cur   = uuid;
    auto&       storage = game::Storage::get();
    for (int guard = 0; guard < kMaxWalk; ++guard) {
        auto bp = storage.find(cur);
        if (!bp) break;
        auto parent = parentOf(*bp);
        if (!parent || *parent == cur) break;
        auto pb = storage.find(*parent);
        if (!pb || !holdsBackpack(*pb, cur)) break;
        ++depth;
        cur = *parent;
    }
    return depth;
}

/// Records `parent` as the container of every backpack item in `slot` (-1 = all slots).
void linkChildren(game::Backpack& parent, int slot) {
    auto const& inv   = parent.inventory();
    int         first = slot < 0 ? 0 : slot;
    int         last  = slot < 0 ? inv.size() - 1 : slot;
    for (int i = first; i <= last && i < inv.size(); ++i) {
        auto const& item = inv.at(i);
        if (item.empty() || !game::isBackpack(item)) continue;
        auto uuid = game::backpackUuid(item);
        if (!uuid || *uuid == parent.uuid()) continue;
        auto child = game::Storage::get().find(*uuid);
        if (!child || parentOf(*child) == parent.uuid()) continue;
        auto& settings = child->data().settings;
        if (!settings.is_object()) settings = nlohmann::json::object();
        auto& rec = settings[std::string(kSettingsKey)];
        if (!rec.is_object()) rec = nlohmann::json::object();
        rec["parent"] = parent.uuid();
        child->markDirty();
    }
}

/// Calls `fn` for `bp` and every backpack nested inside it.
void forEachNested(std::shared_ptr<game::Backpack> const& bp, std::function<void(game::Backpack&)> const& fn, int level = 0) {
    if (!bp || level > kMaxWalk) return;
    fn(*bp);
    auto const& inv = bp->inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& item = inv.at(i);
        if (item.empty() || !game::isBackpack(item)) continue;
        auto uuid = game::backpackUuid(item);
        if (!uuid || *uuid == bp->uuid()) continue;
        forEachNested(game::Storage::get().find(*uuid), fn, level + 1);
    }
}

int depthRec(std::string const& uuid, int limit, int level) {
    if (level > 32) return 0;
    auto bp = game::Storage::get().find(uuid);
    if (!bp) return 0;
    int         best = 0;
    auto const& inv  = bp->inventory();
    for (int i = 0; i < inv.size() && best <= limit; ++i) {
        auto const& item = inv.at(i);
        if (item.empty() || !game::isBackpack(item)) continue;
        int d = 1;
        if (auto child = game::backpackUuid(item); child && *child != uuid) d += depthRec(*child, limit, level + 1);
        best = std::max(best, d);
    }
    return best;
}

void buildRules(game::Backpack& bp) {
    std::weak_ptr<game::Backpack> weak = bp.shared_from_this();
    bp.inventory().addInsertFilter("abp:admin", -1000, [weak](core::InsertQuery const& q) {
        if (q.ctx.source == core::Source::Internal) return core::InsertVerdict::Allow;
        if (isDisallowed(q.stack.id)) return core::InsertVerdict::Deny;
        int max = cfg().maxNestingDepth;
        if (max > 0 && game::isBackpack(q.stack)) {
            // Levels below the target (the inserted backpack + its contents) plus the levels the
            // target itself already sits in.
            int depth = 1;
            if (auto uuid = game::backpackUuid(q.stack)) depth += nestedDepth(*uuid, max);
            if (auto self = weak.lock(); self && depth <= max) depth += ancestorDepth(self->uuid());
            if (depth > max) return core::InsertVerdict::Deny;
        }
        return core::InsertVerdict::Allow;
    });
    // Remember which backpack holds which (for the depth check above).
    bp.inventory().addChangeListener("abp:admin", [weak](int slot) {
        if (auto self = weak.lock()) linkChildren(*self, slot);
    });
}

bool gInstalled = false;

/// Backpacks opened through `/backpack openfor` this session (not recorded as held by the admin).
std::unordered_set<std::string>& inspected() {
    static std::unordered_set<std::string> s;
    return s;
}

} // namespace

void markInspected(std::string const& uuid) { inspected().insert(uuid); }
bool isInspected(std::string const& uuid) { return inspected().contains(uuid); }

namespace {
std::vector<ProtectedUuidProvider>& protectedProviders() {
    static std::vector<ProtectedUuidProvider> v;
    return v;
}
} // namespace

void addProtectedUuidProvider(ProtectedUuidProvider provider) {
    if (provider) protectedProviders().push_back(provider);
}

namespace {
struct PendingOpen {
    std::int64_t                    player;
    std::shared_ptr<game::Backpack> backpack;
    std::string                     title;
    int                             ticks;
};
std::vector<PendingOpen>& pendingOpens() {
    static std::vector<PendingOpen> v;
    return v;
}

void runPendingOpens(Level& level) {
    auto& pending = pendingOpens();
    if (pending.empty()) return;
    std::vector<PendingOpen> due;
    for (auto it = pending.begin(); it != pending.end();) {
        if (--it->ticks <= 0) {
            due.push_back(std::move(*it));
            it = pending.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& p : due) {
        ActorUniqueID id;
        id.rawID = p.player;
        if (auto* player = level.getPlayer(id)) ui::BackpackMenu::openBackpack(*player, p.backpack, p.title);
    }
}
} // namespace

void openLater(Player& player, std::shared_ptr<game::Backpack> backpack, std::string title, int ticks) {
    pendingOpens().push_back({player.getOrCreateUniqueID().rawID, std::move(backpack), std::move(title), std::max(1, ticks)});
}

// ---- rules API -----------------------------------------------------------------------------------

bool isDisallowed(std::string_view itemId) {
    // SB: container items (canFitInsideContainerItems false) other than backpacks.
    if (sbServer().containerItemsDisallowed && game::isShulkerBox(itemId)) return true;
    auto id      = lower(itemId);
    auto matches = [&](std::vector<std::string> const& list) {
        for (auto const& raw : list) {
            if (raw.empty()) continue;
            auto pattern = lower(raw);
            if (pattern.find(':') == std::string::npos) pattern = "minecraft:" + pattern;
            if (core::globMatch(pattern, id)) return true;
        }
        return false;
    };
    return matches(sbServer().disallowedItems) || matches(cfg().disallowedItems); // + the deprecated admin.json list
}

bool isUpgradeDisabled(std::string_view id, std::string_view group) {
    auto const& list = cfg().disabledUpgrades;
    if (list.empty()) return false;
    auto wantId = lower(id), wantGroup = lower(group);
    return std::any_of(list.begin(), list.end(), [&](std::string const& raw) {
        // Accept "pickup", "Pickup", "pickup_upgrade" and "alleviatedbackpacks:pickup_upgrade".
        auto s = lower(raw);
        if (auto colon = s.find(':'); colon != std::string::npos) s = s.substr(colon + 1);
        if (s.empty()) return false;
        if (s == wantId || s == wantGroup) return true;
        if (!s.ends_with("_upgrade")) return false;
        s.resize(s.size() - std::string_view("_upgrade").size());
        return s == wantId || s == wantGroup;
    });
}

namespace {
/// SB / SC upgrade group names (`maxUpgradesPerStorage` keys) -> our upgrade groups.
std::string_view sbGroupAlias(std::string_view group) {
    if (group == "stack") return "stack_upgrades";
    if (group == "cooking") return "furnace_upgrades";
    if (group == "jukebox") return "jukebox_upgrades";
    return {};
}

std::string pathOf(std::string_view id) {
    auto colon = id.find(':');
    return lower(colon == std::string_view::npos ? id : id.substr(colon + 1));
}

/// Name for SC's message: the upgrade group's SC name, else the upgrade's item name.
std::string groupDisplayName(std::string_view group, Player* player) {
    // SC upgrade group names -> our keys (SC's furnace group "furnace_upgrades" is translated as
    // upgrade_group.cooking_upgrades).
    if (group == "furnace_upgrades" || group == "cooking_upgrades") group = "cooking";
    else if (group == "stack_upgrades") group = "stack";
    else if (group == "jukebox_upgrades") group = "jukebox";
    auto key      = "abp.admin.upgrade_group." + std::string(group);
    auto fallback = std::string(group);
    if (group == "stack") fallback = "Stack Upgrades";
    else if (group == "cooking") fallback = "Furnace Upgrades";
    else if (group == "jukebox") fallback = "Jukebox Upgrades";
    return player ? trFor(*player, key, fallback) : tr(key, fallback);
}
} // namespace

std::string backpackDisplayName(core::Tier tier, Player const* player) {
    static constexpr std::string_view kNames[] =
        {"Backpack", "Copper Backpack", "Iron Backpack", "Gold Backpack", "Diamond Backpack", "Netherite Backpack"};
    auto i   = static_cast<std::size_t>(tier);
    auto key = "abp.admin.backpack_name." + std::string(core::tierInfo(tier).id);
    auto fb  = i < std::size(kNames) ? kNames[i] : std::string_view("Backpack");
    return player ? trFor(*player, key, fb) : tr(key, fb);
}

std::optional<UpgradeLimitHit>
maxUpgradesHit(game::Backpack const& backpack, std::string_view itemId, std::string_view group, int excludeSlot) {
    auto const& entries = sbServer().maxUpgradesPerStorage;
    if (entries.empty()) return std::nullopt;
    auto itemPath  = pathOf(itemId);
    auto groupName = lower(group);
    auto alias     = sbGroupAlias(groupName);
    // SC getUpgradesPerStorage / getUpgradesInGroupPerStorage: the first matching entry wins,
    // Integer.MAX_VALUE without one.
    std::optional<int> itemMax, groupMax;
    for (auto const& e : entries) {
        auto bar = e.find('|');
        if (bar == std::string::npos) continue;
        auto name = pathOf(std::string_view(e).substr(0, bar));
        int  max  = 0;
        try {
            max = std::stoi(e.substr(bar + 1));
        } catch (...) {
            continue;
        }
        if (!itemMax && name == itemPath) itemMax = max;
        else if (!groupMax && (name == groupName || (!alias.empty() && name == alias))) groupMax = max;
    }
    if (!itemMax && !groupMax) return std::nullopt;
    // SC checkUpgradePerStorageTypeLimit: 0 = not allowed at all; then the item's own limit
    // (slots holding this item), then the group's (slots holding any upgrade of the group).
    if (itemMax && *itemMax == 0) return UpgradeLimitHit{0, true, {}};
    if (groupMax && *groupMax == 0) return UpgradeLimitHit{0, false, {}};
    std::vector<int> sameItem, sameGroup;
    auto const&      slots = backpack.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const& s = slots[static_cast<std::size_t>(i)];
        if (s.empty() || i == excludeSlot) continue;
        if (pathOf(s.item.id) == itemPath) sameItem.push_back(i);
        if (auto* u = upgrade::upgradeFor(s.item); u && lower(u->info().groupOrId()) == groupName) sameGroup.push_back(i);
    }
    if (itemMax && static_cast<int>(sameItem.size()) >= *itemMax) return UpgradeLimitHit{*itemMax, true, std::move(sameItem)};
    if (groupMax && static_cast<int>(sameGroup.size()) >= *groupMax) return UpgradeLimitHit{*groupMax, false, std::move(sameGroup)};
    return std::nullopt;
}

std::string maxUpgradesMessage(
    UpgradeLimitHit const& hit,
    game::Backpack const&  backpack,
    std::string_view       itemId,
    std::string_view       group,
    Player*                player
) {
    std::string what = hit.byItem ? std::string(itemId) : groupDisplayName(lower(group), player);
    if (hit.byItem) {
        if (auto* u = upgrade::upgradeFor(core::ItemStackData::of(std::string(itemId), 1))) what = upgrade::displayName(*u, player);
    }
    auto storage = backpackDisplayName(backpack.tier(), player);
    if (hit.max == 0) {
        return player ? trFor(*player, "abp.mainscreen.error.add.upgrade_not_allowed", "{0} can't be used in {1}", what, storage)
                      : tr("abp.mainscreen.error.add.upgrade_not_allowed", "{0} can't be used in {1}", what, storage);
    }
    return player ? trFor(*player, "abp.admin.only_x_upgrades_allowed", "Only {}x {} can be used in {}", hit.max, what, storage)
                  : tr("abp.admin.only_x_upgrades_allowed", "Only {}x {} can be used in {}", hit.max, what, storage);
}

bool withinMaxUpgrades(game::Backpack const& backpack, std::string_view itemId, std::string_view group, Player* player, std::string* reason) {
    auto hit = maxUpgradesHit(backpack, itemId, group);
    if (!hit) return true;
    if (reason) *reason = maxUpgradesMessage(*hit, backpack, itemId, group, player);
    return false;
}

int nestedDepth(std::string const& uuid, int limit) { return depthRec(uuid, limit, 0); }

void applyRules() {
    auto& storage = game::Storage::get();
    if (!storage.available()) return;
    storage.forEachLoaded([](std::shared_ptr<game::Backpack> const& bp) { bp->rebuildHooks(); });
}

// ---- holders -------------------------------------------------------------------------------------

void recordHolder(game::Backpack& backpack, Player& player) {
    auto& settings = backpack.data().settings;
    if (!settings.is_object()) settings = nlohmann::json::object();
    auto key = std::string(kSettingsKey);
    if (!settings.contains(key) || !settings[key].is_object()) settings[key] = nlohmann::json::object();
    auto&          rec  = settings[key];
    nlohmann::json self = {{"name", player.getRealName()}, {"uuid", playerUuid(player)}};
    auto           now  = nowSeconds();
    bool           changed = false;
    if (!rec.contains("first") || !rec["first"].is_object()) {
        rec["first"] = self;
        changed      = true;
    }
    if (!rec.contains("last") || rec["last"] != self) {
        rec["last"] = self;
        changed     = true;
    }
    auto seen = rec.contains("seen") && rec["seen"].is_number_integer() ? rec["seen"].get<std::int64_t>() : 0;
    if (changed || now - seen >= kSeenRefreshSec) {
        rec["seen"] = now;
        backpack.markDirty();
    }
}

HolderRecord holderOf(game::Backpack const& backpack) {
    HolderRecord out;
    auto const&  settings = backpack.data().settings;
    if (!settings.is_object()) return out;
    auto it = settings.find(kSettingsKey);
    if (it == settings.end() || !it->is_object()) return out;
    auto holder = [&](char const* k) -> std::optional<Holder> {
        auto h = it->find(k);
        if (h == it->end() || !h->is_object()) return std::nullopt;
        return Holder{h->value("name", std::string()), h->value("uuid", std::string())};
    };
    out.first    = holder("first");
    out.last     = holder("last");
    auto seen    = it->find("seen");
    out.lastSeen = seen != it->end() && seen->is_number_integer() ? seen->get<std::int64_t>() : 0;
    return out;
}

bool playerAccessed(game::Backpack const& backpack) {
    return AccessLog::get().contains(backpack.uuid()) || holderOf(backpack).first.has_value() || isLegacy(backpack.uuid());
}

// ---- queries / maintenance ---------------------------------------------------------------------------

Summary summarize(std::shared_ptr<game::Backpack> const& bp) {
    Summary s;
    s.uuid      = bp->uuid();
    s.tier      = bp->tier();
    auto& inv   = bp->inventory();
    s.slots     = inv.size();
    for (int i = 0; i < inv.size(); ++i) {
        if (!inv.at(i).empty()) {
            ++s.usedSlots;
            s.items += inv.at(i).count;
        }
    }
    for (auto const& u : bp->data().upgrades)
        if (!u.empty()) ++s.upgrades;
    s.holders = holderOf(*bp);
    s.legacy  = isLegacy(bp->uuid());
    return s;
}

std::optional<std::string> resolveUuid(std::string const& input, std::string* errorKey, std::string* errorFallback) {
    auto setError = [&](char const* key, char const* fallback) {
        if (errorKey) *errorKey = key;
        if (errorFallback) *errorFallback = fallback;
        return std::nullopt;
    };
    auto needle = lower(input);
    if (needle.size() < 4) return setError("abp.admin.err.uuid_short", "Give at least 4 characters of the uuid.");
    auto& storage = game::Storage::get();
    if (core::isUuid(needle) && storage.find(needle)) return needle;
    std::optional<std::string> match;
    for (auto const& uuid : storage.storedUuids()) {
        if (!lower(uuid).starts_with(needle)) continue;
        if (match && *match != uuid) return setError("abp.admin.err.uuid_ambiguous", "More than one backpack matches {}.");
        match = uuid;
    }
    if (!match || !storage.find(*match)) return setError("abp.admin.err.uuid_unknown", "No backpack with uuid {}.");
    return match;
}

std::vector<Summary> listBackpacks(std::string const& holderName) {
    std::vector<Summary> out;
    auto&                storage = game::Storage::get();
    auto                 want    = lower(holderName);
    for (auto const& uuid : storage.storedUuids()) {
        auto bp = storage.find(uuid);
        if (!bp) continue;
        auto s = summarize(bp);
        if (!want.empty()) {
            bool match = (s.holders.first && lower(s.holders.first->name) == want)
                      || (s.holders.last && lower(s.holders.last->name) == want);
            if (!match) continue;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<std::string> carriedNow() {
    std::vector<std::string> out;
    auto                     level = ll::service::getLevel();
    if (!level) return out;
    level->forEachPlayer([&](Player& player) {
        if (player.isClientSide()) return true;
        for (auto& [carried, bp] : game::carriedBackpacks(player)) {
            forEachNested(bp, [&](game::Backpack& b) { out.push_back(b.uuid()); });
        }
        return true;
    });
    return out;
}

namespace {
/// Deletes stored backpacks for which `pred(backpack)` holds, skipping carried, protected and in-use
/// ones. `skipUuid(uuid)` true = keep without loading the backpack.
template <class Pred, class Skip>
std::vector<std::string> removeWhere(Pred&& pred, Skip&& skipUuid);

template <class Pred>
std::vector<std::string> removeWhere(Pred&& pred) {
    return removeWhere(std::forward<Pred>(pred), [](std::string const&) { return false; });
}

template <class Pred, class Skip>
std::vector<std::string> removeWhere(Pred&& pred, Skip&& skipUuid) {
    std::vector<std::string> removed;
    auto&                    storage = game::Storage::get();
    auto                     list    = carriedNow();
    std::unordered_set<std::string> carried(list.begin(), list.end());
    for (auto provider : protectedProviders()) {
        for (auto& u : provider()) carried.insert(std::move(u));
    }
    for (auto const& uuid : storage.storedUuids()) {
        if (carried.contains(uuid) || skipUuid(uuid)) continue;
        auto bp = storage.find(uuid);
        if (!bp) continue;           // unreadable data stays untouched
        if (bp.use_count() > 2) continue; // cache + this reference: anything more is a user (menu, upgrade)
        if (!pred(*bp)) continue;
        bp.reset();
        storage.erase(uuid);
        AccessLog::get().erase(uuid);
        removed.push_back(uuid);
    }
    return removed;
}
} // namespace

namespace {
/// SB BackpackStorage.hasItems / hasOtherBackpackData: no items, no upgrades and nothing else
/// stored (default colours, no settings but our own bookkeeping). Unopened mob backpacks waiting
/// for their loot carry a settings marker (`kPendingLootSetting`), so they count as having data.
bool holdsNothing(game::Backpack& bp) {
    auto const& d = bp.data();
    if (!bp.inventory().isEmpty()) return false;
    for (auto const& u : d.upgrades)
        if (!u.empty()) return false;
    core::BackpackData const defaults;
    if (d.mainColor != defaults.mainColor || d.accentColor != defaults.accentColor) return false;
    if (d.settings.is_object()) {
        for (auto const& [k, _] : d.settings.items())
            if (k != kSettingsKey) return false;
    }
    return true;
}
} // namespace

std::vector<std::string> removeNonPlayer(bool onlyWithEmptyInventory) {
    auto removed = removeWhere([&](game::Backpack& bp) {
        // SB: no access log record (and, with onlyWithEmptyInventory, no items, no upgrades and no
        // other backpack data). Backpacks players had before the access log existed count as
        // accessed (legacy / holder records) so updating never deletes them.
        if (playerAccessed(bp)) return false;
        return !onlyWithEmptyInventory || holdsNothing(bp);
    });
    if (!removed.empty()) log().info("admin: removed {} non player backpacks", removed.size());
    return removed;
}

std::vector<std::string> dropEmptyStorages() {
    // Placed backpacks keep their storage (the block stands for it).
    std::unordered_set<std::string> placedUuids;
    for (auto const& e : placed::PlacedBackpacks::get().entries()) placedUuids.insert(e.uuid);
    auto& accessLog = AccessLog::get();
    // Accessed and placed backpacks are kept without loading them; the rest are loaded once.
    auto removed = removeWhere(
        [&](game::Backpack& bp) { return !holderOf(bp).first && holdsNothing(bp); },
        [&](std::string const& uuid) { return placedUuids.contains(uuid) || accessLog.contains(uuid) || isLegacy(uuid); }
    );
    log().info("admin: dropped {} empty, never accessed backpack storage(s) on load", removed.size());
    return removed;
}

std::vector<std::string> cleanupEmpty() {
    core::BackpackData const defaults;
    auto removed = removeWhere([&](game::Backpack& bp) {
        auto const& d = bp.data();
        if (!bp.inventory().isEmpty()) return false;
        for (auto const& u : d.upgrades)
            if (!u.empty()) return false;
        if (d.mainColor != defaults.mainColor || d.accentColor != defaults.accentColor) return false;
        if (d.settings.is_object()) {
            for (auto const& [k, _] : d.settings.items())
                if (k != kSettingsKey) return false;
        }
        return true;
    });
    if (!removed.empty()) log().info("admin: removed {} empty backpacks", removed.size());
    return removed;
}

Stats stats() {
    Stats s;
    auto& storage = game::Storage::get();
    for (auto const& uuid : storage.storedUuids()) {
        game::Storage::LoadStatus status{};
        auto                      bp = storage.find(uuid, &status);
        if (!bp) {
            if (status == game::Storage::LoadStatus::Unreadable) ++s.unreadable;
            if (status != game::Storage::LoadStatus::Missing) ++s.stored;
            continue;
        }
        ++s.stored;
        auto t = static_cast<std::size_t>(bp->tier());
        if (t < std::size(s.perTier)) ++s.perTier[t];
        auto sum = summarize(bp);
        s.items += sum.items;
        if (sum.usedSlots == 0 && sum.upgrades == 0) ++s.empty;
        if (!playerAccessed(*bp)) ++s.neverAccessed;
    }
    s.loaded = storage.loadedCount();
    if (auto* d = db()) {
        d->forEachKeyWithPrefix(kDeathKeyPrefix, DBHelpers::Category::Uncategorized, [&](std::string_view, std::string_view) {
            ++s.deathStashes;
        });
    }
    return s;
}

// ---- keep on death -----------------------------------------------------------------------------------

int stashOnDeath(Player& player, bool force) {
    if (!force && !cfg().keepBackpacksOnDeath) return 0;
    if (keepInventoryOn()) return 0;
    auto  uuid    = playerUuid(player);
    auto& entries = stashes()[uuid];
    int   n       = 0;
    for (auto const& c : game::scanBackpacks(player)) {
        auto stack = game::itemAt(player, c.where); // copy
        if (stack.isNull()) continue;
        entries.push_back({c.where, game::ItemConv::toData(stack)});
        game::setItemAt(player, c.where, ItemStack::EMPTY_ITEM());
        ++n;
    }
    if (n == 0) {
        if (entries.empty()) stashes().erase(uuid);
        return 0;
    }
    persistStash(uuid);
    log().info("admin: kept {} backpacks of {} on death", n, player.getRealName());
    return n;
}

int restoreStash(Player& player) {
    auto uuid = playerUuid(player);
    auto it   = stashes().find(uuid);
    if (it == stashes().end()) return 0;
    auto entries = std::move(it->second);
    stashes().erase(it);
    int n = 0;
    for (auto const& e : entries) {
        auto stack = game::ItemConv::toStack(e.item);
        if (stack.isNull()) continue;
        if (game::itemAt(player, e.where).isNull()) {
            game::setItemAt(player, e.where, stack);
        } else if (!player.addAndRefresh(stack)) {
            player.drop(stack, false);
        }
        ++n;
    }
    persistStash(uuid); // deletes the key
    return n;
}

bool hasStash(Player const& player) {
    auto it = stashes().find(playerUuid(player));
    return it != stashes().end() && !it->second.empty();
}

// ---- access log ---------------------------------------------------------------------------------------

namespace {

/// SB logs every backpack GUI open (BackpackContainer constructor): item, uuid, player, name,
/// colours, time, columns taken. Admin inspection (`/backpack openfor`) is not a player access.
void recordAccess(Player& player, ui::BackpackMenu& menu) {
    auto const& bp = menu.backpack();
    if (!bp || player.isClientSide() || isInspected(bp->uuid())) return;
    std::string itemId = game::backpackItemId(bp->tier());
    std::string name;
    if (auto const& src = menu.source()) {
        auto const& stack = game::itemAt(player, *src);
        if (game::isBackpack(stack)) {
            itemId = stack.getTypeName();
            name   = stack.getCustomName();
        }
    }
    if (name.empty()) {
        auto tier = game::backpackTierOf(itemId);
        name      = backpackDisplayName(tier.value_or(bp->tier()));
    }
    AccessLog::get().logPlayerAccess(player, itemId, *bp, name);
}

/// The access log replaces phase-1's "who carried it" records for `removeNonPlayer`: a world that
/// had backpacks before the log existed gets a record for every backpack a player held (last
/// holder, last seen), so `/sbp list` shows them and `removeNonPlayer` keeps them.
void migrateHolderRecords() {
    auto& accessLog = AccessLog::get();
    if (accessLog.size() > 0) return;
    auto& storage = game::Storage::get();
    int   n       = 0;
    for (auto const& uuid : storage.storedUuids()) {
        auto bp = storage.find(uuid);
        if (!bp) continue;
        auto h = holderOf(*bp);
        if (!h.last) continue;
        AccessLogRecord r;
        r.backpackItemRegistryName = game::backpackItemId(bp->tier());
        r.backpackUuid             = uuid;
        r.playerName               = h.last->name;
        r.backpackName             = backpackDisplayName(bp->tier());
        r.clothColor               = bp->data().mainColor;
        r.trimColor                = bp->data().accentColor;
        r.accessTime               = h.lastSeen * 1000;
        accessLog.put(std::move(r));
        ++n;
    }
    if (n > 0) {
        accessLog.save();
        log().info("admin: {} holder records added to the access log", n);
    }
}

} // namespace

// ---- module -------------------------------------------------------------------------------------------

namespace {
/// SB BackpackStorage.load: empty, never accessed storages are dropped once per world load.
bool gDropEmptyPending = false;
} // namespace

void adoptLegacyStackLimit() {
    auto& legacy = upgrade::stack::StackConfig::legacyStackLimit();
    if (!legacy) return;
    int   limit   = std::max(0, *legacy);
    auto& entries = config::mut<SbServerConfig>().maxUpgradesPerStorage;
    std::erase_if(entries, [](std::string const& e) { return e.starts_with("stack_upgrades|") || e.starts_with("stack|"); });
    entries.push_back("stack_upgrades|" + std::to_string(limit));
    legacy.reset();
    config::saveAll();
    log().info("admin: stack.json maxPerBackpack {} moved to maxUpgradesPerStorage (stack_upgrades|{})", limit, limit);
}

class AdminModule {
public:
    void install() {
        gInstalled = true;
        upgrade::setTypeGate([](upgrade::IUpgrade const& u) {
            return !isUpgradeDisabled(u.info().id, u.info().groupOrId());
        });
        upgrade::setInstallGate(
            [](std::shared_ptr<game::Backpack> const& bp, upgrade::IUpgrade const& u, Player* player, std::string& reason) {
                return withinMaxUpgrades(*bp, u.info().itemId, u.info().groupOrId(), player, &reason);
            }
        );
        upgrade::addRuleBuilder("admin", &buildRules);
        static bool reloadHooked = false;
        if (!reloadHooked) {
            reloadHooked = true;
            config::onReload([] {
                adoptLegacyStackLimit();
                if (gInstalled) applyRules();
            });
        }
        adoptLegacyStackLimit(); // read at mod load, before this callback existed
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerStartedEvent>([](auto&) { onWorld(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([](auto&) { AccessLog::get().save(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDieEvent>([](ll::event::PlayerDieEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide()) return;
            try {
                stashOnDeath(player);
            } catch (std::exception const& e) {
                log().error("admin: keeping backpacks on death failed: {}", e.what());
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
            loadStash(ev.self());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent& ev) {
            tick(ev.level());
        }));
        ui::MenuManager::Observer o;
        o.beforeShow = [](Player& player, ui::Menu& menu, int) {
            if (auto* bm = dynamic_cast<ui::BackpackMenu*>(&menu)) {
                try {
                    recordAccess(player, *bm);
                } catch (std::exception const& e) {
                    log().error("admin: access log failed: {}", e.what());
                }
            }
        };
        mObserver = ui::MenuManager::get().addObserver(std::move(o));
        if (game::Storage::get().available()) onWorld(); // BDS: the world already exists
        applyRules();
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        ui::MenuManager::get().removeObserver(mObserver);
        AccessLog::get().save();
        upgrade::removeRuleBuilder("admin");
        pendingOpens().clear();
        upgrade::setInstallGate(nullptr);
        upgrade::setTypeGate(nullptr);
        gInstalled = false;
        applyRules();
    }

private:
    static void onWorld() {
        stashes().clear();
        legacy() = {};
        inspected().clear();
        pendingOpens().clear();
        AccessLog::get().clear();
        if (!game::Storage::get().available()) return;
        AccessLog::get().load();
        loadOrMarkLegacy();
        migrateHolderRecords();
        gDropEmptyPending = true; // on the first tick, once every unit has loaded the world
        if (auto level = ll::service::getLevel()) {
            level->forEachPlayer([](Player& p) {
                if (!p.isClientSide()) loadStash(p);
                return true;
            });
        }
    }

    void tick(Level& level) {
        ++mTicks;
        if (gDropEmptyPending && game::Storage::get().available()) {
            gDropEmptyPending = false;
            try {
                (void)dropEmptyStorages();
            } catch (std::exception const& e) {
                log().error("admin: dropping empty storages failed: {}", e.what());
            }
        }
        runPendingOpens(level);
        if (mTicks % 600 == 0) AccessLog::get().save(); // every 30 s, like the storage
        bool scan  = mTicks % std::max(1, cfg().holderScanInterval) == 0;
        bool quick = mTicks % kRestoreTicks == 0; // stash restore + open-menu check
        if (!scan && !quick) return;
        auto& menus = ui::MenuManager::get();
        level.forEachPlayer([&](Player& player) {
            if (player.isClientSide() || !player.isAlive()) return true;
            if (quick && hasStash(player)) restoreStash(player);
            if (quick && menus.sessionCount() > 0) {
                // A backpack opened for the first time gets its storage now; mark it as a player's
                // right away so maintenance can't race the periodic scan. Only unowned ones:
                // admins inspecting with `openfor` don't become holders.
                if (auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(menus.current(player))) {
                    auto const& bp = menu->backpack();
                    if (bp && !inspected().contains(bp->uuid()) && !holderOf(*bp).first) recordHolder(*bp, player);
                }
            }
            if (scan) {
                for (auto& [carried, bp] : game::carriedBackpacks(player)) {
                    forEachNested(bp, [&](game::Backpack& b) { recordHolder(b, player); });
                }
            }
            return true;
        });
    }

    std::vector<ll::event::ListenerPtr> mListeners;
    std::int64_t                        mTicks    = 0;
    int                                 mObserver = 0;
};

} // namespace abp::admin

ABP_REGISTER_MODULE(abp::admin::AdminModule);
