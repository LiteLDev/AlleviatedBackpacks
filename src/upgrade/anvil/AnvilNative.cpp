// Server side of the anvil's native tab (SB AnvilUpgradeTab / AnvilUpgradeContainer).
//
// Tab data (`upgrades[i].tab.anvil`):
//   {"left":item|null,"right":item|null,"result":item|null,"cost":n,"tooExpensive":b,
//    "canTake":b,"name":"..."}
// Tab actions:
//   {"click":"left|right|result","button":0|1,"shift":b}   a click on one of the anvil's slots
//   {"name":"text","default":"client name"}                  the item name box changed
//   {"tabOpen":bool,"uuid":..}                                the tab opened / closed (shift-click
//                                                             into the open tab, crafting/Station.h)

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "net/NativeScreens.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/anvil/AnvilUpgrade.h"
#include "upgrade/anvil/StationNative.h"
#include "upgrade/crafting/Station.h"

namespace abp::upgrade::anvil {
namespace {

StationRef refFor(UpgradeContext const& ctx) { return StationRef(ctx.withPlayer(nullptr), "anvil"); }

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    auto ref   = refFor(ctx);
    auto o     = stationOutcome(player, ref);
    bool shown = o.hasResult() && !o.tooExpensive;
    return nlohmann::json{
        {"anvil",
         {
             {"left", stackJson(leftSlot(ref)->display(player))},
             {"right", stackJson(rightSlot(ref)->display(player))},
             {"result", shown ? stackJson(o.result) : nlohmann::json(nullptr)},
             // Vanilla AnvilMenu cost: 0 without a result, unless it is too expensive.
             {"cost", o.hasResult() ? o.cost : 0},
             {"tooExpensive", o.hasResult() && o.tooExpensive},
             {"canTake", shown && canAfford(player, o)},
             {"name", itemName(ref)},
         }}
    };
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (!data.is_object()) return false;
    if (crafting::handleTabOpen(player, ctx, data)) return true;
    auto ref = refFor(ctx);
    if (!ref.valid()) return false;
    if (auto it = data.find("name"); it != data.end()) {
        if (!it->is_string()) return false;
        std::optional<std::string> def;
        if (auto d = data.find("default"); d != data.end() && d->is_string()) def = d->get<std::string>();
        setItemName(ref, it->get<std::string>(), def);
        return true;
    }
    auto which = data.value("click", std::string{});
    std::optional<PendingUndo> pending; // lives until the click is done (undo of a handed-back result)
    std::shared_ptr<ui::Slot> slot;
    if (which == "left") slot = leftSlot(ref);
    else if (which == "right") slot = rightSlot(ref);
    else if (which == "result") slot = resultSlot(ref, pending);
    if (!slot) return false;
    bool ok = clickStationSlot(player, slot, data.value("button", 0), data.value("shift", false), ctx.backpack());
    ui::MenuManager::get().resyncPlayer(player);
    return ok;
}

/// SC mergeStackToOpenUpgradeTab: the anvil's two inputs (both take anything), stacks of the
/// same item first, then empty slots.
int quickMoveIn(Player& player, UpgradeContext const& ctx, core::ItemStackData const& item) {
    auto ref = refFor(ctx);
    return mergeIntoStation(player, {leftSlot(ref), rightSlot(ref)}, item);
}

} // namespace
} // namespace abp::upgrade::anvil

ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{.upgradeId = "anvil", .quickMoveIn = abp::upgrade::anvil::quickMoveIn});

ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{.upgrade = "anvil", .data = abp::upgrade::anvil::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{.upgrade = "anvil", .handle = abp::upgrade::anvil::tabAction});
