#include <gtest/gtest.h>

#include <map>

#include "upgrade/alchemy/AlchemyRules.h"

using namespace abp::upgrade::alchemy;
using abp::core::ItemStackData;

namespace {
ItemStackData item(std::string id, int aux = 0) { return ItemStackData::of(std::move(id), 1, 64, aux); }

EffectLookup has(std::map<int, Effect> effects) {
    return [effects = std::move(effects)](int id) -> std::optional<Effect> {
        auto it = effects.find(id);
        if (it == effects.end()) return std::nullopt;
        return it->second;
    };
}
} // namespace

TEST(AlchemyRules, ConditionNamesRoundTripAndCycle) {
    for (int i = 0; i < kConditionCount; ++i) {
        auto c = static_cast<Condition>(i);
        EXPECT_EQ(conditionFromName(conditionName(c)), c);
    }
    EXPECT_EQ(conditionFromName("bogus"), Condition::Never);
    EXPECT_EQ(next(Condition::Never), Condition::Always);
    EXPECT_EQ(next(Condition::Always), Condition::UnderWater);
    EXPECT_EQ(next(Condition::Hurt), Condition::NegativeEffect);
    EXPECT_EQ(next(Condition::NegativeEffect), Condition::Never);
    EXPECT_FLOAT_EQ(defaultValue(Condition::Hurt), 0.75f);
    EXPECT_FLOAT_EQ(defaultValue(Condition::OnFire), -1.f);
}

TEST(AlchemyRules, ConditionTests) {
    EntityState s;
    EXPECT_FALSE(test(Condition::Never, s, -1));
    EXPECT_TRUE(test(Condition::Always, s, -1));
    EXPECT_FALSE(test(Condition::OnFire, s, -1));
    s.onFire = true;
    EXPECT_TRUE(test(Condition::OnFire, s, -1));
    s.fallDistance = 2;
    EXPECT_FALSE(test(Condition::Falling, s, -1)); // SC: > 2
    s.fallDistance = 2.5f;
    EXPECT_TRUE(test(Condition::Falling, s, -1));
    s.health = 20;
    EXPECT_FALSE(test(Condition::Hurt, s, 0.75f)); // full health
    s.health = 15;
    EXPECT_FALSE(test(Condition::Hurt, s, 0.75f)); // 75% is not below 75%
    s.health = 14;
    EXPECT_TRUE(test(Condition::Hurt, s, 0.75f));
    s.health = 0;
    EXPECT_FALSE(test(Condition::Hurt, s, 0.75f)); // dead
    s.harmfulEffect = true;
    EXPECT_TRUE(test(Condition::NegativeEffect, s, -1));
}

TEST(AlchemyRules, DefaultConditionByFirstEffect) {
    EXPECT_EQ(defaultConditionForEffects({}), Condition::Never);
    EXPECT_EQ(defaultConditionForEffects({{effect::WaterBreathing, 3600, 0}}), Condition::UnderWater);
    EXPECT_EQ(defaultConditionForEffects({{effect::Heal, 1, 0}}), Condition::Hurt);
    EXPECT_EQ(defaultConditionForEffects({{effect::Regeneration, 900, 0}}), Condition::Hurt);
    EXPECT_EQ(defaultConditionForEffects({{effect::FireResistance, 3600, 0}}), Condition::OnFire);
    EXPECT_EQ(defaultConditionForEffects({{effect::Speed, 3600, 0}}), Condition::Sprinting);
    EXPECT_EQ(defaultConditionForEffects({{effect::Haste, 3600, 0}}), Condition::Mining);
    EXPECT_EQ(defaultConditionForEffects({{effect::SlowFalling, 1800, 0}}), Condition::Falling);
    EXPECT_EQ(defaultConditionForEffects({{effect::Poison, 900, 0}}), Condition::Always);
    EXPECT_EQ(defaultCondition(Kind::MilkBucket, {}), Condition::NegativeEffect);
    EXPECT_EQ(defaultCondition(Kind::OminousBottle, {}), Condition::Always);
    EXPECT_EQ(defaultCondition(Kind::GoldenApple, *foodEffects("minecraft:golden_apple")), Condition::Always);
}

TEST(AlchemyRules, Kinds) {
    EXPECT_EQ(kindOf("minecraft:potion"), Kind::Potion);
    EXPECT_EQ(kindOf("minecraft:splash_potion"), Kind::SplashPotion);
    EXPECT_EQ(kindOf("minecraft:lingering_potion"), Kind::None); // SB: not supported
    EXPECT_EQ(kindOf("minecraft:milk_bucket"), Kind::MilkBucket);
    EXPECT_EQ(kindOf("minecraft:ominous_bottle"), Kind::OminousBottle);
    EXPECT_EQ(kindOf("minecraft:golden_apple"), Kind::GoldenApple);
    EXPECT_EQ(kindOf("minecraft:enchanted_golden_apple"), Kind::EffectFood);
    EXPECT_EQ(kindOf("minecraft:rotten_flesh"), Kind::EffectFood);
    EXPECT_EQ(kindOf("minecraft:bread"), Kind::None);
    EXPECT_TRUE(accepts(Kind::EffectFood, "minecraft:golden_apple")); // food definition takes golden apples too
}

TEST(AlchemyRules, ShouldApplyEffects) {
    std::vector<Effect> two{{effect::Speed, 100, 1}, {effect::Haste, 100, 0}};
    // Any Effect Missing (matchAll): apply while one is missing.
    EXPECT_TRUE(shouldApplyEffects(has({}), two, true, true));
    EXPECT_TRUE(shouldApplyEffects(has({{effect::Speed, {effect::Speed, 50, 1}}}), two, true, true));
    EXPECT_FALSE(shouldApplyEffects(has({{effect::Speed, {effect::Speed, 50, 1}}, {effect::Haste, {effect::Haste, 5, 0}}}), two, true, true));
    // Weaker amplifier counts as missing when matching amplifiers.
    auto weak = has({{effect::Speed, {effect::Speed, 50, 0}}, {effect::Haste, {effect::Haste, 5, 0}}});
    EXPECT_TRUE(shouldApplyEffects(weak, two, true, true));
    EXPECT_FALSE(shouldApplyEffects(weak, two, true, false));
    // All Effects Missing: only when none is present.
    EXPECT_TRUE(shouldApplyEffects(has({}), two, false, true));
    EXPECT_FALSE(shouldApplyEffects(has({{effect::Haste, {effect::Haste, 5, 0}}}), two, false, true));
    EXPECT_FALSE(shouldApplyEffects(has({}), {}, true, true)); // water bottle: nothing to apply
}

TEST(AlchemyRules, StackMatching) {
    auto swift      = item("minecraft:potion", 14);
    auto swiftLong  = item("minecraft:potion", 15);
    auto swiftE     = std::vector<Effect>{{effect::Speed, 3600, 0}};
    auto swiftLongE = std::vector<Effect>{{effect::Speed, 9600, 0}};
    EXPECT_TRUE(stackMatches(Kind::Potion, swift, swiftE, swift, swiftE, true, true, true));
    EXPECT_FALSE(stackMatches(Kind::Potion, swiftLong, swiftLongE, swift, swiftE, true, true, true));
    EXPECT_TRUE(stackMatches(Kind::Potion, swiftLong, swiftLongE, swift, swiftE, true, false, true)); // ignore duration
    EXPECT_FALSE(stackMatches(Kind::Potion, item("minecraft:splash_potion", 14), swiftE, swift, swiftE, true, true, true));
    EXPECT_TRUE(stackMatches(Kind::MilkBucket, item("minecraft:milk_bucket"), {}, item("minecraft:milk_bucket"), {}, true, true, true));
    EXPECT_TRUE(stackMatches(Kind::OminousBottle, item("minecraft:ominous_bottle", 3), {}, item("minecraft:ominous_bottle", 0), {}, true, true, true));
    // matchAll off: only the first filter effect has to be in the stack.
    std::vector<Effect> filter{{effect::Speed, 100, 0}, {effect::Haste, 100, 0}};
    EXPECT_TRUE(effectsMatch({{effect::Speed, 100, 0}}, filter, false, true, true));
    EXPECT_FALSE(effectsMatch({{effect::Speed, 100, 0}}, filter, true, true, true));
}

TEST(AlchemyRules, SettingsJsonAndFilters) {
    auto s = Settings::fromJson(nlohmann::json(), 4);
    ASSERT_EQ(s.filters.size(), 4u);
    EXPECT_TRUE(s.matchAll && s.matchDuration && s.matchAmplifier);
    EXPECT_EQ(s.entityMatch, EntityMatch::PlayersAndEntities);

    auto potion = item("minecraft:potion", 12);
    s.setFilter(1, potion, Condition::OnFire);
    EXPECT_EQ(s.filters[1].condition, Condition::OnFire);
    EXPECT_FLOAT_EQ(s.filters[1].value, -1);
    EXPECT_TRUE(s.containsFilter(potion));
    s.toggleCondition(1); // ON_FIRE -> FALLING
    EXPECT_EQ(s.filters[1].condition, Condition::Falling);
    s.setFilter(1, item("minecraft:potion", 13), Condition::UnderWater); // replacing keeps the condition
    EXPECT_EQ(s.filters[1].condition, Condition::Falling);
    s.setFilter(1, {}, Condition::Never);
    EXPECT_EQ(s.filters[1].condition, Condition::Never);
    EXPECT_TRUE(s.filters[1].filter.empty());
    EXPECT_FALSE(s.toggleCondition(1)); // no filter: no toggling

    s.setFilter(0, item("minecraft:potion", 21), Condition::Hurt);
    EXPECT_FLOAT_EQ(s.filters[0].value, 0.75f);
    EXPECT_TRUE(s.scrollValue(0, 1));
    EXPECT_EQ(percent(s.filters[0].value), 80);
    for (int i = 0; i < 10; ++i) s.scrollValue(0, 1);
    EXPECT_FLOAT_EQ(s.filters[0].value, 1.f);
    for (int i = 0; i < 30; ++i) s.scrollValue(0, -1);
    EXPECT_FLOAT_EQ(s.filters[0].value, 0.f);

    s.matchDuration = false;
    s.entityMatch   = EntityMatch::Entities;
    auto back       = Settings::fromJson(s.toJson(), 8); // basic -> advanced keeps everything, padded
    ASSERT_EQ(back.filters.size(), 8u);
    EXPECT_TRUE(back.filters[0].filter.sameItem(s.filters[0].filter));
    EXPECT_EQ(back.filters[0].condition, Condition::Hurt);
    EXPECT_FALSE(back.matchDuration);
    EXPECT_EQ(back.entityMatch, EntityMatch::Entities);
    EXPECT_TRUE(back.filters[7].filter.empty());
}

TEST(AlchemyRules, EntityMatchAndUseEffects) {
    EXPECT_TRUE(entityMatches(EntityMatch::Players, true));
    EXPECT_FALSE(entityMatches(EntityMatch::Players, false));
    EXPECT_TRUE(entityMatches(EntityMatch::Entities, false));
    EXPECT_FALSE(entityMatches(EntityMatch::Entities, true));
    EXPECT_TRUE(entityMatches(EntityMatch::PlayersAndEntities, true));
    EXPECT_EQ(next(EntityMatch::Players), EntityMatch::PlayersAndEntities);
    EXPECT_EQ(next(EntityMatch::Entities), EntityMatch::Players);
    // 32 tick drink: delay 7, every 4 ticks after that.
    EXPECT_FALSE(shouldTriggerUseEffects(32, 28)); // passed 4
    EXPECT_TRUE(shouldTriggerUseEffects(32, 24));  // passed 8
    EXPECT_FALSE(shouldTriggerUseEffects(32, 23));
    EXPECT_FALSE(shouldTriggerUseEffects(32, 1));
}
