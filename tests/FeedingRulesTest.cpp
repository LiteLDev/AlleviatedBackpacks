#include <gtest/gtest.h>

#include "upgrade/feeding/FeedingRules.h"

using namespace abp::upgrade::feeding;
using abp::core::Filter;
using abp::core::FilterMode;
using abp::core::ItemStackData;

namespace {
EaterState eater(int hunger, float health = 20.f) {
    EaterState s;
    s.hunger = hunger;
    s.health = health;
    return s;
}
} // namespace

TEST(FeedingRules, NeverWhenFull) {
    for (auto mode : {HungerMode::Always, HungerMode::Half, HungerMode::Full}) {
        EXPECT_FALSE(shouldEat(eater(20), 1, {mode, true}));
        EXPECT_FALSE(shouldEat(eater(20, 1.f), 1, {mode, true}));
    }
}

TEST(FeedingRules, FullMeansNoWaste) {
    FeedingOptions o{HungerMode::Full, false};
    EXPECT_FALSE(shouldEat(eater(13), 8, o)); // missing 7 < 8
    EXPECT_TRUE(shouldEat(eater(12), 8, o));  // missing 8
    EXPECT_TRUE(shouldEat(eater(0), 8, o));
}

TEST(FeedingRules, HalfAndAlways) {
    FeedingOptions half{HungerMode::Half, false};
    EXPECT_TRUE(shouldEat(eater(16), 8, half));  // missing 4 = half of 8
    EXPECT_FALSE(shouldEat(eater(17), 8, half)); // missing 3
    EXPECT_TRUE(shouldEat(eater(17), 5, half));  // missing 3 >= 2.5
    EXPECT_FALSE(shouldEat(eater(18), 5, half)); // missing 2 < 2.5
    FeedingOptions always{HungerMode::Always, false};
    EXPECT_TRUE(shouldEat(eater(19), 20, always));
}

TEST(FeedingRules, HurtOverridesWaste) {
    FeedingOptions o{HungerMode::Full, true};
    EXPECT_FALSE(shouldEat(eater(19, 10.f), 8, o)); // exactly half is not below half
    EXPECT_TRUE(shouldEat(eater(19, 9.f), 8, o));
    o.eatWhenHurt = false;
    EXPECT_FALSE(shouldEat(eater(19, 9.f), 8, o));
}

TEST(FeedingRules, HarmfulNeedsExplicitAllow) {
    auto        harmful = defaultHarmfulFoods();
    auto        flesh   = ItemStackData::of("minecraft:rotten_flesh", 4);
    auto        bread   = ItemStackData::of("minecraft:bread", 4);
    EXPECT_TRUE(isHarmful(flesh.id, harmful));
    EXPECT_FALSE(isHarmful(bread.id, harmful));

    Filter f(9); // empty allow list: everything safe passes, harmful does not
    EXPECT_TRUE(filterAllows(f, bread, false, nullptr));
    EXPECT_FALSE(filterAllows(f, flesh, true, nullptr));

    f.setGhost(0, flesh); // explicitly allowed
    EXPECT_TRUE(filterAllows(f, flesh, true, nullptr));
    EXPECT_FALSE(filterAllows(f, bread, false, nullptr));

    f.mode = FilterMode::Deny; // a deny list never whitelists
    EXPECT_FALSE(filterAllows(f, flesh, true, nullptr));
    EXPECT_TRUE(filterAllows(f, bread, false, nullptr));
}

TEST(FeedingRules, ModeFromInt) {
    EXPECT_EQ(hungerModeFromInt(0), HungerMode::Always);
    EXPECT_EQ(hungerModeFromInt(1), HungerMode::Half);
    EXPECT_EQ(hungerModeFromInt(2), HungerMode::Full);
    EXPECT_EQ(hungerModeFromInt(42), HungerMode::Full);
}
