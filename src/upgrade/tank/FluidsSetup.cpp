// `/backpack fluids setup`: a diamond backpack with two tanks, a battery, an advanced pump and an
// experience pump plus a few fluid containers (client e2e scenario `fluids`).

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "core/Fluid.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/battery/Battery.h"
#include "upgrade/tank/Tank.h"
#include "upgrade/xppump/XpPump.h"

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("fluids").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        using namespace abp;
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
        auto  bp     = game::Storage::get().create(core::Tier::Diamond);
        if (!bp) {
            output.error("could not create a backpack");
            return;
        }
        int slot = 0;
        for (auto const* id : {"tank", "tank", "battery", "advanced_pump", "xp_pump"}) {
            auto* type = upgrade::byId(id);
            if (!type) continue;
            auto item = game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1));
            (void)upgrade::install(bp, slot++, item, &player);
        }
        upgrade::tank::write(upgrade::UpgradeContext(bp, 0), core::fluid::Tank({"water", 9000}, upgrade::tank::capacity(*bp)));
        upgrade::tank::write(upgrade::UpgradeContext(bp, 1), core::fluid::Tank({"experience", 4000}, upgrade::tank::capacity(*bp)));
        upgrade::battery::setStored(upgrade::UpgradeContext(bp, 2), upgrade::battery::capacity(*bp) * 3 / 4);
        auto& inv = bp->inventory();
        static constexpr std::string_view kItems[] = {"minecraft:cobblestone", "minecraft:dirt", "minecraft:oak_log", "minecraft:sand"};
        for (int i = 0; i < inv.size() && i < 20; ++i) {
            inv.set(i, game::ItemConv::toData(game::ItemConv::make(kItems[static_cast<std::size_t>(i) % std::size(kItems)], 1 + i * 3)));
        }
        bp->markDirty();
        game::Storage::get().save(*bp, true);
        auto stack = game::makeBackpackItem(core::Tier::Diamond, bp->uuid());
        if (!player.add(stack)) player.drop(stack, false);
        for (auto const* id : {"minecraft:water_bucket", "minecraft:bucket", "minecraft:lava_bucket"}) {
            auto s = game::ItemConv::make(id, 1);
            if (!player.add(s)) player.drop(s, false);
        }
        auto bottles = game::ItemConv::make("minecraft:glass_bottle", 4);
        if (!player.add(bottles)) player.drop(bottles, false);
        upgrade::xppump::setPlayerXp(player, 550); // level 20
        player.sendInventory(false);
        abp::log().info("fluids setup: gave diamond backpack {} ({} slots, {} columns taken)", bp->uuid(), inv.size(), bp->data().columnsTaken);
        output.success("fluids setup: gave diamond backpack " + bp->uuid());
    });
});
