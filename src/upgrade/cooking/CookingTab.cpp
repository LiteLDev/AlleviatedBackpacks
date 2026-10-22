// Server side of the cooking upgrades' native SB tab (client/ui/tabs/cooking).
//
// Tab data (`UpgradeView::tab`, all six upgrades):
//   {"kind":"smelting|smoking|blasting", "auto":bool, "slots":[input, fuel, output],
//    "burning":bool, "burnLeft":ticks, "burnTotal":ticks, "cooking":bool, "cookLeft":ticks,
//    "cookTotal":ticks, "inputFilter":{filter, "slots":n, "perRow":n}, "fuelFilter":{..}}
// Tab actions:
//   {"slot":0|1|2,"button":b,"shift":s}           a furnace slot
//   {"filter":"input|fuel","ghost":i,"button":b}   a filter slot (cursor item -> ghost / clear)
//   {"filter":"input|fuel","op":"allowList|primaryMatch|durability|nbt"}
//   {"filter":"input|fuel","op":"addTag|removeTag","tag":t} {"tabOpen":bool,"uuid":..}

#include <algorithm>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "upgrade/filter/FilterTab.h"
#include "mod/I18n.h"
#include "net/NativeScreens.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/cooking/Cooking.h"
#include "upgrade/crafting/Station.h"

namespace abp::upgrade::cooking {

namespace {

using crafting::StateSlot;

core::ItemStackData& field(CookState& s, int slot) { return slot == kInputSlot ? s.input : slot == kFuelSlot ? s.fuel : s.output; }

std::shared_ptr<StateSlot> furnaceSlot(CookingUpgrade& u, UpgradeContext const& ctx, int slot) {
    return std::make_shared<StateSlot>(StateSlot::Ops{
        .get = [ctx, slot] {
            auto s = CookingUpgrade::state(ctx);
            return field(s, slot);
        },
        .set =
            [ctx, slot](core::ItemStackData const& d) {
                auto s        = CookingUpgrade::state(ctx);
                field(s, slot) = d;
                CookingUpgrade::setState(ctx, s);
            },
        .mayPlace =
            [&u, ctx, slot](core::ItemStackData const& d) {
                if (slot == kOutputSlot) return false; // SC: nothing can be put into the output
                return isItemValid(u.env(ctx), CookingUpgrade::state(ctx), slot, d);
            },
        .maxCount = {},
        .valid    = [&u, ctx] { return u.installedAt(ctx); },
    });
}

nlohmann::json itemJson(core::ItemStackData const& d) { return d.empty() ? nlohmann::json(nullptr) : d.toJson(); }

/// Settings key of a filter's tag selection slot (SC FilterLogicContainer.tagSelectionSlot).
std::string tagItemKey(bool fuel) { return fuel ? "fuelFilterTagItem" : "inputFilterTagItem"; }

core::ItemStackData tagItem(UpgradeContext const& ctx, bool fuel) {
    auto const& s = ctx.settings();
    if (!s.is_object()) return {};
    auto it = s.find(tagItemKey(fuel));
    return it != s.end() && it->is_object() ? core::ItemStackData::fromJson(*it) : core::ItemStackData{};
}

/// The shared FilterControl spec: SC AutoCookingUpgradeTab shows the input filter as
/// FilterLogicControl.Advanced and the fuel filter with its slots only.
filtering::FilterSpec filterSpec(bool fuel, core::Filter const& f, int perRow) {
    return filtering::FilterSpec{
        .slots       = static_cast<int>(f.ghosts.size()),
        .slotsPerRow = perRow,
        .advanced    = !fuel,
        .type        = fuel ? filtering::TypeButton::None : filtering::TypeButton::AllowList,
    };
}

nlohmann::json filterJson(UpgradeContext const& ctx, bool fuel, core::Filter const& f, int perRow) {
    auto j      = f.toJson();
    // The shared FilterControl state (client/ui/widgets/FilterControl.h).
    j.update(filtering::tabJson(f, filterSpec(fuel, f, perRow)));
    j["slots"]  = static_cast<int>(f.ghosts.size());
    j["perRow"] = perRow;
    auto item   = tagItem(ctx, fuel);
    j["tagItem"] = itemJson(item);
    nlohmann::json toAdd = nlohmann::json::array();
    if (!item.empty()) {
        for (auto const& t : game::ItemConv::tags().tagsOf(item)) {
            if (std::find(f.tags.begin(), f.tags.end(), t) == f.tags.end()) toAdd.push_back(t);
        }
    }
    j["tagsToAdd"] = std::move(toAdd);
    return j;
}

std::string_view kindName(Kind k) { return k == Kind::Smoking ? "smoking" : k == Kind::Blasting ? "blasting" : "smelting"; }

/// SC tab label and closed-tab tooltip (gui.sophisticatedcore.upgrades.<id>[.tooltip]).
std::pair<std::string, std::string> tabLabels(Player& player, CookingUpgrade const& u) {
    auto const& id = u.info().id;
    std::string label, tooltip;
    if (id == "smoking") label = "Smoking", tooltip = "Smoking";
    else if (id == "blasting") label = "Blasting", tooltip = "Blasting";
    else if (id == "auto_smelting") label = "Auto-smelt", tooltip = "Auto-Smelting";
    else if (id == "auto_smoking") label = "Auto-smoke", tooltip = "Auto-Smoking";
    else if (id == "auto_blasting") label = "Auto-blast", tooltip = "Auto-Blasting";
    else label = "Smelting", tooltip = "Smelting";
    return {trFor(player, "abp.cooking.tab." + id, label), trFor(player, "abp.cooking.tab." + id + ".tooltip", tooltip)};
}

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    auto* u = cookingAt(ctx);
    if (!u) return nlohmann::json::object();
    auto s       = CookingUpgrade::state(ctx);
    // SC pauses a disabled upgrade: no flame, no progress while it is off.
    bool enabled = ctx.state().enabled;
    auto j       = nlohmann::json{
        {"kind",      kindName(u->kind())                                       },
        {"auto",      u->automatic()                                            },
        {"slots",     {itemJson(s.input), itemJson(s.fuel), itemJson(s.output)}},
        {"burning",   enabled && s.burning()                                    },
        {"burnLeft",  s.burnLeft()                                              },
        {"burnTotal", s.burnTotal                                               },
        {"cooking",   enabled && s.cooking                                      },
        {"cookLeft",  s.cookLeft()                                              },
        {"cookTotal", s.cookTotal                                               },
    };
    auto [title, tooltip] = tabLabels(player, *u);
    j["tabLabel"]         = title;
    j["tabTooltip"]       = tooltip;
    if (u->automatic()) {
        j["inputFilter"] = filterJson(ctx, false, u->inputFilter(ctx), u->inputFilterSlotsInRow());
        j["fuelFilter"]  = filterJson(ctx, true, u->fuelFilter(ctx), u->fuelFilterSlotsInRow());
    }
    return j;
}

/// SC FilterLogic slot validity: the input filter takes cookable items, the fuel filter fuels.
bool filterAccepts(CookingUpgrade& u, bool fuel, core::ItemStackData const& d) {
    if (d.empty()) return true;
    if (fuel) return furnaceBurnTicks(d) > 0;
    return recipeResult(u.kind(), d).has_value();
}

bool filterAction(Player& player, CookingUpgrade& u, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (!u.automatic()) return false;
    bool fuel = data.value("filter", std::string{}) == "fuel";
    auto f    = fuel ? u.fuelFilter(ctx) : u.inputFilter(ctx);
    auto save = [&] {
        if (fuel) CookingUpgrade::setFuelFilter(ctx, f);
        else CookingUpgrade::setInputFilter(ctx, f);
    };
    if (auto it = data.find("ghost"); it != data.end() && it->is_number_integer()) {
        int i = it->get<int>();
        if (i < 0 || i >= static_cast<int>(f.ghosts.size())) return false;
        auto cursor = game::ItemConv::toData(crafting::cursorOf(player));
        if (cursor.empty()) {
            f.setGhost(i, {});
        } else {
            if (!filterAccepts(u, fuel, cursor)) return true;
            f.setGhost(i, cursor);
        }
        save();
        return true;
    }
    if (data.contains("tagItem")) {
        // SC tag selection slot: the cursor item (a copy) is the item whose tags can be added.
        auto  cursor = game::ItemConv::toData(crafting::cursorOf(player));
        auto& s      = ctx.settings();
        if (!s.is_object()) s = nlohmann::json::object();
        if (cursor.empty()) s.erase(tagItemKey(fuel));
        else s[tagItemKey(fuel)] = cursor.withCount(1).toJson();
        ctx.saveSettings(false);
        return true;
    }
    auto op = data.value("op", std::string{});
    // Actions of the shared FilterControl.
    if (op == "slot" || op == "type" || op == "primary" || op == "dur" || op == "nbt" || op == "anyTag" || op == "addTag"
        || op == "removeTag") {
        auto cursor = game::ItemConv::toData(crafting::cursorOf(player));
        if (op == "slot" && !cursor.empty() && !filterAccepts(u, fuel, cursor)) return true;
        int  perRow = fuel ? u.fuelFilterSlotsInRow() : u.inputFilterSlotsInRow();
        auto one    = cursor.empty() ? core::ItemStackData{} : cursor.withCount(1);
        if (filtering::applyAction(f, filterSpec(fuel, f, perRow), data, one)) save();
        return true;
    }
    if (op == "allowList") {
        f.mode = f.mode == core::FilterMode::Allow ? core::FilterMode::Deny : core::FilterMode::Allow;
    } else if (op == "primaryMatch") {
        f.matchBy = static_cast<core::MatchBy>((static_cast<int>(f.matchBy) + 1) % 3);
    } else if (op == "durability") {
        f.matchDurability = !f.matchDurability;
    } else if (op == "nbt") {
        f.matchNbt = !f.matchNbt;
    } else {
        return false;
    }
    save();
    return true;
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (crafting::handleTabOpen(player, ctx, data)) return true;
    auto* u = cookingAt(ctx);
    if (!u) return false;
    if (data.contains("filter")) return filterAction(player, *u, ctx, data);
    auto it = data.find("slot");
    if (it == data.end() || !it->is_number_integer()) return false;
    int slot = it->get<int>();
    if (slot < kInputSlot || slot > kOutputSlot) return false;
    auto before = CookingUpgrade::state(ctx);
    if (data.value("shift", false)) {
        // SC quickMoveStack of an upgrade slot: into the storage first, then the player inventory.
        auto item = field(before, slot);
        if (item.empty()) return true;
        auto left = crafting::moveOut(player, ctx.backpack(), item, true);
        auto s    = CookingUpgrade::state(ctx);
        field(s, slot) = left;
        CookingUpgrade::setState(ctx, s);
        ui::MenuManager::get().resyncPlayer(player);
    } else {
        crafting::clickSlot(player, furnaceSlot(*u, ctx, slot), data.value("button", 0), slot == kOutputSlot);
    }
    // SC: taking from the output awards the stored experience.
    if (slot == kOutputSlot && CookingUpgrade::state(ctx).output.count < before.output.count) CookingUpgrade::popExperience(ctx, player);
    return true;
}

/// SC mergeItemStack over the furnace slots: same item first, then the first empty valid slot.
int quickMoveIn(Player&, UpgradeContext const& ctx, core::ItemStackData const& item) {
    auto* u = cookingAt(ctx);
    if (!u || item.empty()) return 0;
    auto s          = CookingUpgrade::state(ctx);
    auto e          = u->env(ctx);
    int  toTransfer = std::min(item.count, std::max(item.maxStack, 1));
    int  max        = std::max(item.maxStack, 1);
    int  moved      = 0;
    for (int slot : {kInputSlot, kFuelSlot}) {
        auto& d = field(s, slot);
        if (toTransfer <= 0 || d.empty() || !d.sameItem(item) || !isItemValid(e, s, slot, item) || d.count >= max) continue;
        int n       = std::min(toTransfer, max - d.count);
        d.count    += n;
        toTransfer -= n;
        moved      += n;
    }
    for (int slot : {kInputSlot, kFuelSlot}) {
        auto& d = field(s, slot);
        if (toTransfer <= 0 || !d.empty() || !isItemValid(e, s, slot, item)) continue;
        d           = item.withCount(toTransfer);
        moved      += toTransfer;
        toTransfer  = 0;
    }
    if (moved > 0) CookingUpgrade::setState(ctx, s);
    return moved;
}

} // namespace

} // namespace abp::upgrade::cooking

ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{std::string(abp::upgrade::cooking::kGroup), &abp::upgrade::cooking::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{std::string(abp::upgrade::cooking::kGroup), &abp::upgrade::cooking::tabAction});

ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{.upgradeId = "smelting", .quickMoveIn = &abp::upgrade::cooking::quickMoveIn});
ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{.upgradeId = "smoking", .quickMoveIn = &abp::upgrade::cooking::quickMoveIn});
ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{.upgradeId = "blasting", .quickMoveIn = &abp::upgrade::cooking::quickMoveIn});
ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{.upgradeId = "auto_smelting", .quickMoveIn = &abp::upgrade::cooking::quickMoveIn});
ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{.upgradeId = "auto_smoking", .quickMoveIn = &abp::upgrade::cooking::quickMoveIn});
ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{.upgradeId = "auto_blasting", .quickMoveIn = &abp::upgrade::cooking::quickMoveIn});
