// E2E support for the client scenario `anvil_smithing_infinity`:
//   /backpack station setup   a diamond backpack with the Anvil and Smithing upgrades (hotbar 1),
//                             a backpack with the Survival Infinity upgrade and a few infinite
//                             items (hotbar 2), test items in the first main inventory row and
//                             30 levels
//   /backpack station probe   logs the player's inventory and the contents of the carried
//                             backpacks ("station probe: ..." lines)

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/anvil/AnvilLogic.h"

namespace abp::upgrade::anvil {
namespace {

Player* playerOf(CommandOrigin const& origin, CommandOutput& output) {
    auto* e = origin.getEntity();
    if (!e || !e->isPlayer() || !game::Storage::get().available()) {
        output.error("players only");
        return nullptr;
    }
    return static_cast<Player*>(e);
}

std::shared_ptr<game::Backpack> backpackWith(Player& player, core::Tier tier, std::vector<std::string_view> const& upgrades) {
    auto bp = game::Storage::get().create(tier);
    if (!bp) return nullptr;
    for (auto id : upgrades) {
        auto* type = byId(id);
        if (!type) continue;
        for (int s = 0; s < static_cast<int>(bp->data().upgrades.size()); ++s) {
            if (!bp->data().upgrades[static_cast<std::size_t>(s)].empty()) continue;
            (void)install(bp, s, game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1)), &player);
            break;
        }
    }
    return bp;
}

void setup(Player& player, CommandOutput& output) {
    auto& inv = player.getInventory();
    for (int i = 0; i < 36; ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());

    auto station = backpackWith(player, core::Tier::Diamond, {"anvil", "smithing"});
    auto infinite = backpackWith(player, core::Tier::Iron, {"survival_infinity"});
    if (!station || !infinite) {
        output.error("could not create the backpacks");
        return;
    }
    station->inventory().set(0, game::ItemConv::toData(game::ItemConv::make("minecraft:cobblestone", 20)));
    core::AccessContext internal{core::Source::Internal};
    for (auto id : {"minecraft:diamond", "minecraft:golden_apple", "minecraft:ender_pearl", "minecraft:oak_log"}) {
        (void)infinite->inventory().insert(game::ItemConv::toData(game::ItemConv::make(id, 1)), internal);
    }
    game::Storage::get().save(*station, true);
    game::Storage::get().save(*infinite, true);
    inv.setItem(0, game::makeBackpackItem(core::Tier::Diamond, station->uuid()));
    inv.setItem(1, game::makeBackpackItem(core::Tier::Iron, infinite->uuid()));

    auto sword = game::ItemConv::make("minecraft:diamond_sword", 1);
    sword.setDamageValue(1000);
    inv.setItem(9, sword);
    inv.setItem(10, game::ItemConv::make("minecraft:diamond", 5));
    inv.setItem(11, game::ItemConv::make("minecraft:netherite_upgrade_smithing_template", 1));
    inv.setItem(12, game::ItemConv::make("minecraft:netherite_ingot", 1));
    inv.setItem(13, game::ItemConv::make("minecraft:diamond_chestplate", 1));
    player.addLevels(30 - playerLevel(player));
    player.sendInventory(false);
    log().info("station setup: station {} infinity {}", station->uuid(), infinite->uuid());
    output.success("station setup done");
}

void probe(Player& player, CommandOutput& output) {
    auto& inv = player.getInventory();
    for (int i = 0; i < 36; ++i) {
        auto const& s = inv.getItem(i);
        if (s.isNull()) continue;
        log().info("station probe: inv {} {} x{} name='{}'", i, s.getTypeName(), static_cast<int>(s.mCount), s.getCustomName());
    }
    for (auto const& c : game::scanBackpacks(player)) {
        if (!c.uuid) continue;
        auto bp = game::Storage::get().find(*c.uuid);
        if (!bp) continue;
        for (int i = 0; i < bp->inventory().size(); ++i) {
            auto const& d = bp->inventory().at(i);
            if (d.empty()) continue;
            auto s = game::ItemConv::toStack(d, 1);
            log().info("station probe: backpack {} slot {} {} x{} name='{}'", *c.uuid, i, d.id, d.count, s.getCustomName());
        }
    }
    log().info("station probe: level {}", playerLevel(player));
    output.success("station probe done");
}

} // namespace
} // namespace abp::upgrade::anvil

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("station").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (auto* p = abp::upgrade::anvil::playerOf(origin, output)) abp::upgrade::anvil::setup(*p, output);
    });
    cmd.overload().text("station").text("probe").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (auto* p = abp::upgrade::anvil::playerOf(origin, output)) abp::upgrade::anvil::probe(*p, output);
    });
});
