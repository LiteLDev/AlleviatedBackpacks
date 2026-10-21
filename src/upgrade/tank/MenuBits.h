#pragma once

// Small chest-menu helpers shared by the Tank, Pump and XP Pump upgrade menus.

#include <functional>
#include <memory>
#include <string>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
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

/// Extra ticks between closing a chest menu and opening the next one. Replacing a menu directly,
/// or reopening right after `closeThen`, left the real client without a screen (the server logged
/// the menu as shown): the client needs a moment after a ContainerClose.
inline constexpr int kReopenDelayTicks = 10;

/// Opens `menu` after closing the current one (a clean screen switch on the client).
/// Runs `fn(player)` after the open menu has closed and the client had time to drop its screen.
/// Without a menu open, `fn` runs right away.
inline void afterClientClose(Player& player, std::function<void(Player&)> fn) {
    auto& mm = ui::MenuManager::get();
    if (!mm.current(player)) {
        fn(player);
        return;
    }
    auto id = player.getOrCreateUniqueID();
    mm.closeThen(player, [id, fn = std::move(fn)](Player&) {
        ll::coro::keepThis([id, fn]() -> ll::coro::CoroTask<> {
            co_await ll::chrono::ticks(kReopenDelayTicks);
            auto level = ll::service::getLevel();
            if (!level) co_return;
            if (auto* p = level->getPlayer(id)) fn(*p);
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    });
}

inline void openFresh(Player& player, std::shared_ptr<ui::Menu> menu) {
    if (!menu) return;
    afterClientClose(player, [menu = std::move(menu)](Player& p) { ui::MenuManager::get().open(p, menu); });
}

/// "Back" button returning to the backpack's upgrades page.
inline std::shared_ptr<ui::Slot> backButton(Player& player, std::shared_ptr<game::Backpack> backpack) {
    return std::make_shared<ui::ButtonSlot>(
        ui::icon("minecraft:arrow", trFor(player, "abp.menu.back_generic", "Back")),
        [bp = std::move(backpack)](ui::ClickContext const& c) {
            afterClientClose(c.player, [bp](Player& p) {
                ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
            });
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
