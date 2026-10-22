// `/backpack stations setup` (client e2e scenario `stations`): two backpacks with the station
// upgrades installed and ingredients in the player's inventory. Plus a diagnostic selftest that
// lists crafting grids matching several recipes (the result cycler).

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/cooking/Cooking.h"
#include "upgrade/crafting/RecipeLookup.h"
#include "upgrade/crafting/Station.h"

namespace abp::upgrade::crafting {
namespace {

std::shared_ptr<game::Backpack> stationBackpack(Player& player, core::Tier tier, std::initializer_list<char const*> upgrades) {
    auto bp = game::Storage::get().create(tier);
    if (!bp) return nullptr;
    int slot = 0;
    for (auto const* id : upgrades) {
        auto* type = upgrade::byId(id);
        if (!type) continue;
        auto item = game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1));
        (void)upgrade::install(bp, slot++, item, &player);
    }
    return bp;
}

void put(game::Backpack& bp, std::string_view id, int count) {
    auto d = game::ItemConv::toData(game::ItemConv::make(id, 1));
    (void)bp.inventory().insert(d.withCount(count), core::AccessContext{core::Source::Internal});
}

void giveItem(Player& player, int slot, std::string_view id, int count) {
    auto s = game::ItemConv::make(id, count);
    s.serverInitNetId();
    player.getInventory().setItem(slot, s);
}

} // namespace

// Diagnostic: single / double item grids that match several distinct recipes on this game version.
ABP_REGISTER_SELFTEST("crafting.multi_result_scan", [](selftest::Context& ctx) {
    static constexpr std::string_view kCandidates[] = {
        "minecraft:bone", "minecraft:lapis_lazuli", "minecraft:cocoa_beans", "minecraft:ink_sac", "minecraft:wither_rose",
        "minecraft:lily_of_the_valley", "minecraft:cornflower", "minecraft:poppy", "minecraft:dandelion", "minecraft:beetroot",
        "minecraft:honeycomb", "minecraft:blue_orchid", "minecraft:sunflower", "minecraft:copper_block", "minecraft:iron_block",
        "minecraft:bamboo", "minecraft:oak_log", "minecraft:melon_slice", "minecraft:pumpkin", "minecraft:glow_ink_sac",
        "minecraft:bone_meal", "minecraft:red_mushroom", "minecraft:white_wool", "minecraft:snow", "minecraft:clay",
    };
    int found = 0;
    for (auto id : kCandidates) {
        Grid grid;
        grid[4]      = game::ItemConv::make(id, 1);
        auto matches = matchAllCrafting(grid);
        if (matches.size() > 1) {
            std::string list;
            for (auto const& m : matches) list += " " + m.result.getTypeName() + "x" + std::to_string(m.result.mCount);
            ctx.info(std::string(id) + " ->" + list);
            ++found;
        }
    }
    ctx.info("grids with several results: " + std::to_string(found));
});

} // namespace abp::upgrade::crafting

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("stations").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        using namespace abp;
        using namespace abp::upgrade::crafting;
        if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
            output.error("You need operator permission.");
            return;
        }
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer() || !game::Storage::get().available()) {
            output.error("players only");
            return;
        }
        auto& player = static_cast<Player&>(*e);
        auto  a      = stationBackpack(player, core::Tier::Diamond, {"crafting", "stonecutter", "smelting"});
        auto  b      = stationBackpack(player, core::Tier::Gold, {"auto_smelting", "tank"});
        if (!a || !b) {
            output.error("could not create the backpacks");
            return;
        }
        put(*a, "minecraft:oak_log", 16);
        put(*a, "minecraft:stone", 64);
        put(*a, "minecraft:cobblestone", 32);
        put(*b, "minecraft:raw_iron", 32);
        put(*b, "minecraft:coal", 8);
        put(*b, "minecraft:oak_log", 8);
        // The Auto-smelting upgrade cooks raw iron (its input allow list names it).
        upgrade::UpgradeContext ctx(b, 0, &player);
        auto&                   cooking = upgrade::cooking::upgradeOf(upgrade::cooking::Kind::Smelting, true);
        auto                    filter  = cooking.inputFilter(ctx);
        filter.setGhost(0, game::ItemConv::toData(game::ItemConv::make("minecraft:raw_iron", 1)));
        upgrade::cooking::CookingUpgrade::setInputFilter(ctx, filter);
        game::Storage::get().save(*a, true);
        game::Storage::get().save(*b, true);
        auto& inv = player.getInventory();
        for (int i = 0; i < 36; ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
        auto ia = game::makeBackpackItem(core::Tier::Diamond, a->uuid());
        auto ib = game::makeBackpackItem(core::Tier::Gold, b->uuid());
        ia.serverInitNetId();
        ib.serverInitNetId();
        inv.setItem(0, ia);
        inv.setItem(1, ib);
        giveItem(player, 9, "minecraft:oak_planks", 32);
        giveItem(player, 10, "minecraft:cobbled_deepslate", 32);
        giveItem(player, 11, "minecraft:raw_iron", 8);
        giveItem(player, 12, "minecraft:coal", 8);
        giveItem(player, 13, "minecraft:stone", 16);
        player.sendInventory(false);
        log().info("stations setup: gave backpacks {} and {}", a->uuid(), b->uuid());
        output.success("stations setup done");
    });
});
