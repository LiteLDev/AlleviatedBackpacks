// Selftests of the Pump / Advanced Pump upgrades: pump.*
// Run with `/backpack selftest "pump.*"`.

#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/states/BlockStateVariant.h"

#include "feature/placed/TestSite.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/pump/Pump.h"
#include "upgrade/tank/Tank.h"

namespace abp::upgrade::pump {
namespace {

using selftest::Context;
namespace fl = core::fluid;

ItemStack const& held(Player& p) { return game::itemAt(p, game::selectedSlot(p)); }
void             setHeld(Player& p, std::string_view id) {
    game::setItemAt(p, game::selectedSlot(p), id.empty() ? ItemStack{} : game::ItemConv::make(id));
}

ABP_REGISTER_SELFTEST("pump.defaults", [](Context& ctx) {
    auto bp = ctx.createBackpack(core::Tier::Gold);
    auto basic = Settings::of(UpgradeContext(bp, ctx.installUpgrade(bp, "pump")));
    ctx.check(basic.input && !basic.hand && !basic.world && basic.handlers && !basic.advanced, "basic: input, hand off, world off, handlers on");
    auto adv = Settings::of(UpgradeContext(bp, ctx.installUpgrade(bp, "advanced_pump")));
    ctx.check(adv.input && adv.hand && !adv.world && adv.handlers && adv.advanced, "advanced: input, hand on, world off, handlers on");
    ctx.check(adv.filter.size() == 4, "4 filter slots");
    // Pumps have no count limit in SB.
    std::string reason;
    ctx.check(upgrade::canInstall(bp, 2, game::ItemConv::toData(game::ItemConv::make("alleviatedbackpacks:pump_upgrade")), nullptr, &reason), "a second pump is allowed: " + reason);
});

ABP_REGISTER_SELFTEST("pump.hand", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, "tank");
    UpgradeContext pump(bp, ctx.installUpgrade(bp, "advanced_pump"));
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 9) == 9, "carry the backpack");
    setHeld(player, "minecraft:water_bucket");
    co_await ctx.ticks(30);
    ctx.check(game::ItemConv::idOf(held(player)) == "minecraft:bucket", "the held water bucket was pumped");
    ctx.check(tank::amountOf(bp, "water") == 1000, "1000 mB water in the tank");
    auto s  = Settings::of(pump);
    s.input = false;
    s.save(pump);
    co_await ctx.ticks(30);
    ctx.check(game::ItemConv::idOf(held(player)) == "minecraft:water_bucket", "output: the held bucket is filled");
    ctx.check(tank::amountOf(bp, "water") == 0, "tank empty");
    s.hand = false;
    s.save(pump);
    setHeld(player, "minecraft:bucket");
    (void)tank::fill(bp, {"water", 1000});
    co_await ctx.ticks(30);
    ctx.check(game::ItemConv::idOf(held(player)) == "minecraft:bucket", "hand interaction off: untouched");
    setHeld(player, "");
});

ABP_REGISTER_SELFTEST("pump.filter", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Gold);
    ctx.installUpgrade(bp, "tank");
    UpgradeContext pump(bp, ctx.installUpgrade(bp, "advanced_pump"), &player);
    // The tab's filter slot takes the fluid of the container on the cursor.
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make("minecraft:lava_bucket"), false);
    for (auto const& h : net::TabActionRegistry::entries()) {
        if (h.upgrade == "pump" && h.handle) (void)h.handle(player, pump, nlohmann::json{{"op", "filter"}, {"index", 1}});
    }
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack{}, false);
    auto s = Settings::of(pump);
    ctx.check(s.filter[1] == "lava", "filter slot 1 = lava, got '" + s.filter[1] + "'");
    ctx.check(s.matches({"minecraft:lava", 1}) && !s.matches({"minecraft:water", 1}), "only lava matches");
    setHeld(player, "minecraft:water_bucket");
    auto& region = player.getDimensionBlockSource();
    auto  feet   = player.getFeetBlockPos();
    (void)pumpTick(pump, &player, nullptr, region, feet);
    ctx.check(game::ItemConv::idOf(held(player)) == "minecraft:water_bucket", "water refused by the filter");
    setHeld(player, "minecraft:lava_bucket");
    (void)pumpTick(pump, &player, nullptr, region, feet);
    ctx.check(tank::amountOf(bp, "lava") == 1000, "lava pumped");
    setHeld(player, "");
    // An empty cursor clears the slot; toggles go through the tab action too.
    for (auto const& h : net::TabActionRegistry::entries()) {
        if (h.upgrade == "pump" && h.handle) {
            (void)h.handle(player, pump, nlohmann::json{{"op", "filter"}, {"index", 1}});
            (void)h.handle(player, pump, nlohmann::json{{"op", "toggle"}, {"what", "world"}});
        }
    }
    s = Settings::of(pump);
    ctx.check(s.filter[1].empty() && s.world, "filter cleared, world interaction on");
});

ABP_REGISTER_SELFTEST("pump.world", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    placed::test::Site site(sp);
    auto&              region = site.region();
    auto               bp     = ctx.createBackpack(core::Tier::Gold);
    ctx.installUpgrade(bp, "tank");
    UpgradeContext pump(bp, ctx.installUpgrade(bp, "advanced_pump"), &sp);
    auto           s = Settings::of(pump);
    s.world          = true;
    s.hand           = false;
    s.handlers       = false;
    s.save(pump);
    // Input: a water source 2 blocks away from the pump position is pumped (cooldown 2 x 20).
    auto base = site.at(0, 0);
    auto src  = site.at(2, 0);
    site.set(site.at(2, -1), "minecraft:stone");
    ctx.require(site.set(src, "minecraft:water"), "place a water source");
    int cd = pumpTick(pump, &sp, nullptr, region, base);
    ctx.check(tank::amountOf(bp, "water") == 1000, "the source was pumped");
    ctx.check(region.getBlock(src).isAir(), "the source is gone");
    ctx.check(cd == 40, "cooldown = distance x 20 = 40, got " + std::to_string(cd));
    // Output: places the water next to the pump position (not above).
    s.input = false;
    s.save(pump);
    site.touch(site.at(0, 0, -1));
    site.touch(site.at(0, -1));
    site.touch(site.at(1, 0));
    site.touch(site.at(-1, 0));
    site.touch(site.at(0, 0, 1));
    cd = pumpTick(pump, &sp, nullptr, region, base);
    ctx.check(cd == 20, "output cooldown 20, got " + std::to_string(cd));
    ctx.check(tank::amountOf(bp, "water") == 0, "the tank was emptied into the world");
    ctx.check(region.getBlock(site.at(0, 1)).isAir(), "nothing placed above");
});

ABP_REGISTER_SELFTEST("pump.handlers", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    placed::test::Site site(sp);
    auto&              region = site.region();
    // Two placed backpacks side by side: A pumps from B's tank.
    auto a = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(a, "tank");
    UpgradeContext pump(a, ctx.installUpgrade(a, "pump"));
    auto b = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(b, "tank");
    (void)tank::fill(b, {"water", 5000});
    auto posA = site.at(0, 1), posB = site.at(1, 1);
    site.set(site.at(0, 0), "minecraft:stone");
    site.set(site.at(1, 0), "minecraft:stone");
    site.touch(posA);
    site.touch(posB);
    auto itemA = ctx.backpackItem(*a);
    auto itemB = ctx.backpackItem(*b);
    using PR   = placed::PlacedBackpacks::PlaceResult;
    ctx.require(placed::PlacedBackpacks::get().place(region, posA, itemA, 2) == PR::Ok, "place A");
    ctx.require(placed::PlacedBackpacks::get().place(region, posB, itemB, 2) == PR::Ok, "place B");
    int cd = pumpTick(pump, nullptr, nullptr, region, posA);
    ctx.check(cd == 20, "handler cooldown 20, got " + std::to_string(cd));
    ctx.check(tank::amountOf(a, "water") == 1000 && tank::amountOf(b, "water") == 4000, "a bucket (max in/out of 6 rows) moved from B to A");
    // Output mode pushes it back.
    auto s  = Settings::of(pump);
    s.input = false;
    s.save(pump);
    (void)pumpTick(pump, nullptr, nullptr, region, posA);
    ctx.check(tank::amountOf(a, "water") == 0 && tank::amountOf(b, "water") == 5000, "pushed back");
    // Nothing to do: 40 ticks.
    ctx.check(pumpTick(pump, nullptr, nullptr, region, posA) == 40, "idle cooldown 40");
});

} // namespace
} // namespace abp::upgrade::pump
