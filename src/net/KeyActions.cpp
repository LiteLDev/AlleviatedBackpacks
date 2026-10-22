// Server side of Sophisticated Backpacks' key bindings (`KeyActionPacket`):
//
//   {"action":"open"}                                 Open Backpack (B) in game: the first carried backpack
//                                                     in SB's order (chest slot, offhand, inventory 0..35)
//   {"action":"openAt","where":"inventory|armor|offhand","slot":n}
//                                                     B over a backpack in an inventory screen
//   {"action":"interact"}                             Inventory interaction (C): Deposit / Restock
//                                                     of the first backpack on the container looked at
//   {"action":"toolSwap"}                             Tool swap: best tool for the looked-at block/entity
//   {"action":"toggleUpgrade","slot":n}               Toggle upgrade N of the first carried backpack
//   {"action":"stashQuery","q":n,"slots":[loc..]}     Right-click stashing overlay (answered with
//                                                     `StashInfoPacket`), see feature/stash/Stash.h
//   {"action":"stash","area":"..","slot":n}           Right click with the cursor on a slot
//
// B inside the native screen (nested backpack, back, close) and the sort / transfer keys are
// screen actions (NativeScreens.cpp), so they are bound to the open menu's session.

#include <algorithm>
#include <chrono>
#include <unordered_map>

#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "feature/stash/Stash.h"
#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "net/Packets.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/ContainerTransfer.h"
#include "upgrade/toolswapper/ToolSwapper.h"

namespace abp::net {

namespace {
constexpr float kReach = 5.5f;

/// SB `PlayerInventoryProvider` order: armor (chest slot), offhand, main inventory 0..35.
int sbRank(game::ItemLocation const& w) {
    using Kind = game::ItemLocation::Kind;
    switch (w.kind) {
    case Kind::Armor:
        return 0;
    case Kind::Offhand:
        return 1;
    case Kind::Inventory:
        return 2 + w.slot;
    }
    return 100;
}

std::vector<std::pair<game::CarriedBackpack, std::shared_ptr<game::Backpack>>> ranked(Player& player) {
    auto list = game::carriedBackpacks(player, false, false);
    std::stable_sort(list.begin(), list.end(), [](auto const& a, auto const& b) { return sbRank(a.first.where) < sbRank(b.first.where); });
    return list;
}

/// SB `BackpackOpenPayload` without a slot: `findAndOpenFirstBackpack`.
bool openFirst(Player& player) {
    auto carried = game::scanBackpacks(player);
    std::stable_sort(carried.begin(), carried.end(), [](auto const& a, auto const& b) { return sbRank(a.where) < sbRank(b.where); });
    for (auto const& c : carried) {
        if (ui::BackpackMenu::openFor(player, c.where)) return true;
    }
    return false;
}

bool openAt(Player& player, nlohmann::json const& body) {
    auto where = body.value("where", std::string{});
    int  slot  = body.value("slot", -1);
    game::ItemLocation loc;
    if (where == "inventory" && slot >= 0 && slot < 36) {
        loc = {game::ItemLocation::Kind::Inventory, slot};
    } else if (where == "armor") {
        loc = {game::ItemLocation::Kind::Armor, 1}; // SB: the chest slot
    } else if (where == "offhand") {
        loc = {game::ItemLocation::Kind::Offhand, 0};
    } else {
        return false;
    }
    auto const& item = game::itemAt(player, loc);
    if (!game::isBackpack(item) || item.mCount != 1) return false;
    log().info("key action: {} opens the backpack at {} {}", player.getRealName(), where, slot);
    return ui::BackpackMenu::openFor(player, loc);
}

bool interact(Player& player) {
    auto hit = player.traceRay(kReach, false, true);
    if (hit.mType != HitResultType::Tile) return false;
    auto container = player.getDimensionBlockSource().tryGetContainer(hit.mBlock);
    if (!container) return false;
    // SB InventoryInteractionPayload: runs on the first backpack only (runOnBackpacks -> true).
    auto list = ranked(player);
    if (list.empty() || !list.front().second) return false;
    auto result = upgrade::transfer::interact(list.front().second, player, *container, static_cast<int>(hit.mFacing));
    player.swing(ActorSwingSource::Interact, HandSlot::Mainhand); // SB: player.swing(MAIN_HAND, true)
    if (!result.handled) return false;
    if (!result.message.empty()) upgrade::transfer::sendTip(player, result.message);
    log().info(
        "key action: {} ran inventory interaction ({} stacks deposited, {} restocked)",
        player.getRealName(),
        result.deposited.stacks,
        result.restocked.stacks
    );
    return true;
}

/// SB: only upgrades that swap on key press (the Advanced Tool Swapper) can handle the key.
bool hasKeyToolSwapper(Player& player) {
    for (auto const& [carried, bp] : ranked(player)) {
        if (bp && upgrade::hasActive(*bp, upgrade::toolswapper::kAdvancedId)) return true;
    }
    return false;
}

/// SB `player.displayClientMessage(Component.translatable("gui.sophisticatedbackpacks.status.<key>"), true)`.
void status(Player& player, std::string_view key, std::string_view fallback) {
    log().info("key action: {} status {}", player.getRealName(), key);
    upgrade::transfer::sendTip(player, trFor(player, "abp.input.status." + std::string(key), fallback));
}

bool toolSwap(Player& player) {
    if (game::isBackpack(player.getSelectedItem())) {
        status(player, "unable_to_swap_tool_for_backpack", "Unable to swap tool for backpack in hand. Switch to different item or empty hand.");
        return false;
    }
    auto hit = player.traceRay(kReach, true, true);
    if (hit.mType != HitResultType::Tile && hit.mType != HitResultType::Entity) return false; // SB sends nothing
    if (!hasKeyToolSwapper(player)) {
        status(player, "no_tool_swap_upgrade_present", "No upgrade present that can do tool swap on key press");
        return false;
    }
    bool ok = false;
    if (hit.mType == HitResultType::Tile) {
        ok = upgrade::toolswapper::swapForBlock(player, player.getDimensionBlockSource().getBlock(hit.mBlock));
        if (!ok) status(player, "no_tool_found_for_block", "No tool was found that works on block");
        return ok;
    }
    auto* target = hit.getEntity();
    if (!target) return false;
    ok = upgrade::toolswapper::isShearable(*target) ? upgrade::toolswapper::swapForEntityUse(player, *target)
                                                    : upgrade::toolswapper::swapForAttack(player);
    if (!ok) status(player, "no_tool_found_for_entity", "No tool was found that works on entity");
    return ok;
}

/// SB `UpgradeTogglePayload`: the first backpack (SB order), only upgrades that can be disabled.
bool toggleUpgrade(Player& player, int slot) {
    auto list = ranked(player);
    if (list.empty() || !list.front().second || slot < 0) return false;
    auto const& bp = list.front().second;
    if (slot >= static_cast<int>(bp->data().upgrades.size())) return false;
    auto const& st   = bp->data().upgrades[static_cast<std::size_t>(slot)];
    auto*       type = st.empty() ? nullptr : upgrade::upgradeFor(st.item);
    if (!type || !type->info().toggleable) {
        log().info("key action: {} toggle upgrade {}: nothing that can be switched", player.getRealName(), slot + 1);
        return false;
    }
    bool on = !st.enabled;
    upgrade::setEnabled(bp, slot, on);
    auto name = upgrade::displayName(*type, &player);
    log().info("key action: {} switched upgrade {} ({}) {}", player.getRealName(), slot + 1, type->info().id, on ? "on" : "off");
    upgrade::transfer::sendTip(
        player,
        on ? trFor(player, "abp.input.status.upgrade_switched_on", "{} Switched On", name)
           : trFor(player, "abp.input.status.upgrade_switched_off", "{} Switched Off", name)
    );
    return true;
}

// ---- right-click stashing ------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;
std::unordered_map<std::int64_t, std::pair<Clock::time_point, int>> gQueryRate; // per player: window start, count
constexpr int kQueriesPerSecond = 16; ///< queries (client: <= 10/s) and stash clicks together

bool queryAllowed(Player const& player) {
    auto& [start, n] = gQueryRate[player.getOrCreateUniqueID().rawID];
    auto now         = Clock::now();
    if (now - start > std::chrono::seconds(1)) {
        start = now;
        n     = 0;
    }
    if (gQueryRate.size() > 512) gQueryRate.clear();
    return ++n <= kQueriesPerSecond;
}

bool stashQuery(Player& player, nlohmann::json const& body) {
    if (!queryAllowed(player)) return false;
    auto answer = feature::stash::query(player, body);
    answer["q"] = body.value("q", 0);
    send(player, StashInfoPacket{std::move(answer)});
    return true;
}

bool stash(Player& player, nlohmann::json const& body) {
    if (!queryAllowed(player)) return false; // shares the stash rate limit (a click is far below it)
    auto loc = feature::stash::parseLocation(body);
    if (!loc) return false;
    int moved = feature::stash::rightClick(player, *loc);
    log().info("key action: {} stash at {}: {} moved", player.getRealName(), feature::stash::key(*loc), moved);
    // The client predicted nothing (it swallowed the click): send the server truth.
    if (moved > 0) ui::MenuManager::get().resyncPlayer(player);
    return moved > 0;
}
} // namespace

bool handleKeyAction(Player& player, nlohmann::json const& body) {
    if (!body.is_object() || !player.isAlive()) return false;
    auto action = body.value("action", std::string{});
    if (action == "open") {
        if (ui::MenuManager::get().current(player)) return false;
        log().info("key action: {} opens the carried backpack", player.getRealName());
        return openFirst(player);
    }
    if (action == "openAt") return openAt(player, body);
    if (action == "interact") return interact(player);
    if (action == "toolSwap") return toolSwap(player);
    if (action == "toggleUpgrade") return toggleUpgrade(player, body.value("slot", -1));
    if (action == "stashQuery") return stashQuery(player, body);
    if (action == "stash") return stash(player, body);
    return false;
}

} // namespace abp::net
