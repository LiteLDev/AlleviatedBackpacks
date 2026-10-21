#include <gtest/gtest.h>

#include "upgrade/anvil/AnvilMath.h"

using namespace abp::upgrade::anvil::math;

TEST(AnvilMath, MaterialRepairUsesQuarterSteps) {
    // Diamond sword: 1561 max durability, quarter = 390.
    auto r = repairWithMaterial(1000, 1561, 64);
    EXPECT_EQ(r.unitsUsed, 3); // 1000 -> 610 -> 220 -> 0
    EXPECT_EQ(r.newDamage, 0);
    r = repairWithMaterial(1000, 1561, 1);
    EXPECT_EQ(r.unitsUsed, 1);
    EXPECT_EQ(r.newDamage, 610);
}

TEST(AnvilMath, MaterialRepairNothingToDo) {
    EXPECT_EQ(repairWithMaterial(0, 1561, 5).unitsUsed, 0);
    EXPECT_EQ(repairWithMaterial(10, 0, 5).unitsUsed, 0);
    EXPECT_EQ(repairWithMaterial(10, 100, 0).unitsUsed, 0);
}

TEST(AnvilMath, CombinedDamageAddsBonus) {
    // 250 max: left has 50 left, right has 100 left, +30 bonus = 180 left -> damage 70.
    EXPECT_EQ(combinedDamage(250, 200, 150), 70);
    EXPECT_EQ(combinedDamage(250, 10, 10), 0);
}

TEST(AnvilMath, FinalCostTooExpensive) {
    auto c = finalCost(30, 12, 0, false);
    EXPECT_EQ(c.levels, 42);
    EXPECT_TRUE(c.tooExpensive);
    EXPECT_FALSE(finalCost(30, 12, 0, true).tooExpensive);
    EXPECT_FALSE(finalCost(20, 19, 0, false).tooExpensive);
}

TEST(AnvilMath, RenameOnlyCappedAt39) {
    auto c = finalCost(63, 1, 1, false);
    EXPECT_EQ(c.levels, 39);
    EXPECT_FALSE(c.tooExpensive);
    // Rename plus other work is not capped.
    EXPECT_TRUE(finalCost(63, 3, 1, false).tooExpensive);
}

TEST(AnvilMath, TruncateUtf8KeepsWholeCharacters) {
    EXPECT_EQ(truncateUtf8("Excalibur", 3), "Exc");
    EXPECT_EQ(truncateUtf8("short", 50), "short");
    // "铁砧剑" is 3 characters of 3 bytes each.
    EXPECT_EQ(truncateUtf8("\xe9\x93\x81\xe7\xa0\xa7\xe5\x89\x91", 2), "\xe9\x93\x81\xe7\xa0\xa7");
    EXPECT_EQ(truncateUtf8("a\xe9\x93\x81", 1), "a");
    // A truncated multi-byte sequence at the end is dropped.
    EXPECT_EQ(truncateUtf8("ab\xe9\x93", 10), "ab");
    EXPECT_EQ(truncateUtf8("", 5), "");
}

TEST(AnvilMath, PriorWorkPenaltyDoubles) {
    EXPECT_EQ(nextRepairCost(0, 0, false), 1);
    EXPECT_EQ(nextRepairCost(1, 0, false), 3);
    EXPECT_EQ(nextRepairCost(3, 7, false), 15);
    EXPECT_EQ(nextRepairCost(3, 0, true), 3);
}
