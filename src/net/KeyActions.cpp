// Server side of Sophisticated Backpacks' key bindings (`KeyActionPacket`):
//
//   {"action":"open"}                                 Open Backpack (B) in game: the carried backpack
//   {"action":"openAt","where":"inventory|armor|offhand","slot":n}
//                                                     B over a backpack in an inventory screen
//   {"action":"interact"}                             Inventory interaction (C): Deposit / Restock
//                                                     on the container the player looks at
//   {"action":"toolSwap"}                             Tool swap: best tool for the looked-at block/entity
//   {"action":"toggleUpgrade","slot":n}               Toggle upgrade N of the carried backpack
//
// B inside the native screen (nested backpack, back, close) and the sort / transfer keys are
// screen actions (NativeScreens.cpp), so they are bound to the open menu's session.

#include <algorithm>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/NativeScreens.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/ContainerTransfer.h"
#include "upgrade/toolswapper/ToolSwapper.h"

namespace abp::net {

namespace {
constexpr float kReach = 5.5f;

/// Carried backpacks in the order Open Backpack uses: worn, offhand, selected hotbar slot, rest.
std::vector<std::pair<game::CarriedBackpack, std::shared_ptr<game::Backpack>>> ranked(Player& player) {
    using Kind = game::ItemLocation::Kind;
    auto list  = game::carriedBackpacks(player, false, false);
    auto sel   = game::selectedSlot(player);
    auto rank  = [&](game::ItemLocation const& w) {
        switch (w.kind) {
        case Kind::Armor:
            return 0;
        case Kind::Offhand:
            return 1;
        case Kind::Inventory:
            return w == sel ? 2 : 3;
        }
        return 4;
    };
    std::stable_sort(list.begin(), list.end(), [&](auto const& a, auto const& b) { return rank(a.first.where) < rank(b.first.where); });
    return list;
}

bool openAt(Player& player, nlohmann::json const& body) {
    auto where = body.value("where", std::string{});
    int  slot  = body.value("slot", -1);
    game::ItemLocation loc;
    if (where == "inventory" && slot >= 0 && slot < 36) {
        loc = {game::ItemLocation::Kind::Inventory, slot};
    } else if (where == "armor") {
        loc = {game::ItemLocation::Kind::Armor, static_cast<int>(SharedTypes::Legacy::ArmorSlot::Torso)};
    } else if (where == "offhand") {
        loc = {game::ItemLocation::Kind::Offhand, 0};
    } else {
        return false;
    }
    auto const& item = game::itemAt(player, loc);
    if (!game::isBackpack(item) || item.mCount != 1) return false;
    return ui::BackpackMenu::openFor(player, loc);
}

bool interact(Player& player) {
    auto hit = player.traceRay(kReach, false, true);
    if (hit.mType != HitResultType::Tile) return false;
    // SB InventoryInteractionHelper: no interaction with blocks in noInteractionBlocks.
    if (upgrade::transfer::interactionDisallowed(player.getDimensionBlockSource(), hit.mBlock)) return false;
    auto container = player.getDimensionBlockSource().tryGetContainer(hit.mBlock);
    if (!container) return false;
    for (auto const& [carried, bp] : ranked(player)) {
        if (!bp) continue;
        auto result = upgrade::transfer::interact(bp, player, *container, static_cast<int>(hit.mFacing));
        if (!result.handled) continue;
        if (!result.message.empty()) upgrade::transfer::sendTip(player, result.message);
        log().info("key action: {} ran inventory interaction ({} stacks deposited, {} restocked)", player.getRealName(), result.deposited.stacks, result.restocked.stacks);
        return true;
    }
    return false;
}

bool hasToolSwapper(Player& player) {
    for (auto const& [carried, bp] : ranked(player)) {
        if (bp && upgrade::hasActive(*bp, upgrade::toolswapper::kGroup)) return true;
    }
    return false;
}

bool toolSwap(Player& player) {
    if (game::isBackpack(player.getSelectedItem())) {
        upgrade::transfer::sendTip(player, trFor(player, "abp.companion.status.swap_backpack", "Can't swap tool for a held backpack"));
        return false;
    }
    if (!hasToolSwapper(player)) {
        upgrade::transfer::sendTip(player, trFor(player, "abp.companion.status.no_tool_swap_upgrade", "No tool swapper upgrade installed"));
        return false;
    }
    auto hit = player.traceRay(kReach, true, true);
    bool ok  = false;
    if (hit.mType == HitResultType::Tile) {
        ok = upgrade::toolswapper::swapForBlock(player, player.getDimensionBlockSource().getBlock(hit.mBlock));
    } else if (hit.mType == HitResultType::Entity) {
        auto* target = hit.getEntity();
        ok = target && upgrade::toolswapper::isShearable(*target) ? upgrade::toolswapper::swapForEntityUse(player, *target)
                                                                  : upgrade::toolswapper::swapForAttack(player);
    } else {
        return false;
    }
    if (!ok) upgrade::transfer::sendTip(player, trFor(player, "abp.companion.status.no_tool", "No tool found to swap to"));
    return ok;
}

bool toggleUpgrade(Player& player, int slot) {
    auto list = ranked(player);
    if (list.empty() || !list.front().second || slot < 0) return false;
    auto const& bp = list.front().second;
    if (slot >= static_cast<int>(bp->data().upgrades.size())) return false;
    auto const& st   = bp->data().upgrades[static_cast<std::size_t>(slot)];
    auto*       type = st.empty() ? nullptr : upgrade::upgradeFor(st.item);
    if (!type || !type->info().toggleable) return false;
    bool on = !st.enabled;
    upgrade::setEnabled(bp, slot, on);
    auto name = upgrade::displayName(*type, &player);
    log().info("key action: {} switched upgrade {} ({}) {}", player.getRealName(), slot + 1, type->info().id, on ? "on" : "off");
    upgrade::transfer::sendTip(
        player,
        on ? trFor(player, "abp.companion.status.upgrade_on", "{} turned on", name)
           : trFor(player, "abp.companion.status.upgrade_off", "{} turned off", name)
    );
    return true;
}
} // namespace

bool handleKeyAction(Player& player, nlohmann::json const& body) {
    if (!body.is_object() || !player.isAlive()) return false;
    auto action = body.value("action", std::string{});
    if (action == "open") {
        if (ui::MenuManager::get().current(player)) return false;
        log().info("key action: {} opens the carried backpack", player.getRealName());
        return ui::BackpackMenu::openCarried(player);
    }
    if (action == "openAt") return openAt(player, body);
    if (action == "interact") return interact(player);
    if (action == "toolSwap") return toolSwap(player);
    if (action == "toggleUpgrade") return toggleUpgrade(player, body.value("slot", -1));
    return false;
}

} // namespace abp::net
