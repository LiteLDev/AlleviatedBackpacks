#include <gtest/gtest.h>

#include "upgrade/feeding/FeedingRules.h"

using namespace abp::upgrade::feeding;

namespace {
EaterState eater(int food, float health = 20.f) {
    EaterState s;
    s.foodLevel = food;
    s.health    = health;
    return s;
}
} // namespace

TEST(FeedingRules, NeverWhenNotHungry) {
    for (auto level : {HungerLevel::Any, HungerLevel::Half, HungerLevel::Full}) {
        EXPECT_FALSE(shouldFeed(eater(20), 1, {level, true}));
        EXPECT_FALSE(shouldFeed(eater(20, 1.f), 1, {level, true}));
    }
}

TEST(FeedingRules, FullMeansNoWaste) {
    FeedingOptions o{HungerLevel::Full, false};
    EXPECT_FALSE(shouldFeed(eater(13), 8, o)); // hunger 7 < 8
    EXPECT_TRUE(shouldFeed(eater(12), 8, o));  // hunger 8
    EXPECT_TRUE(shouldFeed(eater(0), 8, o));
}

TEST(FeedingRules, HalfUsesIntegerHalfAndAnyFeedsAlways) {
    FeedingOptions half{HungerLevel::Half, false};
    EXPECT_TRUE(shouldFeed(eater(16), 8, half));  // 8/2 = 4 <= 4
    EXPECT_FALSE(shouldFeed(eater(17), 8, half)); // 4 > 3
    EXPECT_TRUE(shouldFeed(eater(18), 5, half));  // 5/2 = 2 <= 2 (integer division, as SC)
    EXPECT_FALSE(shouldFeed(eater(19), 5, half)); // 2 > 1
    FeedingOptions any{HungerLevel::Any, false};
    EXPECT_TRUE(shouldFeed(eater(19), 20, any));
}

TEST(FeedingRules, HurtIgnoresHungerSetting) {
    FeedingOptions o{HungerLevel::Full, true};
    EXPECT_FALSE(shouldFeed(eater(19, 20.f), 8, o));  // not hurt
    EXPECT_FALSE(shouldFeed(eater(19, 19.95f), 8, o)); // within 0.1 of max health is not hurt
    EXPECT_TRUE(shouldFeed(eater(19, 19.5f), 8, o));   // any missing health
    o.feedImmediatelyWhenHurt = false;
    EXPECT_FALSE(shouldFeed(eater(19, 2.f), 8, o));
}

TEST(FeedingRules, Edibility) {
    EXPECT_TRUE(isEdible("minecraft:rotten_flesh", 4)); // SB has no harmful-food exclusions
    EXPECT_TRUE(isEdible("minecraft:chorus_fruit", 4));
    EXPECT_FALSE(isEdible("minecraft:ominous_bottle", 1));
    EXPECT_FALSE(isEdible("minecraft:something", 0));
}

TEST(FeedingRules, HungerLevelNames) {
    EXPECT_EQ(hungerLevelFromName("any"), HungerLevel::Any);
    EXPECT_EQ(hungerLevelFromName("full"), HungerLevel::Full);
    EXPECT_EQ(hungerLevelFromName("bogus"), HungerLevel::Half);
    EXPECT_EQ(toString(HungerLevel::Half), "half");
    EXPECT_EQ(next(HungerLevel::Any), HungerLevel::Half);
    EXPECT_EQ(next(HungerLevel::Half), HungerLevel::Full);
    EXPECT_EQ(next(HungerLevel::Full), HungerLevel::Any);
}
