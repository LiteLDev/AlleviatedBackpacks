#include <gtest/gtest.h>

#include "core/render/BackpackModel.h"

using namespace abp::core::render;

TEST(BackpackModel, SingleTankSideFollowsSlotHalf) {
    // 5 upgrade slots: slots 0,1 are the first half (< 5/2 = 2), 2.. the second.
    EXPECT_EQ(tankSides({0}, 5), std::vector<TankSide>{TankSide::Left});
    EXPECT_EQ(tankSides({1}, 5), std::vector<TankSide>{TankSide::Left});
    EXPECT_EQ(tankSides({2}, 5), std::vector<TankSide>{TankSide::Right});
    EXPECT_EQ(tankSides({0}, 1), std::vector<TankSide>{TankSide::Right}); // leather: 1 slot, 1/2 = 0
}

TEST(BackpackModel, TwoTanksAreLeftThenRight) {
    auto s = tankSides({3, 4}, 7);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0], TankSide::Left);
    EXPECT_EQ(s[1], TankSide::Right);
    EXPECT_TRUE(tankSides({}, 7).empty());
}

TEST(BackpackModel, RatioToStepMatchesSb) {
    EXPECT_EQ(ratioToStep(0.0f, kFluidSteps), 0);
    EXPECT_EQ(ratioToStep(0.09f, kFluidSteps), 0);
    EXPECT_EQ(ratioToStep(0.1f, kFluidSteps), 1);
    EXPECT_EQ(ratioToStep(0.99f, kFluidSteps), 9);
    EXPECT_EQ(ratioToStep(1.0f, kFluidSteps), 10);
    EXPECT_EQ(ratioToStep(2.0f, kFluidSteps), 10);
    EXPECT_EQ(ratioToStep(-1.0f, kChargeSteps), 0);
    EXPECT_EQ(ratioToStep(0.5f, kChargeSteps), 2);
    EXPECT_EQ(ratioToStep(0.5f, 0), 0);
}

TEST(BackpackModel, JsonRoundTrip) {
    BackpackModel m;
    m.left    = TankModel{"water", 0.5f, 0xFF3F76E4, false};
    m.right   = TankModel{"lava", 1.0f, 0xFFFFFFFF, false};
    m.battery = 0.25f;
    DisplayModel d;
    d.item     = abp::core::ItemStackData::of("minecraft:diamond", 1);
    d.rotation = 90;
    d.zOffset  = -3;
    d.side     = "left";
    m.display  = d;
    m.activity = {"jukebox"};
    auto back  = BackpackModel::fromJson(m.toJson());
    EXPECT_EQ(back, m);
    EXPECT_EQ(BackpackModel::fromJson(nlohmann::json::object()), BackpackModel{});
    EXPECT_EQ(BackpackModel::fromJson(nlohmann::json(42)), BackpackModel{});
}

TEST(BackpackModel, JsonClampsAndValidates) {
    auto m = BackpackModel::fromJson(nlohmann::json::parse(R"({"left":{"fluid":"water","ratio":3},"battery":-1,
        "display":{"item":{"id":"minecraft:stone","count":1},"zOffset":40,"side":"up"}})"));
    ASSERT_TRUE(m.left);
    EXPECT_FLOAT_EQ(m.left->ratio, 1.0f);
    ASSERT_TRUE(m.battery);
    EXPECT_FLOAT_EQ(*m.battery, 0.0f);
    ASSERT_TRUE(m.display);
    EXPECT_EQ(m.display->zOffset, 16);
    EXPECT_EQ(m.display->side, "front");
}

TEST(BackpackModel, FluidLooks) {
    EXPECT_EQ(fluidLook("water"), FluidLook::Water);
    EXPECT_EQ(fluidLook("minecraft:lava"), FluidLook::Lava);
    EXPECT_EQ(fluidLook("potion:5"), FluidLook::Potion);
    EXPECT_EQ(fluidLook("lingering_potion:3"), FluidLook::Potion);
    EXPECT_EQ(fluidLook("experience"), FluidLook::Experience);
    EXPECT_EQ(fluidLook("milk"), FluidLook::Milk);
}

TEST(BackpackModel, DisplayDepth) {
    EXPECT_FLOAT_EQ(displayDepth(0), kDisplayDepthOffset);
    EXPECT_NEAR(displayDepth(1), 0.0015f - 0.5f / 15.95f, 1e-6f);
    EXPECT_GT(displayDepth(-2), displayDepth(2));
}

#include "core/Comparator.h"

TEST(Comparator, MatchesScAnalogSignal) {
    using namespace abp::core;
    AccessContext const player{Source::Player};
    BackpackInventory inv(27);
    EXPECT_EQ(comparatorSignal(inv), 0);
    inv.insert(ItemStackData::of("minecraft:stone", 1), player);
    EXPECT_EQ(comparatorSignal(inv), 1); // anything stored: at least 1
    BackpackInventory full(2);
    full.insert(ItemStackData::of("minecraft:stone", 128), player);
    EXPECT_EQ(comparatorSignal(full), 15);
    BackpackInventory half(2);
    half.insert(ItemStackData::of("minecraft:stone", 64), player);
    EXPECT_EQ(comparatorSignal(half), 8); // floor(0.5 * 14) + 1
    BackpackInventory pearls(1);
    pearls.insert(ItemStackData::of("minecraft:ender_pearl", 8, 16), player);
    EXPECT_EQ(comparatorSignal(pearls), 8); // 8 / 16
}
