#pragma once

// Small chest-menu helpers shared by the Tank, Pump and XP Pump upgrade menus.

#include <functional>
#include <memory>
#include <string>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/PlayerScan.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "ui/menu/Slot.h"
#include "upgrade/IUpgrade.h"

namespace abp::upgrade::fluidui {

inline std::shared_ptr<ui::Slot> filler(std::string_view id = "minecraft:gray_stained_glass_pane") {
    return std::make_shared<ui::StaticSlot>(ui::icon(id, " "));
}

/// A non-interactive slot whose icon is recomputed on every sync (live gauges and counters).
inline std::shared_ptr<ui::Slot> live(std::function<ItemStack(Player&)> display) {
    auto s       = std::make_shared<ui::CallbackSlot>();
    s->onDisplay = std::move(display);
    s->onClick   = [](ui::ClickContext const&) { return true; };
    return s;
}

/// "Back" button returning to the backpack's upgrades page.
inline std::shared_ptr<ui::Slot> backButton(Player& player, std::shared_ptr<game::Backpack> backpack) {
    return std::make_shared<ui::ButtonSlot>(
        ui::icon("minecraft:arrow", trFor(player, "abp.menu.back_generic", "Back")),
        [bp = std::move(backpack)](ui::ClickContext const& c) {
            ui::BackpackMenu::openBackpack(c.player, bp, {}, ui::BackpackMenu::View::Upgrades);
        }
    );
}

/// Locks the backpack item (if the player carries it in the main inventory) while `menu` is open,
/// like the backpack GUI does.
inline void lockBackpackItem(ui::Menu& menu, Player& player, game::Backpack const& backpack) {
    for (auto const& c : game::scanBackpacks(player)) {
        if (c.uuid && *c.uuid == backpack.uuid() && c.where.kind == game::ItemLocation::Kind::Inventory) {
            menu.lockedInventorySlot = c.where.slot;
            return;
        }
    }
}

inline std::string onOff(Player& player, bool on) {
    return on ? trFor(player, "abp.tank.on", "§aOn") : trFor(player, "abp.tank.off", "§cOff");
}

} // namespace abp::upgrade::fluidui
