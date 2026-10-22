#pragma once

// Pure rules of the Alchemy upgrades (Sophisticated Core `upgrades/alchemy`): conditions, item
// kinds, effect matching and the per-upgrade settings. No game types: gtested in
// tests/AlchemyRulesTest.cpp, used by Alchemy.cpp (server) and the native tab (client).

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/ItemStackData.h"

namespace abp::upgrade::alchemy {

// ---- conditions ---------------------------------------------------------------------------------

/// SC `AlchemyCondition`, in SC's order (the toggle button cycles through it).
enum class Condition { Never, Always, UnderWater, OnFire, Falling, Mining, Sprinting, Hurt, NegativeEffect };
inline constexpr int kConditionCount = 9;

inline constexpr std::array<std::string_view, kConditionCount> kConditionNames{
    "never", "always", "under_water", "on_fire", "falling", "mining", "sprinting", "hurt", "negative_effect"};

[[nodiscard]] inline std::string_view conditionName(Condition c) { return kConditionNames[static_cast<std::size_t>(c)]; }
/// Unknown names are `Never` (SC `fromName`).
[[nodiscard]] inline Condition conditionFromName(std::string_view name) {
    for (int i = 0; i < kConditionCount; ++i) {
        if (kConditionNames[static_cast<std::size_t>(i)] == name) return static_cast<Condition>(i);
    }
    return Condition::Never;
}
[[nodiscard]] inline Condition next(Condition c) { return static_cast<Condition>((static_cast<int>(c) + 1) % kConditionCount); }
/// SC: only HURT has a value (health fraction, 0.75); the others use -1.
[[nodiscard]] inline float defaultValue(Condition c) { return c == Condition::Hurt ? 0.75f : -1.f; }

/// What the conditions look at.
struct EntityState {
    bool  underWater    = false; ///< eyes in water
    bool  onFire        = false;
    float fallDistance  = 0;
    bool  mining        = false; ///< a player breaking a block
    bool  sprinting     = false;
    float health        = 20;
    float maxHealth     = 20;
    bool  harmfulEffect = false; ///< has an active harmful effect
};

[[nodiscard]] inline bool test(Condition c, EntityState const& e, float value) {
    switch (c) {
    case Condition::Never:
        return false;
    case Condition::Always:
        return true;
    case Condition::UnderWater:
        return e.underWater;
    case Condition::OnFire:
        return e.onFire;
    case Condition::Falling:
        return e.fallDistance > 2;
    case Condition::Mining:
        return e.mining;
    case Condition::Sprinting:
        return e.sprinting;
    case Condition::Hurt:
        return e.health > 0 && e.health < e.maxHealth && e.health / e.maxHealth < value;
    case Condition::NegativeEffect:
        return e.harmfulEffect;
    }
    return false;
}

// ---- entity match (SC `EntityMatch`) ------------------------------------------------------------

enum class EntityMatch { Players, PlayersAndEntities, Entities };
inline constexpr std::array<std::string_view, 3> kEntityMatchNames{"players", "players_and_entities", "entities"};

[[nodiscard]] inline std::string_view entityMatchName(EntityMatch m) { return kEntityMatchNames[static_cast<std::size_t>(m)]; }
[[nodiscard]] inline EntityMatch      entityMatchFromName(std::string_view name) {
    for (int i = 0; i < 3; ++i) {
        if (kEntityMatchNames[static_cast<std::size_t>(i)] == name) return static_cast<EntityMatch>(i);
    }
    return EntityMatch::PlayersAndEntities;
}
[[nodiscard]] inline EntityMatch next(EntityMatch m) { return static_cast<EntityMatch>((static_cast<int>(m) + 1) % 3); }
[[nodiscard]] inline bool        entityMatches(EntityMatch m, bool isPlayer) {
    return m == EntityMatch::PlayersAndEntities || (m == EntityMatch::Players) == isPlayer;
}

// ---- effects ------------------------------------------------------------------------------------

/// One mob effect (Bedrock `MobEffectIds` numbering, duration in ticks).
struct Effect {
    int id        = 0;
    int duration  = 0;
    int amplifier = 0;
    [[nodiscard]] bool operator==(Effect const&) const = default;
};

namespace effect {
inline constexpr int Speed          = 1;
inline constexpr int Slowness       = 2;
inline constexpr int Haste          = 3;
inline constexpr int Heal           = 6;
inline constexpr int Nausea         = 9;
inline constexpr int Regeneration   = 10;
inline constexpr int Resistance     = 11;
inline constexpr int FireResistance = 12;
inline constexpr int WaterBreathing = 13;
inline constexpr int Hunger         = 17;
inline constexpr int Weakness       = 18;
inline constexpr int Poison         = 19;
inline constexpr int Absorption     = 22;
inline constexpr int SlowFalling    = 27;
inline constexpr int BadOmen        = 28;
} // namespace effect

/// SC `getDefaultConditionForPotion`: by the potion's first effect.
[[nodiscard]] inline Condition defaultConditionForEffects(std::vector<Effect> const& effects) {
    if (effects.empty()) return Condition::Never;
    switch (effects.front().id) {
    case effect::WaterBreathing:
        return Condition::UnderWater;
    case effect::Heal:
    case effect::Regeneration:
        return Condition::Hurt;
    case effect::FireResistance:
        return Condition::OnFire;
    case effect::Speed:
        return Condition::Sprinting;
    case effect::Haste:
        return Condition::Mining;
    case effect::SlowFalling:
        return Condition::Falling;
    default:
        return Condition::Always;
    }
}

/// The effect of `id` an entity has (nullopt = none).
using EffectLookup = std::function<std::optional<Effect>(int id)>;

/// SC `effectPresent`: present, and (when matching the amplifier) at least as strong.
[[nodiscard]] inline bool effectPresent(EffectLookup const& lookup, Effect const& e, bool matchAmplifier) {
    auto have = lookup(e.id);
    return have && (!matchAmplifier || have->amplifier >= e.amplifier);
}

/// SC `shouldApplyPotionEffectsTo` / `shouldApplyFoodEffectsTo`: with `matchAll` ("Any Effect
/// Missing") apply when any effect is missing, else ("All Effects Missing") only when all are.
[[nodiscard]] inline bool shouldApplyEffects(EffectLookup const& lookup, std::vector<Effect> const& effects, bool matchAll, bool matchAmplifier) {
    if (effects.empty()) return false;
    for (auto const& e : effects) {
        bool present = effectPresent(lookup, e, matchAmplifier);
        if (matchAll && !present) return true;
        if (!matchAll && present) return false;
    }
    return !matchAll;
}

/// SC `stackPotionEffectsMatch` / `foodStackPotionEffectsMatch` without the exact-item shortcut:
/// the filter's effects (all of them, or just the first with `matchAll` off) appear in the stack's.
[[nodiscard]] inline bool effectsMatch(
    std::vector<Effect> const& stack,
    std::vector<Effect> const& filter,
    bool                       matchAll,
    bool                       matchDuration,
    bool                       matchAmplifier
) {
    if (stack.empty() || filter.empty()) return false;
    for (auto const& f : filter) {
        bool found = std::any_of(stack.begin(), stack.end(), [&](Effect const& s) {
            return s.id == f.id && (!matchDuration || s.duration == f.duration) && (!matchAmplifier || s.amplifier == f.amplifier);
        });
        if (!found) return false;
        if (!matchAll) return true;
    }
    return true;
}

// ---- items --------------------------------------------------------------------------------------

/// SC's item definitions, in SC's lookup order (the first one accepting a filter item wins).
enum class Kind { None, OminousBottle, SplashPotion, Potion, MilkBucket, GoldenApple, EffectFood };

inline constexpr std::string_view kOminousBottle = "minecraft:ominous_bottle";
inline constexpr std::string_view kSplashPotion  = "minecraft:splash_potion";
inline constexpr std::string_view kPotion        = "minecraft:potion";
inline constexpr std::string_view kMilkBucket    = "minecraft:milk_bucket";
inline constexpr std::string_view kGoldenApple   = "minecraft:golden_apple";
inline constexpr std::string_view kGlassBottle   = "minecraft:glass_bottle";
inline constexpr std::string_view kBucket        = "minecraft:bucket";

/// Effects of vanilla foods that have them (SB's "food with effects": FoodProperties effects),
/// with Bedrock's values. nullptr = no effects.
[[nodiscard]] inline std::vector<Effect> const* foodEffects(std::string_view id) {
    using namespace effect;
    static std::vector<std::pair<std::string_view, std::vector<Effect>>> const kFoods{
        {"minecraft:golden_apple", {{Regeneration, 100, 1}, {Absorption, 2400, 0}}},
        {"minecraft:enchanted_golden_apple", {{Regeneration, 600, 1}, {Absorption, 2400, 3}, {Resistance, 6000, 0}, {FireResistance, 6000, 0}}},
        {"minecraft:rotten_flesh", {{Hunger, 600, 0}}},
        {"minecraft:chicken", {{Hunger, 600, 0}}},
        {"minecraft:spider_eye", {{Poison, 100, 0}}},
        {"minecraft:poisonous_potato", {{Poison, 100, 0}}},
        {"minecraft:pufferfish", {{Poison, 1200, 3}, {Hunger, 300, 2}, {Nausea, 300, 0}}},
    };
    for (auto const& [name, effects] : kFoods) {
        if (name == id) return &effects;
    }
    return nullptr;
}

/// Whether the definition `kind` accepts an item (SC `AlchemyItemDefinition.filter`).
[[nodiscard]] inline bool accepts(Kind kind, std::string_view id) {
    switch (kind) {
    case Kind::OminousBottle:
        return id == kOminousBottle;
    case Kind::SplashPotion:
        return id == kSplashPotion;
    case Kind::Potion:
        return id == kPotion;
    case Kind::MilkBucket:
        return id == kMilkBucket;
    case Kind::GoldenApple:
        return id == kGoldenApple;
    case Kind::EffectFood:
        return foodEffects(id) != nullptr;
    case Kind::None:
        break;
    }
    return false;
}

/// The first definition accepting `id` (Kind::None = not an alchemy item).
[[nodiscard]] inline Kind kindOf(std::string_view id) {
    for (auto k : {Kind::OminousBottle, Kind::SplashPotion, Kind::Potion, Kind::MilkBucket, Kind::GoldenApple, Kind::EffectFood}) {
        if (accepts(k, id)) return k;
    }
    return Kind::None;
}

/// Default condition of a new filter (SC `getDefaultCondition`).
[[nodiscard]] inline Condition defaultCondition(Kind kind, std::vector<Effect> const& effects) {
    switch (kind) {
    case Kind::SplashPotion:
    case Kind::Potion:
        return defaultConditionForEffects(effects);
    case Kind::MilkBucket:
        return Condition::NegativeEffect;
    case Kind::OminousBottle:
    case Kind::GoldenApple:
    case Kind::EffectFood:
        return Condition::Always;
    case Kind::None:
        break;
    }
    return Condition::Never;
}

/// Whether a backpack stack can be used for a filter of definition `kind` (SC `def.filter` +
/// `stackMatches`). Effects are the item effects of the stack / filter.
[[nodiscard]] inline bool stackMatches(
    Kind                       kind,
    core::ItemStackData const& stack,
    std::vector<Effect> const& stackEffects,
    core::ItemStackData const& filter,
    std::vector<Effect> const& filterEffects,
    bool                       matchAll,
    bool                       matchDuration,
    bool                       matchAmplifier
) {
    if (stack.empty() || !accepts(kind, stack.id)) return false;
    switch (kind) {
    case Kind::OminousBottle:
    case Kind::MilkBucket:
        return true; // any ominous bottle / milk bucket
    case Kind::SplashPotion:
    case Kind::Potion:
    case Kind::GoldenApple:
    case Kind::EffectFood:
        if (matchAll && matchDuration && matchAmplifier) return stack.sameItem(filter);
        return effectsMatch(stackEffects, filterEffects, matchAll, matchDuration, matchAmplifier);
    case Kind::None:
        break;
    }
    return false;
}

// ---- settings -----------------------------------------------------------------------------------

/// SC `AlchemyFilterAttribute`.
struct FilterAttribute {
    core::ItemStackData filter;
    Condition           condition = Condition::Never;
    float               value     = -1;
};

/// The settings of one installed alchemy upgrade (persisted in the upgrade's settings JSON; they
/// travel with the item like SB's data components).
struct Settings {
    std::vector<FilterAttribute> filters;
    bool                         matchAll       = true; ///< SC MATCH_ALL_EFFECTS default
    bool                         matchDuration  = true;
    bool                         matchAmplifier = true;
    EntityMatch                  entityMatch    = EntityMatch::PlayersAndEntities;

    /// Reads settings, padded / cut to `slots` filters (SC pads to the configured slot count).
    [[nodiscard]] static Settings fromJson(nlohmann::json const& j, int slots) {
        Settings s;
        auto     flag = [&j](char const* key) {
            auto it = j.find(key);
            return it == j.end() || !it->is_boolean() || it->get<bool>();
        };
        if (j.is_object()) {
            s.matchAll       = flag("matchAll");
            s.matchDuration  = flag("matchDuration");
            s.matchAmplifier = flag("matchAmplifier");
            if (auto it = j.find("entityMatch"); it != j.end() && it->is_string()) {
                s.entityMatch = entityMatchFromName(it->get<std::string>());
            }
            if (auto it = j.find("filters"); it != j.end() && it->is_array()) {
                for (auto const& f : *it) {
                    FilterAttribute a;
                    if (f.is_object()) {
                        try {
                            if (auto item = f.find("item"); item != f.end() && item->is_object()) {
                                a.filter = core::ItemStackData::fromJson(*item);
                            }
                        } catch (...) {
                            a.filter = {};
                        }
                        if (auto c = f.find("condition"); c != f.end() && c->is_string()) a.condition = conditionFromName(c->get<std::string>());
                        auto v  = f.find("value");
                        a.value = v != f.end() && v->is_number() ? v->get<float>() : defaultValue(a.condition);
                    }
                    s.filters.push_back(std::move(a));
                }
            }
        }
        if (slots >= 0) s.filters.resize(static_cast<std::size_t>(slots));
        return s;
    }

    [[nodiscard]] nlohmann::json toJson() const {
        auto arr = nlohmann::json::array();
        for (auto const& f : filters) {
            arr.push_back({
                {"item", f.filter.empty() ? nlohmann::json(nullptr) : f.filter.withCount(1).toJson()},
                {"condition", std::string(conditionName(f.condition))},
                {"value", f.value},
            });
        }
        return {
            {"filters", std::move(arr)},
            {"matchAll", matchAll},
            {"matchDuration", matchDuration},
            {"matchAmplifier", matchAmplifier},
            {"entityMatch", std::string(entityMatchName(entityMatch))},
        };
    }

    /// True if the same item is already a filter (SC `doesNotContain`).
    [[nodiscard]] bool containsFilter(core::ItemStackData const& item) const {
        return std::any_of(filters.begin(), filters.end(), [&](FilterAttribute const& f) {
            return !f.filter.empty() && f.filter.sameItem(item);
        });
    }

    /// SC `setFilter`: a new filter in an empty slot gets its default condition and value, a
    /// cleared slot goes back to NEVER, replacing a filter keeps the condition.
    void setFilter(int slot, core::ItemStackData const& item, Condition defaultForItem) {
        if (slot < 0 || slot >= static_cast<int>(filters.size())) return;
        auto& a = filters[static_cast<std::size_t>(slot)];
        if (a.filter.empty()) {
            a.condition = defaultForItem;
            a.value     = defaultValue(defaultForItem);
        } else if (item.empty()) {
            a.condition = Condition::Never;
            a.value     = -1;
        }
        a.filter = item.empty() ? core::ItemStackData{} : item.withCount(1);
    }

    /// SC `toggleCondition`: next condition with its default value (only for set filters).
    bool toggleCondition(int slot) {
        if (slot < 0 || slot >= static_cast<int>(filters.size())) return false;
        auto& a = filters[static_cast<std::size_t>(slot)];
        if (a.filter.empty()) return false;
        a.condition = next(a.condition);
        a.value     = defaultValue(a.condition);
        return true;
    }

    /// SC scroll on a HURT condition: +-5%, clamped to [0, 1].
    bool scrollValue(int slot, int direction) {
        if (slot < 0 || slot >= static_cast<int>(filters.size()) || direction == 0) return false;
        auto& a = filters[static_cast<std::size_t>(slot)];
        if (a.filter.empty() || a.condition != Condition::Hurt) return false;
        a.value = direction > 0 ? std::min(a.value + 0.05f, 1.f) : std::max(a.value - 0.05f, 0.f);
        return true;
    }
};

/// The HURT threshold as SC shows it (`Math.round(value * 100) + "%"`).
[[nodiscard]] inline int percent(float value) { return static_cast<int>(std::lround(value * 100)); }

// ---- application timing (SC AlchemyUpgradeWrapper.tick) -------------------------------------------

inline constexpr int kCheckInterval = 5; ///< ticks between checks
inline constexpr int kCheckRadius   = 3; ///< placed backpacks: entities within this many blocks

/// SC `shouldTriggerItemUseEffects`: use sounds every 4 ticks once 21.875% of the use time passed.
[[nodiscard]] inline bool shouldTriggerUseEffects(int useDuration, int remaining) {
    if (remaining < 2) return false;
    int passed = useDuration - remaining;
    int delay  = static_cast<int>(static_cast<float>(useDuration) * 0.21875f);
    return passed > delay && remaining % 4 == 0;
}

} // namespace abp::upgrade::alchemy
