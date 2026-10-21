#pragma once

// Pure decision rules of the Feeding upgrade (no game headers; unit-tested in tests/).

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "core/Filter.h"
#include "core/ItemStackData.h"

namespace abp::upgrade::feeding {

/// When the advanced Feeding upgrade eats a food item.
enum class HungerMode : int {
    Always = 0, ///< whenever any hunger is missing
    Half   = 1, ///< when at least half of the food's nutrition would be used
    Full   = 2, ///< only when all of the food's nutrition would be used (no waste)
};

struct FeedingOptions {
    HungerMode mode        = HungerMode::Full;
    bool       eatWhenHurt = false; ///< eat as soon as any hunger is missing while health is below half
};

/// The player's state relevant to feeding.
struct EaterState {
    int   hunger    = 20; ///< current food level (0..20)
    int   maxHunger = 20;
    float health    = 20.f;
    float maxHealth = 20.f;

    [[nodiscard]] int missing() const { return std::max(0, maxHunger - hunger); }
};

/// Whether a food worth `nutrition` should be eaten now.
[[nodiscard]] inline bool shouldEat(EaterState const& s, int nutrition, FeedingOptions const& o) {
    int missing = s.missing();
    if (missing <= 0) return false;
    if (o.eatWhenHurt && s.maxHealth > 0 && s.health < s.maxHealth / 2.f) return true;
    switch (o.mode) {
    case HungerMode::Always:
        return true;
    case HungerMode::Half:
        return missing * 2 >= nutrition;
    case HungerMode::Full:
    default:
        return missing >= nutrition;
    }
}

/// Foods that are skipped unless explicitly whitelisted: harmful effects (or a possible one, like
/// raw chicken's hunger and suspicious stew's random effect) and chorus fruit's random teleport.
[[nodiscard]] inline std::vector<std::string> defaultHarmfulFoods() {
    return {
        "minecraft:rotten_flesh",
        "minecraft:spider_eye",
        "minecraft:pufferfish",
        "minecraft:poisonous_potato",
        "minecraft:chicken",
        "minecraft:suspicious_stew",
        "minecraft:chorus_fruit",
    };
}

[[nodiscard]] inline bool isHarmful(std::string_view id, std::vector<std::string> const& harmful) {
    return std::find(harmful.begin(), harmful.end(), id) != harmful.end();
}

/// Whether the filter lets the upgrade eat `item`. Harmful foods need an explicit allow-list
/// entry: a ghost of that very item (an empty allow list, a namespace or a tag match don't count).
[[nodiscard]] inline bool
filterAllows(core::Filter const& filter, core::ItemStackData const& item, bool harmful, core::ITagProvider const* tags) {
    if (harmful) {
        if (filter.mode != core::FilterMode::Allow) return false;
        core::Filter byItem = filter;
        byItem.matchBy      = core::MatchBy::Item;
        return byItem.matches(item, tags);
    }
    return filter.passes(item, tags);
}

[[nodiscard]] inline HungerMode hungerModeFromInt(int v) {
    return v == 0 ? HungerMode::Always : v == 1 ? HungerMode::Half : HungerMode::Full;
}

} // namespace abp::upgrade::feeding
