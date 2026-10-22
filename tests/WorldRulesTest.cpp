#include <gtest/gtest.h>

#include "core/WorldRules.h"

using namespace abp::core;

TEST(WorldRules, MoonBrightnessCycles) {
    EXPECT_FLOAT_EQ(moonBrightness(0), 1.0f);
    EXPECT_FLOAT_EQ(moonBrightness(4), 0.0f);
    EXPECT_FLOAT_EQ(moonBrightness(7), 0.75f);
    EXPECT_FLOAT_EQ(moonBrightness(8), 1.0f);
    EXPECT_FLOAT_EQ(moonBrightness(-1), 0.75f);
}

TEST(WorldRules, EffectiveDifficultyMatchesVanilla) {
    EXPECT_FLOAT_EQ(effectiveDifficulty(0, 10'000'000, 10'000'000, 1.0f), 0.0f); // peaceful
    // New world, normal: 2 * 0.75.
    EXPECT_FLOAT_EQ(effectiveDifficulty(2, 0, 0, 1.0f), 1.5f);
    // Hard, everything maxed: 3 * (0.75 + 0.25 + 1 + 0.25) = 6.75.
    EXPECT_FLOAT_EQ(effectiveDifficulty(3, 1'512'000 + 72'000, 3'600'000, 1.0f), 6.75f);
    // Easy halves the regional part.
    EXPECT_FLOAT_EQ(effectiveDifficulty(1, 1'512'000 + 72'000, 3'600'000, 1.0f), 1.0f * (0.75f + 0.25f + (0.75f + 0.25f) * 0.5f));
}

TEST(WorldRules, SpecialMultiplier) {
    EXPECT_FLOAT_EQ(specialMultiplier(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(specialMultiplier(3.0f), 0.5f);
    EXPECT_FLOAT_EQ(specialMultiplier(5.0f), 1.0f);
}

TEST(WorldRules, DifficultyIndexBuckets) {
    EXPECT_EQ(difficultyIndex(0.0f), 0);
    EXPECT_EQ(difficultyIndex(2.25f), 0); // 3/6.75*2.25 - .1 = 0.9
    EXPECT_EQ(difficultyIndex(2.5f), 1);
    EXPECT_EQ(difficultyIndex(4.75f), 2);
    EXPECT_EQ(difficultyIndex(6.75f), 2);
}

TEST(WorldRules, TierPoolsFollowMinimumTiers) {
    std::array<int, 6> w{625, 250, 125, 25, 5, 1};
    auto all = tierPool(w, 0, 1, 2);
    ASSERT_EQ(all.size(), 6u);
    EXPECT_EQ(totalWeight(all), 1031);
    auto mid = tierPool(w, 1, 1, 2);
    ASSERT_EQ(mid.size(), 5u);
    EXPECT_EQ(mid.front().tier, Tier::Copper);
    auto high = tierPool(w, 2, 1, 2);
    ASSERT_EQ(high.size(), 4u);
    EXPECT_EQ(high.front().tier, Tier::Iron);
    EXPECT_EQ(pickWeighted(all, 0), Tier::Leather);
    EXPECT_EQ(pickWeighted(all, 624), Tier::Leather);
    EXPECT_EQ(pickWeighted(all, 625), Tier::Copper);
    EXPECT_EQ(pickWeighted(all, 1030), Tier::Netherite);
    EXPECT_TRUE(tierPool(w, 2, 1, 6).empty());
}

TEST(WorldRules, DropChanceScalesWithTierAndLooting) {
    EXPECT_DOUBLE_EQ(dropChance(0.5, 0.15, 0, Tier::Leather), 0.5);
    EXPECT_DOUBLE_EQ(dropChance(0.5, 0.15, 3, Tier::Copper), (0.5 + 0.45) * 1.25);
    EXPECT_DOUBLE_EQ(dropChanceMultiplier(Tier::Netherite), 6.0);
    EXPECT_EQ(minDifficultyOf(Tier::Iron), 1);
    EXPECT_EQ(minDifficultyOf(Tier::Netherite), 4);
}

TEST(WorldRules, ArmorRanges) {
    auto leather = armorRanges(Tier::Leather);
    EXPECT_EQ(leather.helmet.first, 3);
    EXPECT_EQ(leather.helmet.last, 5);
    auto copper = armorRanges(Tier::Copper);
    EXPECT_EQ(copper.helmet.first, 2);
    EXPECT_EQ(copper.boots.first, 3);
    auto netherite = armorRanges(Tier::Netherite);
    EXPECT_EQ(netherite.leggings.first, 0);
    EXPECT_EQ(netherite.leggings.last, 1);
    EXPECT_EQ(armorWeight(0), 1);
    EXPECT_EQ(armorWeight(4), 81);
}

TEST(WorldRules, SlownessLevels) {
    EXPECT_EQ(slownessLevels(3, 3, 1.0), 0);
    EXPECT_EQ(slownessLevels(4, 3, 1.0), 1);
    EXPECT_EQ(slownessLevels(5, 3, 0.4), 1); // ceil(0.8)
    EXPECT_EQ(slownessLevels(30, 1, 5.0), 10);
}

TEST(WorldRules, NearestFormatCode) {
    EXPECT_EQ(nearestFormatCode(0xFFFFFF), 'f');
    EXPECT_EQ(nearestFormatCode(0x000000), '0');
    EXPECT_EQ(nearestFormatCode(0xFF5555), 'c');
    EXPECT_EQ(nearestFormatCode(0xFFA0703C & 0xFFFFFF), 'n'); // default leather main colour -> copper brown
}

TEST(WorldRules, ShortDateTime) {
    EXPECT_EQ(shortDateTime(2026, 10, 22, 13, 5), "10/22/26, 1:05 PM");
    EXPECT_EQ(shortDateTime(2026, 1, 2, 0, 0), "1/2/26, 12:00 AM");
    EXPECT_EQ(shortDateTime(2009, 12, 31, 12, 59), "12/31/09, 12:59 PM");
}
