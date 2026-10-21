// Host-side tests of the capability API's pure parts: src/api/abp/{Energy,Fluid,Xp}.h,
// core/FluidRegistry and core/Capacity.

#include <gtest/gtest.h>

#include <climits>

#include "api/abp/Energy.h"
#include "api/abp/Fluid.h"
#include "api/abp/Xp.h"
#include "core/Capacity.h"
#include "core/Experience.h"
#include "core/FluidRegistry.h"

using namespace abp;
using api::FluidStack;

// ---- energy ---------------------------------------------------------------------------------------

TEST(ApiEnergy, ReceiveAndExtractRespectLimits) {
    api::EnergyStorage e(1000, 300, 200);
    EXPECT_EQ(e.receive(500, true), 300);
    EXPECT_EQ(e.stored(), 0);
    EXPECT_EQ(e.receive(500, false), 300);
    EXPECT_EQ(e.receive(500, false), 300);
    EXPECT_EQ(e.receive(500, false), 300);
    EXPECT_EQ(e.receive(500, false), 100); // capacity
    EXPECT_EQ(e.receive(500, false), 0);
    EXPECT_EQ(e.stored(), 1000);
    EXPECT_EQ(e.extract(1000, false), 200);
    EXPECT_EQ(e.extract(-5, false), 0);
    EXPECT_EQ(e.receive(0, false), 0);
    EXPECT_EQ(e.stored(), 800);
}

TEST(ApiEnergy, ReceiveOnlyAndClamping) {
    api::EnergyStorage e(100, 50, 0, 500);
    EXPECT_EQ(e.stored(), 100); // initial value clamped to capacity
    EXPECT_FALSE(e.canExtract());
    EXPECT_TRUE(e.canReceive());
    EXPECT_EQ(e.extract(10, false), 0);
    e.setStored(-3);
    EXPECT_EQ(e.stored(), 0);
    api::EnergyStorage none(-5);
    EXPECT_EQ(none.capacity(), 0);
    EXPECT_EQ(none.receive(10, false), 0);
}

namespace {
struct Counting : api::EnergyStorage {
    using EnergyStorage::EnergyStorage;
    int  changes = 0;
    void onChanged() override { ++changes; }
};
} // namespace

TEST(ApiEnergy, OnChangedOnlyForRealChanges) {
    Counting e(100);
    e.receive(10, true);
    e.receive(0, false);
    EXPECT_EQ(e.changes, 0);
    e.receive(10, false);
    e.extract(5, false);
    EXPECT_EQ(e.changes, 2);
}

TEST(ApiEnergy, Transfer) {
    api::EnergyStorage from(1000, 1000, 70, 1000);
    api::EnergyStorage to(500, 40, 1000);
    EXPECT_EQ(api::transferEnergy(from, to, 1000, true), 40);
    EXPECT_EQ(from.stored(), 1000);
    EXPECT_EQ(api::transferEnergy(from, to, 1000), 40);
    EXPECT_EQ(from.stored(), 960);
    EXPECT_EQ(to.stored(), 40);
    EXPECT_EQ(api::transferEnergy(from, from, 10), 0);
    EXPECT_EQ(api::transferEnergy(from, to, 0), 0);
}

// ---- fluid stacks / tanks -------------------------------------------------------------------------

TEST(ApiFluid, StackIdentity) {
    FluidStack a{"minecraft:potion", 250, "{\"potion\":7}"};
    FluidStack b{"minecraft:potion", 1000, "{\"potion\":7}"};
    FluidStack c{"minecraft:potion", 250, "{\"potion\":8}"};
    EXPECT_TRUE(a.isSameFluid(b));
    EXPECT_FALSE(a.isSameFluid(c));
    EXPECT_FALSE(a.isSameFluid(FluidStack{}));
    EXPECT_TRUE(FluidStack("minecraft:water", 0).empty());
    EXPECT_TRUE(a.copyWithAmount(0).empty());
    EXPECT_EQ(a.copyWithAmount(5), (FluidStack{"minecraft:potion", 5, "{\"potion\":7}"}));
    auto d = a;
    d.shrink(250);
    EXPECT_TRUE(d.empty());
    EXPECT_TRUE(d.id.empty());
    EXPECT_EQ(api::intNbt("potion", 7), "{\"potion\":7}");
}

TEST(ApiFluid, Merge) {
    FluidStack into;
    EXPECT_EQ(api::mergeFluid(into, {"minecraft:water", 700}, 1000), 700);
    EXPECT_EQ(into, (FluidStack{"minecraft:water", 700}));
    EXPECT_EQ(api::mergeFluid(into, {"minecraft:water", 700}, 1000), 300);
    EXPECT_EQ(into.amount, 1000);
    EXPECT_EQ(api::mergeFluid(into, {"minecraft:lava", 1}, 5000), 0);
    EXPECT_EQ(api::mergeFluid(into, {}, 5000), 0);
    FluidStack potion{"minecraft:potion", 10, "{\"potion\":1}"};
    EXPECT_EQ(api::mergeFluid(potion, {"minecraft:potion", 10, "{\"potion\":2}"}, 100), 0);
    EXPECT_EQ(api::mergeFluid(potion, {"minecraft:potion", 10, "{\"potion\":1}"}, 100), 10);
}

TEST(ApiFluid, Tank) {
    api::FluidTank t(1000, [](FluidStack const& s) { return s.id != "minecraft:lava"; });
    EXPECT_EQ(t.fill({"minecraft:lava", 100}, false), 0);
    EXPECT_EQ(t.fill({"minecraft:water", 600}, true), 600);
    EXPECT_TRUE(t.fluid().empty());
    EXPECT_EQ(t.fill({"minecraft:water", 600}, false), 600);
    EXPECT_EQ(t.fill({"minecraft:water", 600}, false), 400);
    EXPECT_EQ(t.fill({"minecraft:milk", 1}, false), 0);
    EXPECT_TRUE(t.drain({"minecraft:milk", 100}, false).empty());
    EXPECT_EQ(t.drain({"minecraft:water", 300}, false), (FluidStack{"minecraft:water", 300}));
    EXPECT_EQ(t.drainAny(5000, false), (FluidStack{"minecraft:water", 700}));
    EXPECT_TRUE(t.fluid().empty());
    EXPECT_EQ(t.fluidInTank(1), FluidStack{});
    EXPECT_EQ(t.tankCapacity(0), 1000);
}

TEST(ApiFluid, CombinedUsesSbRules) {
    auto a = std::make_shared<api::FluidTank>(1000);
    auto b = std::make_shared<api::FluidTank>(1000);
    api::CombinedFluidHandler h({a, nullptr, b});
    EXPECT_EQ(h.tanks(), 2);
    EXPECT_EQ(h.tankCapacity(1), 1000);
    EXPECT_EQ(h.tankCapacity(2), 0);
    // b holds water; filling water goes to a first (slot order), like SB.
    b->setFluid({"minecraft:water", 500});
    EXPECT_EQ(h.fill({"minecraft:water", 1200}, false), 1200);
    EXPECT_EQ(a->amount(), 1000);
    EXPECT_EQ(b->amount(), 700);
    EXPECT_EQ(h.fill({"minecraft:lava", 10}, false), 0);
    // drain(resource) collects from all tanks.
    EXPECT_EQ(h.drain({"minecraft:water", 1500}, false), (FluidStack{"minecraft:water", 1500}));
    EXPECT_EQ(a->amount(), 0);
    EXPECT_EQ(b->amount(), 200);
    // drainAny takes from the first handler that gives anything (no merging).
    a->setFluid({"minecraft:water", 100});
    EXPECT_EQ(h.drainAny(1000, false), (FluidStack{"minecraft:water", 100}));
    EXPECT_EQ(h.drainAny(1000, false), (FluidStack{"minecraft:water", 200}));
    EXPECT_TRUE(h.drainAny(1000, false).empty());
    EXPECT_EQ(h.fluidInTank(-1), FluidStack{});
}

// ---- registry: containers -------------------------------------------------------------------------

TEST(ApiFluidRegistry, VanillaContainers) {
    auto r = core::FluidRegistry::withBuiltins();
    using C = core::FluidRegistry::Contents;
    EXPECT_EQ(r.contentsOf("minecraft:water_bucket", 0), (C{{"minecraft:water", 1000}, "minecraft:bucket"}));
    EXPECT_EQ(r.contentsOf("minecraft:lava_bucket", 0)->fluid.id, "minecraft:lava");
    EXPECT_EQ(r.contentsOf("minecraft:milk_bucket", 0)->fluid.id, "minecraft:milk");
    EXPECT_EQ(r.contentsOf("minecraft:powder_snow_bucket", 0)->fluid.id, "minecraft:powder_snow");
    EXPECT_EQ(r.contentsOf("minecraft:potion", 0), (C{{"minecraft:water", 250}, "minecraft:glass_bottle"}));
    EXPECT_EQ(r.contentsOf("minecraft:potion", 7), (C{{"minecraft:potion", 250, "{\"potion\":7}"}, "minecraft:glass_bottle"}));
    EXPECT_EQ(r.contentsOf("minecraft:splash_potion", 0)->fluid, (FluidStack{"minecraft:splash_potion", 250, "{\"potion\":0}"}));
    EXPECT_EQ(r.contentsOf("minecraft:lingering_potion", 21)->fluid.nbt, "{\"potion\":21}");
    EXPECT_EQ(r.contentsOf("minecraft:honey_bottle", 0)->fluid, (FluidStack{"minecraft:honey", 250}));
    EXPECT_EQ(r.contentsOf("minecraft:experience_bottle", 0), (C{{"abp:experience", 160}, "minecraft:glass_bottle"}));
    EXPECT_FALSE(r.contentsOf("minecraft:bucket", 0));
    EXPECT_FALSE(r.contentsOf("minecraft:cod_bucket", 0));
    EXPECT_FALSE(r.contentsOf("minecraft:water_bucket", -1));
}

TEST(ApiFluidRegistry, FillContainers) {
    auto r = core::FluidRegistry::withBuiltins();
    using F = core::FluidRegistry::Filled;
    EXPECT_EQ(r.fill("minecraft:bucket", {"minecraft:water", 1}), (F{"minecraft:water_bucket", 0, 1000}));
    EXPECT_EQ(r.fill("minecraft:bucket", {"minecraft:powder_snow", 1}), (F{"minecraft:powder_snow_bucket", 0, 1000}));
    EXPECT_EQ(r.fill("minecraft:glass_bottle", {"minecraft:water", 1}), (F{"minecraft:potion", 0, 250}));
    EXPECT_EQ(r.fill("minecraft:glass_bottle", {"abp:experience", 1}), (F{"minecraft:experience_bottle", 0, 160}));
    EXPECT_EQ(r.fill("minecraft:glass_bottle", {"minecraft:potion", 1, "{\"potion\":7}"}), (F{"minecraft:potion", 7, 250}));
    EXPECT_EQ(r.fill("minecraft:glass_bottle", {"minecraft:splash_potion", 1, "{\"potion\":0}"}), (F{"minecraft:splash_potion", 0, 250}));
    // potion aux 0 is the water bottle, never a "potion #0".
    EXPECT_FALSE(r.fill("minecraft:glass_bottle", {"minecraft:potion", 1, "{\"potion\":0}"}));
    EXPECT_FALSE(r.fill("minecraft:glass_bottle", {"minecraft:potion", 1}));
    EXPECT_FALSE(r.fill("minecraft:glass_bottle", {"minecraft:potion", 1, "{\"potion\":7,\"x\":1}"}));
    EXPECT_FALSE(r.fill("minecraft:bucket", {"minecraft:honey", 1}));
    EXPECT_FALSE(r.fill("minecraft:bucket", {"abp:experience", 1}));
    EXPECT_FALSE(r.fill("minecraft:stone", {"minecraft:water", 1}));
    EXPECT_TRUE(r.isEmptyContainer("minecraft:bucket"));
    EXPECT_TRUE(r.isEmptyContainer("minecraft:glass_bottle"));
    EXPECT_FALSE(r.isEmptyContainer("minecraft:water_bucket"));
}

TEST(ApiFluidRegistry, ModsExtendIt) {
    auto r = core::FluidRegistry::withBuiltins();
    EXPECT_FALSE(r.addFluid({"minecraft:water"}));     // duplicate
    EXPECT_FALSE(r.addFluid({"oil"}));                 // no namespace
    EXPECT_TRUE(r.addFluid({"mymod:oil", "mymod.oil", "Oil", 0xFF000000, {"c:oil"}}));
    EXPECT_TRUE(r.hasTag("mymod:oil", "c:oil"));
    EXPECT_TRUE(r.hasTag("abp:experience", "c:experience"));
    EXPECT_FALSE(r.hasTag("minecraft:water", "c:experience"));
    EXPECT_FALSE(r.addContainer({"mymod:oil_can", 0, "", {"mymod:oil", 500}}));          // no empty item
    EXPECT_FALSE(r.addContainer({"mymod:oil_can", 0, "mymod:can", {"mymod:oil", 0}}));   // no amount
    EXPECT_TRUE(r.addContainer({"mymod:oil_can", 0, "mymod:can", {"mymod:oil", 500}}));
    EXPECT_FALSE(r.addContainer({"mymod:oil_can", 0, "mymod:can", {"mymod:oil", 400}})); // same filled item + aux
    EXPECT_EQ(r.contentsOf("mymod:oil_can", 0)->fluid, (FluidStack{"mymod:oil", 500}));
    EXPECT_EQ(r.fill("mymod:can", {"mymod:oil", 1})->item, "mymod:oil_can");
    EXPECT_TRUE(r.removeContainer("mymod:oil_can", 0));
    EXPECT_FALSE(r.contentsOf("mymod:oil_can", 0));
    EXPECT_TRUE(r.removeFluid("mymod:oil"));
    EXPECT_EQ(r.fluid("mymod:oil"), nullptr);
}

TEST(ApiFluidRegistry, Nbt) {
    EXPECT_EQ(core::singleIntNbt("{\"potion\":7}", "potion"), 7);
    EXPECT_FALSE(core::singleIntNbt("{\"potion\":\"7\"}", "potion"));
    EXPECT_FALSE(core::singleIntNbt("{\"a\":1,\"potion\":7}", "potion"));
    EXPECT_FALSE(core::singleIntNbt("not json", "potion"));
    EXPECT_FALSE(core::singleIntNbt("", "potion"));
}

// ---- phase-1 id mapping ---------------------------------------------------------------------------

TEST(ApiFluidRegistry, LegacyIdsRoundTrip) {
    EXPECT_EQ(core::toApi({"water", 1000}), (FluidStack{"minecraft:water", 1000}));
    EXPECT_EQ(core::toApi({"experience", 20}), (FluidStack{"abp:experience", 20}));
    EXPECT_EQ(core::toApi({"potion:7", 250}), (FluidStack{"minecraft:potion", 250, "{\"potion\":7}"}));
    EXPECT_EQ(core::toApi({"splash_potion:0", 250}), (FluidStack{"minecraft:splash_potion", 250, "{\"potion\":0}"}));
    EXPECT_EQ(core::toApi({"mymod:oil", 5}), (FluidStack{"mymod:oil", 5}));
    EXPECT_EQ(core::toApi({"mymod:oil{\"a\":1}", 5}), (FluidStack{"mymod:oil", 5, "{\"a\":1}"}));
    EXPECT_TRUE(core::toApi({"water", 0}).empty());
    for (auto const* id : {"water", "lava", "milk", "powder_snow", "honey", "experience", "potion:7", "splash_potion:0", "lingering_potion:21", "mymod:oil", "mymod:oil{\"a\":1}"}) {
        core::fluid::FluidStack s{id, 123};
        EXPECT_EQ(core::fromApi(core::toApi(s)), s) << id;
    }
    // API stacks without a phase-1 id keep their id (and nbt).
    EXPECT_EQ(core::legacyIdOf({"minecraft:potion", 1, "{\"potion\":0}"}), "minecraft:potion{\"potion\":0}");
    EXPECT_EQ(core::legacyIdOf({"minecraft:water", 1, "{\"x\":1}"}), "minecraft:water{\"x\":1}");
    EXPECT_EQ(core::legacyIdOf({"minecraft:lava", 1}), "lava");
}

// ---- XP -------------------------------------------------------------------------------------------

TEST(ApiXp, MatchesSophisticatedCore) {
    namespace xp = api::xp;
    EXPECT_EQ(xp::experienceForLevel(0), 0);
    EXPECT_EQ(xp::experienceForLevel(1), 7);
    EXPECT_EQ(xp::experienceForLevel(15), 315);
    EXPECT_EQ(xp::experienceForLevel(16), 352);
    EXPECT_EQ(xp::experienceForLevel(30), 1395);
    EXPECT_EQ(xp::experienceForLevel(31), 1507);
    EXPECT_EQ(xp::experienceForLevel(-4), 0);
    EXPECT_EQ(xp::experienceLimitOnLevel(0), 7);
    EXPECT_EQ(xp::experienceLimitOnLevel(14), 35);
    EXPECT_EQ(xp::experienceLimitOnLevel(15), 37);
    EXPECT_EQ(xp::experienceLimitOnLevel(30), 112);
    for (int level = 0; level < 200; ++level) {
        // Same curve as the phase-1 core maths, and consistent with itself.
        EXPECT_EQ(xp::experienceForLevel(level), core::xp::totalForLevel(level)) << level;
        EXPECT_EQ(xp::experienceLimitOnLevel(level), core::xp::pointsForNextLevel(level)) << level;
        EXPECT_EQ(xp::experienceForLevel(level + 1) - xp::experienceForLevel(level), xp::experienceLimitOnLevel(level)) << level;
        EXPECT_EQ(xp::levelForExperience(xp::experienceForLevel(level)), level) << level;
        if (level > 0) EXPECT_EQ(xp::levelForExperience(xp::experienceForLevel(level) - 1), level - 1) << level;
    }
    EXPECT_EQ(xp::levelForExperience(-10), 0);
    EXPECT_DOUBLE_EQ(xp::levelsForExperience(315 + 37 / 2), 15.0 + static_cast<double>(37 / 2) / 37);
    EXPECT_EQ(xp::totalExperience(15, 0.5f), 315 + 18);
}

TEST(ApiXp, Saturation) {
    namespace xp = api::xp;
    EXPECT_LT(xp::experienceForLevel(xp::kSaturatedLevel - 1), INT_MAX);
    EXPECT_EQ(xp::experienceForLevel(xp::kSaturatedLevel), INT_MAX);
    EXPECT_EQ(xp::experienceForLevel(1'000'000), INT_MAX);
    EXPECT_EQ(xp::levelForExperience(INT_MAX), xp::kSaturatedLevel);
    EXPECT_EQ(xp::levelForExperience(INT_MAX - 1), xp::kSaturatedLevel - 1);
}

TEST(ApiXp, Liquid) {
    namespace xp = api::xp;
    EXPECT_EQ(xp::experienceToLiquid(8.0f), api::kXpBottleVolume);
    EXPECT_EQ(xp::experienceToLiquid(1.0f), 20);
    EXPECT_EQ(xp::experienceToLiquid(0.07f), 1);
    EXPECT_FLOAT_EQ(xp::liquidToExperience(160), 8.0f);
    EXPECT_FLOAT_EQ(xp::liquidToExperience(10), 0.5f);
}

// ---- SB sizing ------------------------------------------------------------------------------------

TEST(ApiCapacity, SlotRows) {
    using core::capacity::slotRows;
    EXPECT_EQ(slotRows(27), 3);  // leather
    EXPECT_EQ(slotRows(45), 5);  // copper
    EXPECT_EQ(slotRows(54), 6);  // iron
    EXPECT_EQ(slotRows(81), 9);  // gold
    EXPECT_EQ(slotRows(108), 9); // diamond: 12 columns
    EXPECT_EQ(slotRows(120), 10); // netherite
    EXPECT_EQ(slotRows(10), 2);
    EXPECT_EQ(slotRows(0), 0);
}

TEST(ApiCapacity, BatteryAndTank) {
    namespace c = core::capacity;
    EXPECT_EQ(c::batteryCapacity(3, 1), 30000);
    EXPECT_EQ(c::batteryCapacity(6, 4), 240000);
    EXPECT_EQ(c::batteryCapacity(6, 4, 10000, 0.5), 150000); // 1 + 0.5 * 3 = 2.5
    EXPECT_EQ(c::batteryCapacity(6, 4, 10000, 0.0), 60000);
    EXPECT_EQ(c::batteryCapacity(10, 1e9), INT_MAX);
    EXPECT_EQ(c::batteryMaxInOut(3, 1), 60);
    EXPECT_EQ(c::batteryMaxInOut(9, 2), 360);
    EXPECT_EQ(c::tankCapacity(3, 1), 12000);
    EXPECT_EQ(c::tankCapacity(9, 2), 72000);
    EXPECT_EQ(c::tankMaxInOut(3, 1), 1000); // at least a bucket
    EXPECT_EQ(c::tankMaxInOut(10, 8), 1600);
    EXPECT_DOUBLE_EQ(c::adjustedMultiplier(4, 1.0), 4.0);
    EXPECT_DOUBLE_EQ(c::adjustedMultiplier(1, 3.0), 1.0);
}
