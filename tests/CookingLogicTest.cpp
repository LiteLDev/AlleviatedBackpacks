#include <gtest/gtest.h>

#include "upgrade/cooking/CookingLogic.h"

using namespace abp::core;
using namespace abp::upgrade::cooking;

namespace {
ItemStackData item(char const* id, int n, int max = 64) { return ItemStackData::of(id, n, max); }

/// raw_iron -> iron_ingot (0.7 xp), oak_log -> charcoal (0.15 xp), wet sponge -> sponge;
/// coal burns 1600, oak_log 300, lava bucket 20000 (furnace values, times `burnModifier`).
CookEnv env(int recipeTime = 200, double burnModifier = 1.0) {
    CookEnv e;
    e.result = [](ItemStackData const& in) -> std::optional<ItemStackData> {
        if (in.id == "raw_iron") return item("iron_ingot", 1);
        if (in.id == "oak_log") return item("charcoal", 1);
        if (in.id == "minecraft:wet_sponge") return item("minecraft:sponge", 1);
        return std::nullopt;
    };
    e.burnTicks = [burnModifier](ItemStackData const& f) {
        int base = f.id == "coal" ? 1600 : f.id == "oak_log" ? 300 : f.id == "lava_bucket" ? 20000 : 0;
        return static_cast<int>(base * burnModifier);
    };
    e.afterBurn = [](ItemStackData const& f) {
        if (f.id == "lava_bucket") return ItemStackData::of("minecraft:bucket", 1, 16);
        return f.withCount(f.count - 1);
    };
    e.experience = [](ItemStackData const& r) { return r.id == "iron_ingot" ? 0.7f : 0.15f; };
    e.recipeTime = recipeTime;
    return e;
}
} // namespace

TEST(CookingLogic, CoalSmeltsEightItems) {
    CookState s;
    s.input = item("raw_iron", 10);
    s.fuel  = item("coal", 1);
    auto e  = env();
    runManual(s, e, 1601);
    EXPECT_EQ(s.output.id, "iron_ingot");
    EXPECT_EQ(s.output.count, 8);
    EXPECT_EQ(s.input.count, 2);
    EXPECT_TRUE(s.fuel.empty());
    runManual(s, e, 400);
    EXPECT_EQ(s.output.count, 8) << "no fuel left";
    EXPECT_FALSE(s.burning());
    EXPECT_FALSE(s.cooking) << "progress cools down without fuel";
    EXPECT_NEAR(s.storedXp, 8 * 0.7f, 1e-4);
}

TEST(CookingLogic, SmokerAndBlastFurnaceBurnFuelTwiceAsFast) {
    // SC: recipe time 100, burn time modifier 0.5 -> still 8 items per coal, in half the time.
    CookState s;
    s.input = item("raw_iron", 10);
    s.fuel  = item("coal", 1);
    auto e  = env(100, 0.5);
    runManual(s, e, 801);
    EXPECT_EQ(s.output.count, 8);
    runManual(s, e, 200);
    EXPECT_EQ(s.output.count, 8);
    EXPECT_FALSE(s.burning());
}

TEST(CookingLogic, SpeedAndFuelMultipliers) {
    {
        // Twice as fast: an item every 100 ticks, fuel burns half as long -> still 8 per coal.
        CookState s;
        s.input                  = item("raw_iron", 20);
        s.fuel                   = item("coal", 1);
        auto e                   = env();
        e.cookingSpeedMultiplier = 2.0;
        EXPECT_EQ(cookTime(e), 100);
        EXPECT_EQ(burnTime(e, s.fuel), 800);
        runManual(s, e, 801);
        EXPECT_EQ(s.output.count, 8);
    }
    {
        // Fuel efficiency 2: a coal lasts 3200 ticks -> 16 items.
        CookState s;
        s.input                    = item("raw_iron", 20);
        s.fuel                     = item("coal", 1);
        auto e                     = env();
        e.fuelEfficiencyMultiplier = 2.0;
        runManual(s, e, 3201);
        EXPECT_EQ(s.output.count, 16);
    }
}

TEST(CookingLogic, NoFuelWastedWithoutInput) {
    CookState s;
    s.fuel = item("coal", 3);
    auto e = env();
    runManual(s, e, 500);
    EXPECT_EQ(s.fuel.count, 3);
    EXPECT_FALSE(s.burning());
    s.input = item("dirt", 4); // no recipe
    runManual(s, e, 500);
    EXPECT_EQ(s.fuel.count, 3);
    EXPECT_FALSE(s.cooking);
}

TEST(CookingLogic, StopsWhenOutputFull) {
    CookState s;
    s.input  = item("raw_iron", 5);
    s.fuel   = item("coal", 2);
    s.output = item("iron_ingot", 63);
    auto e   = env();
    runManual(s, e, 1000);
    EXPECT_EQ(s.output.count, 64);
    EXPECT_EQ(s.input.count, 4);
    EXPECT_EQ(s.fuel.count, 1) << "a full output never lights new fuel";
}

TEST(CookingLogic, OutputOfAnotherItemBlocks) {
    CookState s;
    s.input  = item("raw_iron", 5);
    s.fuel   = item("coal", 2);
    s.output = item("charcoal", 1);
    runManual(s, env(), 500);
    EXPECT_EQ(s.input.count, 5);
    EXPECT_EQ(s.fuel.count, 2);
}

TEST(CookingLogic, LavaBucketLeavesBucket) {
    CookState s;
    s.input = item("raw_iron", 1);
    s.fuel  = item("lava_bucket", 1, 1);
    runManual(s, env(), 201);
    EXPECT_EQ(s.output.count, 1);
    EXPECT_EQ(s.fuel.id, "minecraft:bucket");
}

TEST(CookingLogic, WetSpongeFillsBucket) {
    CookState s;
    s.input = item("minecraft:wet_sponge", 1, 64);
    s.fuel  = item("lava_bucket", 1, 1);
    runManual(s, env(), 201);
    EXPECT_EQ(s.output.id, "minecraft:sponge");
    EXPECT_EQ(s.fuel.id, "minecraft:water_bucket") << "the bucket left by the lava catches the sponge's water";
}

TEST(CookingLogic, ProgressValues) {
    CookState s;
    s.input = item("raw_iron", 2);
    s.fuel  = item("coal", 1);
    auto e  = env();
    runManual(s, e, 1);
    EXPECT_TRUE(s.burning());
    EXPECT_TRUE(s.cooking);
    EXPECT_EQ(s.cookTotal, 200);
    EXPECT_EQ(s.burnTotal, 1600);
    runManual(s, e, 99);
    EXPECT_NEAR(s.cookProgress(), 0.5f, 0.01f);
    EXPECT_NEAR(s.burnProgress(), 100.f / 1600.f, 0.01f) << "SC: the elapsed fraction";
}

TEST(CookingLogic, ItemValidity) {
    CookState s;
    auto      e = env();
    EXPECT_TRUE(isItemValid(e, s, kInputSlot, item("raw_iron", 1)));
    EXPECT_FALSE(isItemValid(e, s, kInputSlot, item("dirt", 1)));
    EXPECT_TRUE(isItemValid(e, s, kFuelSlot, item("coal", 1)));
    EXPECT_FALSE(isItemValid(e, s, kFuelSlot, item("raw_iron", 1)));
    e.inputAllowed = [](ItemStackData const&) { return false; };
    EXPECT_FALSE(isItemValid(e, s, kInputSlot, item("raw_iron", 1))) << "Auto input filter";
    s.input = item("raw_iron", 1);
    EXPECT_TRUE(isItemValid(e, s, kInputSlot, item("raw_iron", 1))) << "same item always stacks";
}

TEST(CookingLogic, JsonRoundTrip) {
    CookState s;
    s.input    = item("raw_iron", 3);
    s.fuel     = item("coal", 2);
    s.output   = item("iron_ingot", 5);
    s.clock    = 1234;
    s.cooking  = true;
    s.storedXp = 2.5f;
    runManual(s, env(), 3);
    auto back = CookState::fromJson(s.toJson());
    EXPECT_TRUE(back.sameData(s));
    EXPECT_FALSE(CookState{}.burning()) << "a fresh state isn't burning";
}

TEST(CookingLogic, StopLosesBurningFuelAndProgress) {
    CookState s;
    s.input = item("raw_iron", 3);
    s.fuel  = item("coal", 2);
    runManual(s, env(), 50);
    ASSERT_TRUE(s.burning());
    s.stop();
    EXPECT_FALSE(s.burning());
    EXPECT_FALSE(s.cooking);
    EXPECT_EQ(s.fuel.count, 1);
    EXPECT_EQ(s.input.count, 3);
}

// ---- Auto variants ----------------------------------------------------------------------------

TEST(CookingLogic, AutoEmptyInputAllowListPullsNothing) {
    BackpackInventory inv(4);
    inv.set(0, item("raw_iron", 10));
    inv.set(1, item("coal", 10));
    CookState s;
    auto      e    = env();
    e.inputAllowed = [](ItemStackData const&) { return false; }; // SC: empty allow list matches nothing
    AutoEnv a{inv};
    runAuto(s, e, a, 300);
    EXPECT_TRUE(s.input.empty());
    EXPECT_EQ(s.fuel.count, 10) << "fuel (empty fuel filter = everything) is pulled as a full stack";
    EXPECT_EQ(inv.at(0).count, 10);
    EXPECT_FALSE(s.burning());
}

TEST(CookingLogic, AutoPullsFullStacksAndPushesOutput) {
    BackpackInventory inv(6);
    inv.set(0, item("raw_iron", 40));
    inv.set(1, item("raw_iron", 40));
    inv.set(2, item("coal", 3));
    CookState s;
    auto      e    = env();
    e.inputAllowed = [](ItemStackData const& x) { return x.id == "raw_iron"; };
    int       xpPushes = 0;
    AutoEnv   a{inv, AccessContext{Source::Upgrade}, [&](CookState&) { ++xpPushes; }};
    runAuto(s, e, a, 1);
    EXPECT_EQ(s.input.count, 64) << "SC pulls a full stack";
    EXPECT_EQ(s.fuel.count, 2) << "3 coal pulled, one burning";
    EXPECT_EQ(inv.count([](ItemStackData const& x) { return x.id == "raw_iron"; }), 16);
    runAuto(s, e, a, 1700);
    EXPECT_EQ(inv.count([](ItemStackData const& x) { return x.id == "iron_ingot"; }) + s.output.count, 8);
    EXPECT_GT(inv.count([](ItemStackData const& x) { return x.id == "iron_ingot"; }), 0) << "output pushed into the storage";
    EXPECT_GT(xpPushes, 0);
}

TEST(CookingLogic, AutoOutputCooldownWhenStorageFull) {
    BackpackInventory inv(1);
    inv.set(0, item("dirt", 64));
    CookState s;
    s.input  = item("raw_iron", 4);
    s.fuel   = item("coal", 1);
    auto e   = env();
    AutoEnv a{inv};
    runAuto(s, e, a, 900);
    EXPECT_EQ(s.output.count, 4) << "results stay in the output slot";
    EXPECT_EQ(inv.at(0).id, "dirt");
}

TEST(CookingLogic, AutoPushesSpentLavaBucket) {
    BackpackInventory inv(4);
    CookState         s;
    s.input = item("raw_iron", 1);
    s.fuel  = item("lava_bucket", 1, 1);
    AutoEnv a{inv};
    runAuto(s, env(), a, 400);
    EXPECT_EQ(inv.count([](ItemStackData const& x) { return x.id == "minecraft:bucket"; }), 1);
    EXPECT_TRUE(s.fuel.empty());
}

TEST(CookingLogic, AutoFuelFilter) {
    BackpackInventory inv(4);
    inv.set(0, item("oak_log", 8));
    inv.set(1, item("coal", 8));
    CookState s;
    auto      e   = env();
    e.fuelAllowed  = [](ItemStackData const& x) { return x.id == "coal"; };
    e.inputAllowed = [](ItemStackData const&) { return false; };
    AutoEnv a{inv};
    runAuto(s, e, a, 1);
    EXPECT_EQ(s.fuel.id, "coal");
    EXPECT_EQ(inv.at(0).count, 8);
}

TEST(CookingLogic, AutoUsesInceptionInventoryChain) {
    // GX-2: SC getInventoryForUpgradeProcessing with Inception: input and fuel come from the
    // backpacks inside too, output goes there when the main inventory is full.
    BackpackInventory main(1), inner(4);
    main.set(0, item("dirt", 64));
    inner.set(2, item("raw_iron", 2));
    inner.set(3, item("coal", 1));
    CookState s;
    auto      e = env();
    AutoEnv   a{main};
    a.chain = {&main, &inner};
    runAuto(s, e, a, 1);
    EXPECT_EQ(s.input.count, 2) << "input pulled from the inner backpack";
    EXPECT_TRUE(s.burning()) << "the coal from the inner backpack burns";
    runAuto(s, e, a, 900);
    EXPECT_EQ(inner.count([](ItemStackData const& x) { return x.id == "iron_ingot"; }), 2) << "output pushed into the inner backpack";
    EXPECT_EQ(main.at(0).id, "dirt");
}
