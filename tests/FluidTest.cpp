#include <gtest/gtest.h>

#include "core/Experience.h"
#include "core/Fluid.h"

using namespace abp::core;

// ---- fluid containers ------------------------------------------------------------------------

TEST(Fluid, BucketContents) {
    auto water = fluid::contentsOf("minecraft:water_bucket", 0);
    ASSERT_TRUE(water);
    EXPECT_EQ(water->fluid, (fluid::FluidStack{"water", 1000}));
    EXPECT_EQ(water->emptyId, "minecraft:bucket");
    EXPECT_EQ(fluid::contentsOf("minecraft:lava_bucket", 0)->fluid.id, "lava");
    EXPECT_EQ(fluid::contentsOf("minecraft:milk_bucket", 0)->fluid.id, "milk");
    EXPECT_EQ(fluid::contentsOf("minecraft:powder_snow_bucket", 0)->fluid.id, "powder_snow");
    EXPECT_FALSE(fluid::contentsOf("minecraft:bucket", 0));
    EXPECT_FALSE(fluid::contentsOf("minecraft:cod_bucket", 0));
    EXPECT_FALSE(fluid::contentsOf("minecraft:stone", 0));
}

TEST(Fluid, BottleContents) {
    auto waterBottle = fluid::contentsOf("minecraft:potion", 0);
    ASSERT_TRUE(waterBottle);
    EXPECT_EQ(waterBottle->fluid, (fluid::FluidStack{"water", 250}));
    EXPECT_EQ(waterBottle->emptyId, "minecraft:glass_bottle");
    EXPECT_EQ(fluid::contentsOf("minecraft:potion", 7)->fluid.id, "potion:7");
    EXPECT_EQ(fluid::contentsOf("minecraft:splash_potion", 0)->fluid.id, "splash_potion:0");
    EXPECT_EQ(fluid::contentsOf("minecraft:lingering_potion", 21)->fluid.id, "lingering_potion:21");
    EXPECT_EQ(fluid::contentsOf("minecraft:honey_bottle", 0)->fluid, (fluid::FluidStack{"honey", 250}));
}

TEST(Fluid, FillContainers) {
    auto wb = fluid::fill("minecraft:bucket", "water");
    ASSERT_TRUE(wb);
    EXPECT_EQ(wb->item, (fluid::ContainerItem{"minecraft:water_bucket", 0}));
    EXPECT_EQ(wb->amount, 1000);
    auto bottle = fluid::fill("minecraft:glass_bottle", "water");
    ASSERT_TRUE(bottle);
    EXPECT_EQ(bottle->item, (fluid::ContainerItem{"minecraft:potion", 0}));
    EXPECT_EQ(bottle->amount, 250);
    EXPECT_EQ(fluid::fill("minecraft:glass_bottle", "potion:12")->item, (fluid::ContainerItem{"minecraft:potion", 12}));
    EXPECT_EQ(fluid::fill("minecraft:glass_bottle", "splash_potion:3")->item, (fluid::ContainerItem{"minecraft:splash_potion", 3}));
    EXPECT_EQ(fluid::fill("minecraft:glass_bottle", "honey")->item.id, "minecraft:honey_bottle");
    EXPECT_FALSE(fluid::fill("minecraft:bucket", "honey"));
    EXPECT_FALSE(fluid::fill("minecraft:bucket", "potion:5"));
    EXPECT_FALSE(fluid::fill("minecraft:glass_bottle", "lava"));
    EXPECT_FALSE(fluid::fill("minecraft:bucket", "experience"));
    EXPECT_FALSE(fluid::fill("minecraft:glass_bottle", "experience"));
    EXPECT_FALSE(fluid::fill("minecraft:stone", "water"));
}

TEST(Fluid, RoundTripEveryContainer) {
    for (std::string_view fl : {"water", "lava", "milk", "powder_snow", "honey", "potion:5", "splash_potion:0", "lingering_potion:30"}) {
        auto shown = fluid::displayItem(fl);
        ASSERT_TRUE(shown) << fl;
        auto back = fluid::contentsOf(shown->id, shown->aux);
        ASSERT_TRUE(back) << fl;
        EXPECT_EQ(back->fluid.id, fl);
    }
    EXPECT_FALSE(fluid::displayItem("experience"));
}

TEST(Fluid, KnownIdsAndFamilies) {
    EXPECT_TRUE(fluid::isKnown("water"));
    EXPECT_TRUE(fluid::isKnown("experience"));
    EXPECT_TRUE(fluid::isKnown("potion:7"));
    EXPECT_FALSE(fluid::isKnown("potion:0")); // that's "water"
    EXPECT_TRUE(fluid::isKnown("splash_potion:0"));
    EXPECT_FALSE(fluid::isKnown("potion:x"));
    EXPECT_FALSE(fluid::isKnown("oil"));
    EXPECT_EQ(fluid::family("potion:7"), "potion");
    EXPECT_EQ(fluid::family("water"), "water");
    EXPECT_EQ(fluid::potionAux("lingering_potion:21"), 21);
    EXPECT_EQ(fluid::potionAux("water"), 0);
    EXPECT_TRUE(fluid::isEmptyContainer("minecraft:bucket"));
    EXPECT_TRUE(fluid::isEmptyContainer("minecraft:glass_bottle"));
    EXPECT_FALSE(fluid::isEmptyContainer("minecraft:water_bucket"));
}

TEST(Fluid, Capacity) {
    EXPECT_EQ(fluid::tankCapacity(27, 1), 12000);  // leather: 3 rows
    EXPECT_EQ(fluid::tankCapacity(120, 1), 52000); // netherite: 13 rows
    EXPECT_EQ(fluid::tankCapacity(27, 4), 48000);  // stack upgrade x4
    EXPECT_EQ(fluid::tankCapacity(5, 1), 4000);    // at least one row
    EXPECT_EQ(fluid::tankCapacity(27, 0), 12000);  // bad multiplier -> 1
    EXPECT_EQ(fluid::tankCapacity(120, 1 << 30), 1'000'000'000);
}

// ---- tank -------------------------------------------------------------------------------------

TEST(FluidTank, FillAndDrain) {
    fluid::Tank t({}, 3000);
    EXPECT_TRUE(t.empty());
    EXPECT_EQ(t.fill({"water", 1000}), 1000);
    EXPECT_EQ(t.fill({"lava", 1000}), 0) << "one fluid per tank";
    EXPECT_EQ(t.fill({"water", 5000}, true), 2000) << "simulate partial";
    EXPECT_EQ(t.amount(), 1000) << "simulate changes nothing";
    EXPECT_EQ(t.fill({"water", 2500}, false, true), 0) << "exact fill refuses a partial";
    EXPECT_EQ(t.fill({"water", 2000}, false, true), 2000);
    EXPECT_EQ(t.space(), 0);
    EXPECT_EQ(t.drain("lava", 1000).amount, 0);
    EXPECT_EQ(t.drain("", 250), (fluid::FluidStack{"water", 250}));
    EXPECT_TRUE(t.drain("water", 5000, false, true).empty()) << "exact drain refuses";
    EXPECT_EQ(t.drain("water", 5000).amount, 2750);
    EXPECT_TRUE(t.empty());
    EXPECT_TRUE(t.fluid().empty()) << "fluid kind clears when empty";
    EXPECT_EQ(t.fill({"lava", 1000}), 1000);
}

TEST(FluidTank, OverfullAfterCapacityDrop) {
    fluid::Tank t({"milk", 5000}, 2000);
    EXPECT_EQ(t.amount(), 5000) << "contents kept";
    EXPECT_EQ(t.space(), 0);
    EXPECT_EQ(t.fill({"milk", 1}), 0);
    EXPECT_EQ(t.drain("milk", 4000).amount, 4000);
    EXPECT_EQ(t.fill({"milk", 1000}), 1000);
}

TEST(FluidTank, Json) {
    fluid::Tank t({"potion:7", 750}, 4000);
    auto        j = t.toJson();
    EXPECT_EQ(j["fluid"], "potion:7");
    EXPECT_EQ(j["amount"], 750);
    auto back = fluid::Tank::fromJson(j, 4000);
    EXPECT_EQ(back.contents(), t.contents());
    EXPECT_TRUE(fluid::Tank::fromJson(nlohmann::json::object(), 10).empty());
    EXPECT_TRUE(fluid::Tank::fromJson(nlohmann::json{{"fluid", 3}, {"amount", "x"}}, 10).empty());
    EXPECT_TRUE(fluid::Tank::fromJson(nlohmann::json{{"fluid", "water"}, {"amount", -5}}, 10).empty());
    EXPECT_TRUE(fluid::Tank({}, 10).toJson().empty());
}

// ---- experience -------------------------------------------------------------------------------

TEST(Experience, LevelTable) {
    EXPECT_EQ(xp::pointsForNextLevel(0), 7);
    EXPECT_EQ(xp::pointsForNextLevel(14), 35);
    EXPECT_EQ(xp::pointsForNextLevel(15), 37);
    EXPECT_EQ(xp::pointsForNextLevel(29), 107);
    EXPECT_EQ(xp::pointsForNextLevel(30), 112);
    EXPECT_EQ(xp::totalForLevel(0), 0);
    EXPECT_EQ(xp::totalForLevel(1), 7);
    EXPECT_EQ(xp::totalForLevel(16), 352);
    EXPECT_EQ(xp::totalForLevel(30), 1395);
    EXPECT_EQ(xp::totalForLevel(31), 1507);
    long long sum = 0;
    for (int l = 0; l < 100; ++l) {
        EXPECT_EQ(xp::totalForLevel(l), sum) << "level " << l;
        sum += xp::pointsForNextLevel(l);
    }
}

TEST(Experience, FromTotal) {
    EXPECT_EQ(xp::fromTotal(0), (xp::LevelAndPoints{0, 0}));
    EXPECT_EQ(xp::fromTotal(-5), (xp::LevelAndPoints{0, 0}));
    EXPECT_EQ(xp::fromTotal(6), (xp::LevelAndPoints{0, 6}));
    EXPECT_EQ(xp::fromTotal(7), (xp::LevelAndPoints{1, 0}));
    EXPECT_EQ(xp::fromTotal(1400), (xp::LevelAndPoints{30, 5}));
    for (long long t : {0LL, 1LL, 100LL, 352LL, 9999LL, 123456LL}) {
        auto lp = xp::fromTotal(t);
        EXPECT_EQ(xp::toTotal(lp.level, lp.points), t);
        EXPECT_LT(lp.points, xp::pointsForNextLevel(lp.level));
    }
}

TEST(Experience, TransferPlanning) {
    long long t = xp::toTotal(5, 3); // level 5 + 3 points
    EXPECT_EQ(xp::storeLevels(t, 1), t - xp::totalForLevel(4));
    EXPECT_EQ(xp::storeLevels(t, 100), t);
    EXPECT_EQ(xp::storeLevels(0, 1), 0);
    EXPECT_EQ(xp::takeLevels(t, 1), xp::totalForLevel(6) - t);
    EXPECT_EQ(xp::takeLevels(0, 30), 1395);
    EXPECT_EQ(xp::keepLevel(t, 5), 3);
    EXPECT_EQ(xp::keepLevel(t, 6), -(xp::totalForLevel(6) - t));
    EXPECT_EQ(xp::keepLevel(xp::totalForLevel(10), 10), 0);
}

TEST(Experience, Mending) {
    EXPECT_EQ(xp::pointsToRepair(0), 0);
    EXPECT_EQ(xp::pointsToRepair(1), 1);
    EXPECT_EQ(xp::pointsToRepair(2), 1);
    EXPECT_EQ(xp::pointsToRepair(7), 4);
}
