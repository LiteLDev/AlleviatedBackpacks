// `/backpack misc setup`: the backpack of the client e2e scenario `misc_upgrades` (P2-9: jukebox,
// refill, tool swapper, inception, everlasting). Gives the calling player a netherite backpack with
// an Advanced Jukebox (3 discs), Advanced Refill (torch -> hotbar 3, bread -> main hand), Advanced
// Tool Swapper, Inception and Everlasting upgrade, a leather backpack stored in its first slot
// and some tools; the player's inventory is cleared first and the backpack goes to hotbar slot 0.

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/jukebox/Jukebox.h"
#include "upgrade/refill/Refill.h"

namespace abp::upgrade::jukebox {
namespace {

core::ItemStackData itemData(std::string_view id, int count = 1) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

} // namespace
} // namespace abp::upgrade::jukebox

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("misc").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        using namespace abp;
        using upgrade::jukebox::itemData;
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer() || !game::Storage::get().available()) {
            output.error("players only");
            return;
        }
        auto& player = static_cast<Player&>(*e);
        if (!player.isOperator()) { // test setup: it hands out items
            output.error("operators only");
            return;
        }
        auto bp = game::Storage::get().create(core::Tier::Netherite);
        auto  inner  = game::Storage::get().create(core::Tier::Leather);
        if (!bp || !inner) {
            output.error("could not create a backpack");
            return;
        }
        int slot = 0;
        for (auto const* id : {"advanced_jukebox", "advanced_refill", "advanced_tool_swapper", "inception", "everlasting"}) {
            auto* type = upgrade::byId(id);
            if (!type) continue;
            std::string reason;
            if (!upgrade::install(bp, slot, itemData(type->info().itemId), &player, &reason)) {
                output.error(std::string("install ") + id + ": " + reason);
                return;
            }
            ++slot;
        }
        upgrade::UpgradeContext jb(bp, 0, &player);
        upgrade::jukebox::setDisc(jb, 0, itemData("minecraft:music_disc_cat"));
        upgrade::jukebox::setDisc(jb, 1, itemData("minecraft:music_disc_blocks"));
        upgrade::jukebox::setDisc(jb, 2, itemData("minecraft:music_disc_13"));
        upgrade::UpgradeContext rf(bp, 1, &player);
        (void)upgrade::refill::setGhost(rf, true, 0, itemData("minecraft:torch"));
        upgrade::refill::setTarget(rf, true, 0, static_cast<upgrade::refill::Target>(static_cast<int>(upgrade::refill::Target::Hotbar1) + 2));
        (void)upgrade::refill::setGhost(rf, true, 1, itemData("minecraft:bread"));
        upgrade::refill::setTarget(rf, true, 1, upgrade::refill::Target::MainHand);
        auto& inv = bp->inventory();
        inv.set(0, game::ItemConv::toData(game::makeBackpackItem(core::Tier::Leather, inner->uuid())));
        inv.set(1, itemData("minecraft:diamond_shovel"));
        inv.set(2, itemData("minecraft:iron_sword"));
        inv.set(3, itemData("minecraft:shears"));
        inv.set(4, itemData("minecraft:torch", 64));
        inv.set(5, itemData("minecraft:bread", 32));
        inner->inventory().set(0, itemData("minecraft:apple", 5));
        bp->markDirty();
        inner->markDirty();
        game::Storage::get().save(*bp, true);
        game::Storage::get().save(*inner, true);
        auto& pinv = player.getInventory();
        for (int i = 0; i < pinv.getContainerSize(); ++i) pinv.setItem(i, ItemStack::EMPTY_ITEM());
        pinv.setItem(0, game::makeBackpackItem(core::Tier::Netherite, bp->uuid()));
        pinv.setItem(4, game::ItemConv::make("minecraft:iron_pickaxe", 1));
        player.setSelectedSlot(0);
        player.sendInventory(true);
        log().info("misc setup: gave netherite backpack {} (inner {})", bp->uuid(), inner->uuid());
        output.success("misc setup: gave netherite backpack " + bp->uuid());
    });
});
