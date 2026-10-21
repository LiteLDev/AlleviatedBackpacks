// Selftests of the Hopper / Advanced Hopper upgrades: hopper.*
// Run with `/backpack selftest "hopper.*"`.

#include "mc/server/SimulatedPlayer.h"

#include "core/Filter.h"
#include "feature/placed/TestSite.h"
#include "selftest/SelfTest.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/hopper/HopperUpgrade.h"

namespace abp::upgrade::hopper {
namespace {

using core::Tier;
using placed::test::countIn;
using placed::test::Site;
using placed::test::toGround;
using selftest::Context;

core::AccessContext const kInternal{core::Source::Internal};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

void placeAt(Context& ctx, Site& site, game::Backpack const& bp, BlockPos const& pos) {
    site.touch(pos);
    auto item = ctx.backpackItem(bp);
    ctx.require(
        placed::PlacedBackpacks::get().place(site.region(), pos, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok,
        "place backpack"
    );
}

/// Runs `n` placed ticks, each long enough for one transfer.
void transfers(int n) {
    for (int i = 0; i < n; ++i) placed::PlacedBackpacks::get().tickPlaced(1000);
}

nlohmann::json filterOf(std::initializer_list<std::string_view> ids, core::FilterMode mode, int slots) {
    core::Filter f(slots);
    int          i = 0;
    for (auto id : ids) f.setGhost(i++, data(id, 1));
    f.mode = mode;
    return f.toJson();
}

ABP_REGISTER_SELFTEST("hopper.basic_pull_push_filters", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto below = site.at(0, 0);
    auto pos   = site.at(0, 1);
    auto above = site.at(0, 2);
    site.set(site.at(0, -1), "minecraft:stone");
    site.set(below, "minecraft:chest");
    site.set(pos, "minecraft:air");
    site.set(above, "minecraft:chest");
    auto* top    = placed::containerAt(site.region(), above);
    auto* bottom = placed::containerAt(site.region(), below);
    ctx.require(top && bottom, "chests");
    top->setItem(0, game::ItemConv::make("minecraft:iron_ingot", 4));
    top->setItem(1, game::ItemConv::make("minecraft:dirt", 4));

    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, "hopper");
    UpgradeContext uctx(bp, slot);
    uctx.settings()["input"]  = filterOf({"minecraft:iron_ingot"}, core::FilterMode::Allow, 2);
    uctx.settings()["output"] = filterOf({"minecraft:gold_ingot"}, core::FilterMode::Deny, 2);
    uctx.saveSettings();
    bp->inventory().insert(data("minecraft:gold_ingot", 3), kInternal);
    bp->inventory().insert(data("minecraft:emerald", 2), kInternal);
    placeAt(ctx, site, *bp, pos);

    transfers(1);
    ctx.check(countIn(*top, "minecraft:iron_ingot") == 3, "basic: one item per transfer");
    transfers(12);
    ctx.check(countIn(*top, "minecraft:iron_ingot") == 0, "iron pulled in (input filter allows it)");
    ctx.check(countIn(*top, "minecraft:dirt") == 4, "dirt stays (input filter)");
    ctx.check(countIn(*bottom, "minecraft:emerald") == 2, "emeralds pushed below");
    ctx.check(countIn(*bottom, "minecraft:iron_ingot") == 4, "iron passed through to the chest below");
    ctx.check(countIn(*bp, "minecraft:gold_ingot") == 3 && countIn(*bottom, "minecraft:gold_ingot") == 0, "gold kept (output deny filter)");

    // Disabled upgrade does nothing.
    top->setItem(0, game::ItemConv::make("minecraft:iron_ingot", 1));
    upgrade::setEnabled(bp, slot, false);
    transfers(3);
    ctx.check(countIn(*top, "minecraft:iron_ingot") == 1, "disabled upgrade is idle");
    upgrade::setEnabled(bp, slot, true);
});

ABP_REGISTER_SELFTEST("hopper.advanced_directions", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos   = site.at(0, 1);
    auto north = site.at(0, 1, -1);
    auto east  = site.at(1, 1);
    site.set(site.at(0, 0), "minecraft:stone");
    site.set(pos, "minecraft:air");
    site.set(north, "minecraft:chest");
    site.set(east, "minecraft:barrel");
    auto* src = placed::containerAt(site.region(), north);
    auto* dst = placed::containerAt(site.region(), east);
    ctx.require(src && dst, "containers");
    src->setItem(0, game::ItemConv::make("minecraft:redstone", 16));

    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, "advanced_hopper");
    auto* type = static_cast<HopperUpgradeBase*>(upgrade::byId("advanced_hopper"));
    UpgradeContext uctx(bp, slot);
    type->setDirection(uctx, "pull", 1, false); // not from above
    type->setDirection(uctx, "pull", 2, true);  // from north
    type->setDirection(uctx, "push", 0, false); // not down
    type->setDirection(uctx, "push", 5, true);  // to east
    auto pull = type->pullDirections(uctx);
    ctx.check(pull[2] && !pull[1], "pull directions stored");
    placeAt(ctx, site, *bp, pos);

    transfers(1);
    ctx.check(countIn(*src, "minecraft:redstone") == 12, "advanced: four items per transfer");
    transfers(8);
    ctx.check(countIn(*src, "minecraft:redstone") == 0, "pulled everything from the north");
    ctx.check(countIn(*dst, "minecraft:redstone") == 16, "pushed everything to the east");
    ctx.check(countIn(*bp, "minecraft:redstone") == 0, "nothing left behind");
});

ABP_REGISTER_SELFTEST("hopper.between_backpacks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto lower = site.at(0, 1);
    auto upper = site.at(0, 2);
    site.set(site.at(0, 0), "minecraft:stone");
    site.set(lower, "minecraft:air");
    site.set(upper, "minecraft:air");
    auto a = ctx.createBackpack(Tier::Leather); // upper, with the upgrade: pushes down
    auto b = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(a, "hopper");
    a->inventory().insert(data("minecraft:apple", 5), kInternal);
    placeAt(ctx, site, *b, lower);
    placeAt(ctx, site, *a, upper);
    transfers(6);
    ctx.check(countIn(*a, "minecraft:apple") == 0 && countIn(*b, "minecraft:apple") == 5, "moved into the placed backpack below");
});

ABP_REGISTER_SELFTEST("hopper.carried_is_idle", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    auto bp = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "hopper");
    bp->inventory().insert(data("minecraft:apple", 5), kInternal);
    ctx.require(ctx.give(sp, ctx.backpackItem(*bp), 0) == 0, "give");
    co_await ctx.ticks(30);
    ctx.check(countIn(*bp, "minecraft:apple") == 5, "a carried backpack's hopper upgrade does nothing (like the Java mod)");
});

ABP_REGISTER_SELFTEST("hopper.settings_menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    using Where  = ui::MenuManager::SlotAddress::Where;
    using Action = ui::MenuManager::ActionType;
    auto& mm = ui::MenuManager::get();
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    auto bp   = ctx.createBackpack(Tier::Iron);
    int  slot = ctx.installUpgrade(bp, "advanced_hopper");
    auto* type = upgrade::byId("advanced_hopper");
    UpgradeContext uctx(bp, slot, &sp);
    type->openSettings(sp, uctx);
    ctx.require(mm.current(sp) != nullptr, "settings menu open");
    mm.forceShown(sp);
    (void)mm.simulate(sp, {Action::Take, {Where::Menu, 7}, {Where::Cursor, 0}, 1}); // pull from east
    auto dirs = static_cast<HopperUpgradeBase*>(type)->pullDirections(uctx);
    ctx.check(dirs[5], "toggle turns on pulling from the east");
    co_await ctx.ticks(1);
    (void)mm.simulate(sp, {Action::Take, {Where::Menu, 0}, {Where::Cursor, 0}, 1}); // input filter
    co_await ctx.ticks(3);
    ctx.check(std::dynamic_pointer_cast<ui::FilterMenu>(mm.current(sp)) != nullptr, "input filter editor opens");
    auto lore = type->describe(sp, uctx);
    ctx.check(!lore.empty(), "describe lists directions");
    mm.close(sp);
});

} // namespace
} // namespace abp::upgrade::hopper
