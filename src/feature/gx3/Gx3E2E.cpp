// GX-3 client e2e support (`tools/e2e/client/scenarios/gx3.ps1`):
//
//   /backpack gx3 setup   hotbar: 0 gold backpack A (pickup upgrade, contents), 1 diamond backpack D
//                         (contents), 2 red dye, 3 blue dye, 4 netherite upgrade template, 5 netherite
//                         ingot, 6 16 cobblestone, 7 copper backpack E (9 apples),
//                         8 empty; inventory 9: advanced tool swapper upgrade (long tooltip),
//                         10: 8 iron ingots, 11: 4 coal, 12: 4 raw iron, 13: 6 apples
//   /backpack gx3 where   logs "gx3 where: <tier>@<uuid> at <place>" for every backpack item the
//                         player has (inventory, armor, offhand, UI slots incl. the cursor)

#include <algorithm>
#include <string>

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/InventorySlotPacketPayload.h"
#include "mc/world/ContainerID.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"

#include "core/Tier.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"

namespace {
using namespace abp;

/// Test helpers: operators only (they hand out items).
Player* playerOf(CommandOrigin const& origin, CommandOutput& output) {
    if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
        output.error("operators only");
        return nullptr;
    }
    auto* e = origin.getEntity();
    if (!e || !e->isPlayer() || !game::Storage::get().available()) {
        output.error("players only");
        return nullptr;
    }
    return static_cast<Player*>(e);
}

/// Moves `count` items (0 = all) from inventory slot `from` into UI slot `to` (crafting grid,
/// smithing table), so the client shows them when the screen opens.
void toUi(Player& player, int from, PlayerUISlot to, int count = 0) {
    auto item = game::itemAt(player, {game::ItemLocation::Kind::Inventory, from});
    if (item.isNull()) return;
    int n = count > 0 ? std::min<int>(count, item.mCount) : item.mCount;
    ItemStack moved = item;
    moved.set(n);
    if (n >= item.mCount) item = ItemStack{};
    else item.set(item.mCount - n);
    game::setItemAt(player, {game::ItemLocation::Kind::Inventory, from}, item);
    player.mPlayerUIContainer->setItem(static_cast<int>(to), moved);
    InventorySlotPacket pkt{InventorySlotPacketPayload{ContainerID::PlayerOnlyUi, static_cast<int>(to), moved}};
    pkt.sendTo(player);
}

PlayerUISlot uiAt(PlayerUISlot first, int offset) { return static_cast<PlayerUISlot>(static_cast<int>(first) + offset); }

std::string describe(ItemStack const& s) {
    auto tier = game::backpackTierOf(s);
    return std::string(tier ? core::tierInfo(*tier).id : "?") + "@" + game::backpackUuid(s).value_or("(none)");
}
} // namespace

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("gx3").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin, output);
        if (!player) return;
        auto& storage = game::Storage::get();
        auto  a       = storage.create(core::Tier::Gold);
        auto  d       = storage.create(core::Tier::Diamond);
        auto  e       = storage.create(core::Tier::Copper);
        if (!a || !d || !e) {
            output.error("could not create backpacks");
            return;
        }
        auto put = [](game::Backpack& bp, int slot, std::string_view id, int count) {
            bp.inventory().set(slot, game::ItemConv::toData(game::ItemConv::make(id, 1)).withCount(count));
        };
        put(*a, 0, "minecraft:cobblestone", 40);
        put(*a, 1, "minecraft:dirt", 30);
        put(*d, 0, "minecraft:emerald", 7);
        put(*d, 1, "minecraft:gold_ingot", 12);
        put(*e, 0, "minecraft:apple", 9);
        if (auto* t = upgrade::byId("pickup")) (void)upgrade::install(a, 0, game::ItemConv::toData(game::ItemConv::make(t->info().itemId, 1)), player);
        for (auto const& bp : {a, d, e}) {
            bp->markDirty();
            storage.save(*bp, true);
        }
        auto give = [&](int slot, ItemStack const& item) { game::setItemAt(*player, {game::ItemLocation::Kind::Inventory, slot}, item); };
        for (int i = 0; i < 36; ++i) give(i, ItemStack{});
        give(0, game::makeBackpackItem(core::Tier::Gold, a->uuid()));
        give(1, game::makeBackpackItem(core::Tier::Diamond, d->uuid()));
        give(2, game::ItemConv::make("minecraft:red_dye", 1));
        give(3, game::ItemConv::make("minecraft:blue_dye", 1));
        give(4, game::ItemConv::make("minecraft:netherite_upgrade_smithing_template", 1));
        give(5, game::ItemConv::make("minecraft:netherite_ingot", 1));
        give(6, game::ItemConv::make("minecraft:cobblestone", 16));
        give(7, game::makeBackpackItem(core::Tier::Copper, e->uuid()));
        if (auto* t = upgrade::byId("advanced_tool_swapper")) give(9, game::ItemConv::make(t->info().itemId, 1));
        give(10, game::ItemConv::make("minecraft:iron_ingot", 8));
        give(11, game::ItemConv::make("minecraft:coal", 4));
        give(12, game::ItemConv::make("minecraft:raw_iron", 4));
        give(13, game::ItemConv::make("minecraft:apple", 6));
        player->sendInventory(false);
        log().info("gx3 setup: A={} D={} E={}", a->uuid(), d->uuid(), e->uuid());
        output.success("gx3 setup: A=" + a->uuid() + " D=" + d->uuid() + " E=" + e->uuid());
    });
    // Station slots filled before the screen opens (the client only reports positions of slots
    // that show an item):
    //   grid2:    2x2 grid 0 = the 6 apples (inventory 13), 1 = the red dye (inventory 2);
    //   table:    3x3 grid 4 = copper backpack E (inventory 7), 1/3/5/7 = one iron ingot each
    //             (inventory 10), 8 = the blue dye (inventory 3) - the tier recipe once 8 is empty;
    //   smithing: template (inventory 4), Diamond backpack D (inventory 1), netherite ingot (5).
    cmd.overload().text("gx3").text("grid2").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin, output);
        if (!player) return;
        toUi(*player, 13, PlayerUISlot::Crafting2x2Input1);
        toUi(*player, 2, uiAt(PlayerUISlot::Crafting2x2Input1, 1));
        player->sendInventory(false);
        log().info("gx3 grid2 ready");
        output.success("gx3 grid2 ready");
    });
    cmd.overload().text("gx3").text("table").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin, output);
        if (!player) return;
        toUi(*player, 7, uiAt(PlayerUISlot::Crafting3x3Input1, 4));
        for (int i : {1, 3, 5, 7}) toUi(*player, 10, uiAt(PlayerUISlot::Crafting3x3Input1, i), 1);
        toUi(*player, 3, uiAt(PlayerUISlot::Crafting3x3Input1, 8));
        player->sendInventory(false);
        log().info("gx3 table ready");
        output.success("gx3 table ready");
    });
    cmd.overload().text("gx3").text("smithing").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin, output);
        if (!player) return;
        toUi(*player, 4, PlayerUISlot::SmithingTableTemplate);
        toUi(*player, 1, PlayerUISlot::SmithingTableInput);
        toUi(*player, 5, PlayerUISlot::SmithingTableMaterial);
        player->sendInventory(false);
        log().info("gx3 smithing ready");
        output.success("gx3 smithing ready");
    });
    cmd.overload().text("gx3").text("where").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin, output);
        if (!player) return;
        int found = 0;
        for (auto const& c : game::scanBackpacks(*player)) {
            auto const& s     = game::itemAt(*player, c.where);
            auto        place = c.where.kind == game::ItemLocation::Kind::Armor     ? "armor:" + std::to_string(c.where.slot)
                              : c.where.kind == game::ItemLocation::Kind::Offhand ? std::string("offhand")
                                                                                   : "inventory:" + std::to_string(c.where.slot);
            log().info("gx3 where: {} at {}", describe(s), place);
            ++found;
        }
        for (int i = 0; i < static_cast<int>(PlayerUISlot::Count); ++i) {
            auto const& s = player->mPlayerUIContainer->getItem(i);
            if (s.isNull() || !game::isBackpack(s)) continue;
            log().info("gx3 where: {} at {}", describe(s), i == 0 ? std::string("cursor") : "ui:" + std::to_string(i));
            ++found;
        }
        log().info("gx3 where: {} backpacks", found);
        output.success("gx3 where: " + std::to_string(found) + " backpacks");
    });
});
