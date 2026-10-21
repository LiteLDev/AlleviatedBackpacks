#include <climits>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "core/BackpackInventory.h"
#include "core/StackMultiplier.h"

using namespace abp::core;

namespace {
ItemStackData stone(int n) { return ItemStackData::of("minecraft:stone", n); }
ItemStackData pearl(int n) { return ItemStackData::of("minecraft:ender_pearl", n, 16); }
ItemStackData sword(int n) { return ItemStackData::of("minecraft:iron_sword", n, 1); }
ItemStackData bundle(int n) { return ItemStackData::of("minecraft:bundle", n, 1); }
AccessContext const kPlayer{Source::Player};
} // namespace

TEST(StackMultiplier, CombineMultiplies) {
    std::vector<double> none;
    EXPECT_DOUBLE_EQ(stack::combine(none), 1.0);
    std::vector<double> one{2.0};
    EXPECT_DOUBLE_EQ(stack::combine(one), 2.0);
    std::vector<double> three{1.5, 4.0, 16.0};
    EXPECT_DOUBLE_EQ(stack::combine(three), 96.0);
    std::vector<double> down{0.125, 16.0};
    EXPECT_DOUBLE_EQ(stack::combine(down), 2.0);
    std::vector<double> bad{0.0, -3.0, 2.0};
    EXPECT_DOUBLE_EQ(stack::combine(bad), 2.0);
}

TEST(StackMultiplier, CombineCapsAtIntMax) {
    std::vector<double> omega{static_cast<double>(INT_MAX), 16.0};
    double              m = stack::combine(omega);
    EXPECT_LE(m * 64.0, static_cast<double>(INT_MAX) + 1.0);
    EXPECT_EQ(stack::baseSlotLimit(m), INT_MAX);
    // Order never matters, even around the clamp.
    std::vector<double> omegaDown{static_cast<double>(INT_MAX), 0.125};
    std::vector<double> downOmega{0.125, static_cast<double>(INT_MAX)};
    EXPECT_DOUBLE_EQ(stack::combine(omegaDown), stack::combine(downOmega));
    EXPECT_EQ(stack::baseSlotLimit(stack::combine(omegaDown)), INT_MAX);
    std::vector<double> inf{std::numeric_limits<double>::infinity(), 2.0};
    EXPECT_DOUBLE_EQ(stack::combine(inf), 2.0);
}

TEST(StackMultiplier, ScaledLimit) {
    EXPECT_EQ(stack::scaledLimit(64, 1.5), 96);
    EXPECT_EQ(stack::scaledLimit(16, 2.0), 32);
    EXPECT_EQ(stack::scaledLimit(1, 1.5), 1);  // rounds down
    EXPECT_EQ(stack::scaledLimit(1, 2.0), 2);  // unstackables stack with tier 1+ (as in SB)
    EXPECT_EQ(stack::scaledLimit(64, 0.125), 8);
    EXPECT_EQ(stack::scaledLimit(16, 0.03125), 1); // never below 1
    EXPECT_EQ(stack::scaledLimit(64, 1e12), INT_MAX);
    EXPECT_EQ(stack::scaledLimit(64, 0.0), 64); // invalid multiplier = 1
    EXPECT_EQ(stack::baseSlotLimit(4.0), 256);
}

TEST(StackMultiplier, FirstSlotOver) {
    BackpackInventory inv(4);
    inv.set(0, stone(100));
    inv.set(1, pearl(20));
    inv.set(2, sword(1));
    auto all = [](ItemStackData const&) { return true; };
    EXPECT_EQ(stack::firstSlotOver(inv, 2.0, all), -1);
    EXPECT_EQ(stack::firstSlotOver(inv, 1.5, all), 0); // 100 > 96
    inv.set(0, stone(90));
    EXPECT_EQ(stack::firstSlotOver(inv, 1.5, all), -1); // 90 <= 96, pearls 20 <= 24
    EXPECT_EQ(stack::firstSlotOver(inv, 1.0, all), 0);
    inv.set(0, stone(64));
    EXPECT_EQ(stack::firstSlotOver(inv, 1.0, all), 1); // 20 pearls > 16
    // Items not affected by stack upgrades are checked against their own max stack size.
    inv.set(1, {});
    inv.set(3, bundle(2));
    auto notBundles = [](ItemStackData const& s) { return s.id != "minecraft:bundle"; };
    EXPECT_EQ(stack::firstSlotOver(inv, 4.0, notBundles), 3);
    EXPECT_EQ(stack::firstSlotOver(inv, 4.0, all), -1);
}

TEST(StackMultiplier, LimitModifierWithInventory) {
    BackpackInventory inv(2);
    double            mult = 8.0;
    inv.addLimitModifier("stack", 0, [&](int, ItemStackData const&, int cur) { return stack::scaledLimit(cur, mult); });
    EXPECT_EQ(inv.slotLimit(0, stone(1)), 512);
    EXPECT_EQ(inv.slotLimit(0, sword(1)), 8);
    auto r = inv.insert(stone(1000), kPlayer);
    EXPECT_EQ(r.inserted, 1000);
    EXPECT_EQ(inv.at(0).count, 512);
    EXPECT_EQ(inv.at(1).count, 488);
}

TEST(StackMultiplier, SortDoesNotOverflowWithHugeSlots) {
    BackpackInventory inv(3);
    inv.addLimitModifier("stack", 0, [](int, ItemStackData const&, int) { return INT_MAX; });
    inv.set(0, stone(INT_MAX));
    inv.set(1, ItemStackData::of("minecraft:dirt", 5));
    inv.set(2, stone(INT_MAX));
    inv.sort([](ItemStackData const& a, ItemStackData const& b) { return a.id < b.id; });
    EXPECT_EQ(inv.at(0).id, "minecraft:dirt");
    EXPECT_EQ(inv.at(1).count, INT_MAX);
    EXPECT_EQ(inv.at(2).count, INT_MAX);
    EXPECT_EQ(inv.count([](ItemStackData const& s) { return s.id == "minecraft:stone"; }), 2LL * INT_MAX);
}
