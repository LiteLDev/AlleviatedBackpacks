// SB backpack settings categories (main / item display / no sort / memory) and settings templates
// in SB's settings NBT layout. See SbSettings.h.

#include "feature/gui/SbSettings.h"

#include <algorithm>
#include <set>

#include "mc/world/actor/player/Player.h"

#include "core/Appearance.h"
#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "mod/Config.h"

namespace abp::feature::gui {

namespace {
using nlohmann::json;

constexpr char const* kSearchPhraseKey = "searchPhrase";
constexpr char const* kNoSortColorKey  = "noSortColor";
constexpr char const* kItemDisplayKey  = "itemDisplay";
constexpr int         kDefaultNoSort   = static_cast<int>(core::DyeColor::Lime);
constexpr int         kDefaultDisplay  = static_cast<int>(core::DyeColor::Red);

std::vector<MainSettingInfo> const kMain{
    {MainSetting::ShiftClickOpenTab, kOpenTabFirst, false},
    {MainSetting::KeepTabOpen, "keepTabOpen", true},
    {MainSetting::KeepSearchPhrase, "keepSearchPhrase", true},
    {MainSetting::AnotherPlayerCanOpen, "anotherPlayerCanOpen", true},
};

bool defaultOf(MainSetting s) {
    // The server config can change the default of the one main setting the phase-1 options had.
    if (s == MainSetting::ShiftClickOpenTab) return config::get<GuiConfig>().defaultOpenTabFirst;
    return mainSettingInfo(s).defaultValue;
}

std::optional<bool> boolIn(json const* j, char const* key) {
    if (!j || !j->is_object()) return std::nullopt;
    auto it = j->find(key);
    if (it == j->end() || !it->is_boolean()) return std::nullopt;
    return it->get<bool>();
}

int validDye(json const& v, int fallback) {
    if (!v.is_number_integer()) return fallback;
    int d = v.get<int>();
    return d >= 0 && d < static_cast<int>(core::kDyes.size()) ? d : fallback;
}

json const* companionSection(game::Backpack const& bp) {
    auto const& s = bp.data().settings;
    if (!s.is_object()) return nullptr;
    auto it = s.find("companion");
    return it != s.end() && it->is_object() ? &*it : nullptr;
}

json& displayJson(game::Backpack& bp) {
    auto& g = detail::guiSection(bp);
    auto& d = g[kItemDisplayKey];
    if (!d.is_object()) d = json::object();
    return d;
}

void writeDisplay(game::Backpack& bp, ItemDisplay const& d) {
    auto& j = displayJson(bp);
    j       = json::object();
    if (!d.slots.empty()) {
        j["slots"] = d.slots;
        json rot = json::object(), z = json::object();
        for (std::size_t i = 0; i < d.slots.size(); ++i) {
            if (i < d.rotations.size() && d.rotations[i] != 0) rot[std::to_string(d.slots[i])] = d.rotations[i];
            if (i < d.zOffsets.size() && d.zOffsets[i] != 0) z[std::to_string(d.slots[i])] = d.zOffsets[i];
        }
        if (!rot.empty()) j["rotations"] = std::move(rot);
        if (!z.empty()) j["zOffsets"] = std::move(z);
    }
    if (d.color != kDefaultDisplay) j["color"] = d.color;
    if (j.empty()) detail::guiSection(bp).erase(kItemDisplayKey);
    bp.markDirty();
}

ItemDisplay displayFromJson(json const& j, int slots) {
    ItemDisplay d;
    if (!j.is_object()) return d;
    if (auto it = j.find("slots"); it != j.end() && it->is_array()) {
        for (auto const& v : *it) {
            if (!v.is_number_integer()) continue;
            int s = v.get<int>();
            if (s < 0 || (slots >= 0 && s >= slots)) continue;
            if (std::find(d.slots.begin(), d.slots.end(), s) != d.slots.end()) continue;
            d.slots.push_back(s);
        }
    }
    auto mapOf = [&](char const* key, std::vector<int>& out, int lo, int hi) {
        out.assign(d.slots.size(), 0);
        auto it = j.find(key);
        if (it == j.end() || !it->is_object()) return;
        for (std::size_t i = 0; i < d.slots.size(); ++i) {
            auto v = it->find(std::to_string(d.slots[i]));
            if (v != it->end() && v->is_number_integer()) out[i] = std::clamp(v->get<int>(), lo, hi);
        }
    };
    mapOf("rotations", d.rotations, 0, 359);
    mapOf("zOffsets", d.zOffsets, kMinZOffset, kMaxZOffset);
    d.color = j.contains("color") ? validDye(j["color"], kDefaultDisplay) : kDefaultDisplay;
    return d;
}

} // namespace

// ---- main settings ------------------------------------------------------------------------------------

MainSettingInfo const& mainSettingInfo(MainSetting s) { return kMain[static_cast<std::size_t>(s)]; }
std::vector<MainSettingInfo> const& mainSettings() { return kMain; }

std::optional<MainSetting> mainSettingByKey(std::string_view key) {
    for (auto const& m : kMain) {
        if (key == m.key) return m.setting;
    }
    // G2's protocol name of the first toggle.
    if (key == "shiftClickIntoOpenTab") return MainSetting::ShiftClickOpenTab;
    return std::nullopt;
}

std::optional<bool> playerSetting(Player& player, MainSetting s) {
    auto j = playerOverrides(player);
    return boolIn(&j, mainSettingInfo(s).key);
}

bool playerSettingOrDefault(Player& player, MainSetting s) { return playerSetting(player, s).value_or(defaultOf(s)); }

void setPlayerSetting(Player& player, MainSetting s, bool value) {
    if (value == defaultOf(s)) setPlayerOption(player, mainSettingInfo(s).key, std::nullopt);
    else setPlayerOption(player, mainSettingInfo(s).key, json(value));
}

std::optional<bool> storageSetting(game::Backpack const& backpack, MainSetting s) {
    if (auto v = boolIn(detail::guiSectionOf(backpack), mainSettingInfo(s).key)) return v;
    // G2 kept keepTabOpen / keepSearchPhrase under settings.companion.
    if (s == MainSetting::KeepTabOpen || s == MainSetting::KeepSearchPhrase) return boolIn(companionSection(backpack), mainSettingInfo(s).key);
    return std::nullopt;
}

bool settingValue(Player& player, game::Backpack const& backpack, MainSetting s) {
    if (auto v = storageSetting(backpack, s)) return *v;
    return playerSettingOrDefault(player, s);
}

void setStorageSetting(Player& player, game::Backpack& backpack, MainSetting s, bool value) {
    auto& g = detail::guiSection(backpack);
    if (value != playerSettingOrDefault(player, s)) g[mainSettingInfo(s).key] = value;
    else g.erase(mainSettingInfo(s).key);
    if (auto* c = const_cast<json*>(companionSection(backpack))) c->erase(mainSettingInfo(s).key);
    backpack.markDirty();
}

std::string searchPhrase(Player& player, game::Backpack const& backpack) {
    std::string phrase;
    if (playerSettingOrDefault(player, MainSetting::KeepSearchPhrase)) {
        auto j = playerOverrides(player);
        if (auto it = j.find(kSearchPhraseKey); it != j.end() && it->is_string()) phrase = it->get<std::string>();
    }
    if (phrase.empty() && settingValue(player, backpack, MainSetting::KeepSearchPhrase)) {
        if (auto const* g = detail::guiSectionOf(backpack)) {
            if (auto it = g->find(kSearchPhraseKey); it != g->end() && it->is_string()) phrase = it->get<std::string>();
        }
        if (phrase.empty()) {
            auto j = playerOverrides(player);
            if (auto it = j.find(kSearchPhraseKey); it != j.end() && it->is_string()) phrase = it->get<std::string>();
        }
    }
    return phrase;
}

void setSearchPhrase(Player& player, game::Backpack& backpack, std::string const& phrase) {
    // SC setSearchPhrase: kept at player level when the player keeps search phrases, else on the
    // storage (SC setSetting removes a value equal to the player's).
    auto storePlayer = [&](std::string const& v) {
        setPlayerOption(player, kSearchPhraseKey, v.empty() ? std::nullopt : std::optional<json>(json(v)));
    };
    auto storeStorage = [&](std::string const& v) {
        auto        pj       = playerOverrides(player);
        std::string playerV  = pj.value(kSearchPhraseKey, std::string{});
        auto&       g        = detail::guiSection(backpack);
        if (v != playerV) g[kSearchPhraseKey] = v;
        else g.erase(kSearchPhraseKey);
        backpack.markDirty();
    };
    if (playerSettingOrDefault(player, MainSetting::KeepSearchPhrase)) {
        storePlayer(phrase);
        storeStorage("");
    } else {
        storeStorage(phrase);
    }
}

bool anotherPlayerCanOpen(Player& wearer, game::Backpack const& backpack) {
    if (!config::get<GuiConfig>().allowOpeningOtherPlayerBackpacks) return false;
    return settingValue(wearer, backpack, MainSetting::AnotherPlayerCanOpen);
}

// ---- no sort colour ---------------------------------------------------------------------------------------

int noSortColor(game::Backpack const& backpack) {
    if (auto const* g = detail::guiSectionOf(backpack); g && g->contains(kNoSortColorKey)) {
        return validDye((*g)[kNoSortColorKey], kDefaultNoSort);
    }
    if (auto const* c = companionSection(backpack); c && c->contains(kNoSortColorKey)) return validDye((*c)[kNoSortColorKey], kDefaultNoSort);
    return kDefaultNoSort;
}

void setNoSortColor(game::Backpack& backpack, int dye) {
    if (dye < 0 || dye >= static_cast<int>(core::kDyes.size())) return;
    detail::guiSection(backpack)[kNoSortColorKey] = dye;
    if (auto* c = const_cast<json*>(companionSection(backpack))) c->erase(kNoSortColorKey);
    backpack.markDirty();
}

// ---- item display ----------------------------------------------------------------------------------------

ItemDisplay itemDisplay(game::Backpack const& backpack) {
    auto const* g = detail::guiSectionOf(backpack);
    if (!g || !g->contains(kItemDisplayKey)) return {};
    auto d = displayFromJson((*g)[kItemDisplayKey], backpack.data().inventory.size());
    if (static_cast<int>(d.slots.size()) > kItemDisplayLimit) {
        d.slots.resize(kItemDisplayLimit);
        d.rotations.resize(kItemDisplayLimit);
        d.zOffsets.resize(kItemDisplayLimit);
    }
    return d;
}

bool selectItemDisplaySlot(game::Backpack& backpack, int slot) {
    if (slot < 0 || slot >= backpack.inventory().size()) return false;
    auto d = itemDisplay(backpack);
    if (std::find(d.slots.begin(), d.slots.end(), slot) != d.slots.end()) return false;
    if (static_cast<int>(d.slots.size()) + 1 > kItemDisplayLimit) return false; // SC: limit reached
    d.slots.push_back(slot);
    d.rotations.push_back(0);
    d.zOffsets.push_back(0);
    writeDisplay(backpack, d);
    return true;
}

bool unselectItemDisplaySlot(game::Backpack& backpack, int slot) {
    auto d  = itemDisplay(backpack);
    auto it = std::find(d.slots.begin(), d.slots.end(), slot);
    if (it == d.slots.end()) return false;
    auto i = static_cast<std::size_t>(it - d.slots.begin());
    d.slots.erase(it);
    d.rotations.erase(d.rotations.begin() + static_cast<std::ptrdiff_t>(i));
    d.zOffsets.erase(d.zOffsets.begin() + static_cast<std::ptrdiff_t>(i));
    writeDisplay(backpack, d);
    return true;
}

void rotateItemDisplay(game::Backpack& backpack, int slot, bool clockwise) {
    auto d  = itemDisplay(backpack);
    auto it = std::find(d.slots.begin(), d.slots.end(), slot);
    if (it == d.slots.end()) return;
    auto& r = d.rotations[static_cast<std::size_t>(it - d.slots.begin())];
    r       = (r + (clockwise ? 1 : -1) * 45 + 360) % 360;
    writeDisplay(backpack, d);
}

void changeItemDisplayZOffset(game::Backpack& backpack, int slot, int change) {
    auto d  = itemDisplay(backpack);
    auto it = std::find(d.slots.begin(), d.slots.end(), slot);
    if (it == d.slots.end()) return;
    auto& z = d.zOffsets[static_cast<std::size_t>(it - d.slots.begin())];
    z       = std::clamp(z + change, kMinZOffset, kMaxZOffset);
    writeDisplay(backpack, d);
}

void setItemDisplayColor(game::Backpack& backpack, int dye) {
    if (dye < 0 || dye >= static_cast<int>(core::kDyes.size())) return;
    auto d  = itemDisplay(backpack);
    d.color = dye;
    writeDisplay(backpack, d);
}

std::optional<DisplayedItem> displayedItem(game::Backpack& backpack) {
    if (config::get<GuiConfig>().itemDisplayDisabled) return std::nullopt;
    auto d = itemDisplay(backpack);
    if (d.slots.empty()) return std::nullopt;
    DisplayedItem out;
    out.slot     = d.slots.front();
    out.rotation = d.rotations.front();
    out.zOffset  = d.zOffsets.front();
    out.color    = d.color;
    auto const& inv = backpack.inventory();
    if (out.slot < inv.size() && !inv.at(out.slot).empty()) out.item = inv.at(out.slot).withCount(1);
    else if (auto const& mem = memory(backpack); out.slot < static_cast<int>(mem.size())) out.item = mem[static_cast<std::size_t>(out.slot)];
    return out;
}

// ---- templates ---------------------------------------------------------------------------------------------

nlohmann::json settingsTemplateOf(game::Backpack& backpack) {
    json out = json::object();
    // backpackGlobal: the backpack's own main settings.
    json main = json::object();
    if (auto const* g = detail::guiSectionOf(backpack)) {
        for (auto const& m : kMain) {
            if (auto it = g->find(m.key); it != g->end() && it->is_boolean()) main[m.key] = *it;
        }
        if (auto it = g->find(kSearchPhraseKey); it != g->end() && it->is_string()) main[kSearchPhraseKey] = *it;
    }
    out["backpackGlobal"] = std::move(main);

    auto d = itemDisplay(backpack);
    json disp = json::object();
    if (!d.slots.empty()) {
        disp["slots"] = d.slots;
        json rot = json::object(), z = json::object();
        for (std::size_t i = 0; i < d.slots.size(); ++i) {
            if (d.rotations[i] != 0) rot[std::to_string(d.slots[i])] = d.rotations[i];
            if (d.zOffsets[i] != 0) z[std::to_string(d.slots[i])] = d.zOffsets[i];
        }
        if (!rot.empty()) disp["rotations"] = std::move(rot);
        if (!z.empty()) disp["zOffsets"] = std::move(z);
    }
    disp["color"]        = d.color;
    out["item_display"]  = std::move(disp);

    json ns    = json::object();
    json sel   = json::array();
    auto const& noSortSlots = noSort(backpack);
    for (std::size_t i = 0; i < noSortSlots.size(); ++i) {
        if (noSortSlots[i]) sel.push_back(static_cast<int>(i));
    }
    ns["selectedSlots"] = std::move(sel);
    ns["color"]         = noSortColor(backpack);
    out["no_sort"]      = std::move(ns);

    bool ignore = memoryIgnoresNbt(backpack);
    json items = json::object(), stacks = json::object();
    auto const& mem = memory(backpack);
    for (std::size_t i = 0; i < mem.size(); ++i) {
        if (mem[i].empty()) continue;
        // SC stores the item id; Bedrock items may also need their aux value.
        if (ignore) items[std::to_string(i)] = mem[i].aux == 0 ? json(mem[i].id) : core::ItemStackData::of(mem[i].id, 1, mem[i].maxStack, mem[i].aux).toJson();
        else stacks[std::to_string(i)] = mem[i].withCount(1).toJson();
    }
    out["memory"] = json{{"slotFilterItems", std::move(items)}, {"slotFilterStacks", std::move(stacks)}, {"ignoreNbt", ignore}};
    return out;
}

namespace {
/// Memory entries of a template: slot -> item (count 1), plus its ignoreNbt flag.
std::map<int, core::ItemStackData> templateMemory(json const& tmpl, bool& ignoreNbt) {
    std::map<int, core::ItemStackData> out;
    ignoreNbt  = true;
    auto it    = tmpl.find("memory");
    if (it == tmpl.end() || !it->is_object()) return out;
    ignoreNbt = it->value("ignoreNbt", true);
    auto slotOf = [](std::string const& k) {
        try {
            return std::stoi(k);
        } catch (...) {
            return -1;
        }
    };
    if (auto items = it->find("slotFilterItems"); items != it->end() && items->is_object()) {
        for (auto const& [k, v] : items->items()) {
            int s = slotOf(k);
            if (s < 0) continue;
            if (v.is_string() && !v.get<std::string>().empty()) out[s] = core::ItemStackData::of(v.get<std::string>(), 1);
            else if (v.is_object()) {
                auto d = core::ItemStackData::fromJson(v);
                if (!d.empty()) out[s] = core::ItemStackData::of(d.id, 1, d.maxStack, d.aux);
            }
        }
    }
    if (auto stacks = it->find("slotFilterStacks"); stacks != it->end() && stacks->is_object()) {
        for (auto const& [k, v] : stacks->items()) {
            int s = slotOf(k);
            if (s < 0) continue;
            auto d = core::ItemStackData::fromJson(v);
            if (!d.empty()) out[s] = d.withCount(1);
        }
    }
    return out;
}

std::vector<int> templateNoSort(json const& tmpl, int& color) {
    std::vector<int> out;
    color   = kDefaultNoSort;
    auto it = tmpl.find("no_sort");
    if (it == tmpl.end() || !it->is_object()) return out;
    if (it->contains("color")) color = validDye((*it)["color"], kDefaultNoSort);
    if (auto s = it->find("selectedSlots"); s != it->end() && s->is_array()) {
        for (auto const& v : *s) {
            if (v.is_number_integer() && v.get<int>() >= 0) out.push_back(v.get<int>());
        }
    }
    return out;
}
} // namespace

void loadSettingsTemplate(game::Backpack& backpack, nlohmann::json const& tmpl) {
    if (!tmpl.is_object()) return;
    int size = backpack.inventory().size();
    // backpackGlobal: SC MainSettingsCategory.overwriteWith is a no-op.

    // item_display: slots, rotations, z offsets and colour of the template.
    if (auto it = tmpl.find("item_display"); it != tmpl.end()) {
        auto d = displayFromJson(*it, size);
        if (static_cast<int>(d.slots.size()) > kItemDisplayLimit) {
            d.slots.resize(kItemDisplayLimit);
            d.rotations.resize(kItemDisplayLimit);
            d.zOffsets.resize(kItemDisplayLimit);
        }
        writeDisplay(backpack, d);
    } else {
        writeDisplay(backpack, {});
    }

    // no_sort: selected slots + colour.
    int  color = kDefaultNoSort;
    auto slots = templateNoSort(tmpl, color);
    setNoSortAll(backpack, false);
    auto& g = detail::guiSection(backpack);
    json  sel = json::array();
    for (int s : slots) {
        if (s < size) sel.push_back(s);
    }
    if (!sel.empty()) g["noSort"] = std::move(sel);
    setNoSortColor(backpack, color);

    // memory: unselect all, take the template's mode, then its entries where the slot is empty or
    // already holds a matching stack.
    bool ignore  = true;
    auto entries = templateMemory(tmpl, ignore);
    g.erase("memory");
    g["memoryIgnoreNbt"] = ignore;
    json mem = json::object();
    auto const& inv = backpack.inventory();
    for (auto const& [slot, item] : entries) {
        if (slot >= size) continue;
        auto const& cur = inv.at(slot);
        if (!cur.empty() && !memoryAccepts(ignore, item, cur)) continue;
        auto entry = ignore ? core::ItemStackData::of(item.id, 1, item.maxStack, item.aux) : item;
        mem[std::to_string(slot)] = entry.toJson();
    }
    if (!mem.empty()) g["memory"] = std::move(mem);
    detail::slotSettingsChanged(backpack, true);
}

bool settingsTemplateTooLarge(nlohmann::json const& tmpl, int slots) {
    if (!tmpl.is_object()) return false;
    bool ignore = true;
    for (auto const& [s, item] : templateMemory(tmpl, ignore)) {
        if (s >= slots) return true;
    }
    int color = 0;
    for (int s : templateNoSort(tmpl, color)) {
        if (s >= slots) return true;
    }
    if (auto it = tmpl.find("item_display"); it != tmpl.end()) {
        auto d = displayFromJson(*it, -1);
        for (int s : d.slots) {
            if (s >= slots) return true;
        }
    }
    return false;
}

nlohmann::json settingsTemplatePreview(nlohmann::json const& tmpl) {
    json out = json::object();
    bool ignore = true;
    json mem    = json::object();
    for (auto const& [s, item] : templateMemory(tmpl, ignore)) mem[std::to_string(s)] = item.toJson();
    out["memory"] = std::move(mem);
    int  color    = kDefaultNoSort;
    auto slots    = templateNoSort(tmpl, color);
    out["noSort"] = json{{"slots", slots}, {"color", color}};
    ItemDisplay d;
    if (auto it = tmpl.find("item_display"); it != tmpl.end()) d = displayFromJson(*it, -1);
    json rot = json::object();
    for (std::size_t i = 0; i < d.slots.size(); ++i) rot[std::to_string(d.slots[i])] = d.rotations[i];
    out["itemDisplay"] = json{{"slots", d.slots}, {"color", d.color}, {"rotations", std::move(rot)}};
    return out;
}

} // namespace abp::feature::gui
