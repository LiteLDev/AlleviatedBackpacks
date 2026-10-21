#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "upgrade/IUpgrade.h"
#include "upgrade/feeding/FeedingRules.h"

class Player;

namespace abp::upgrade::feeding {

/// `config/feeding.json`.
struct FeedingConfig {
    int version = 1;
    /// Foods the upgrade only eats when they are explicitly on its allow list (harmful effects).
    std::vector<std::string> harmfulFoods = defaultHarmfulFoods();
};

inline constexpr int kBasicFilterSlots    = 9;
inline constexpr int kAdvancedFilterSlots = 16;

/// Settings keys (in the upgrade's settings JSON).
inline constexpr char const* kFilterKey      = "filter";
inline constexpr char const* kHungerModeKey  = "hungerMode";
inline constexpr char const* kEatWhenHurtKey = "eatWhenHurt";

/// Defaults of the advanced upgrade.
inline constexpr HungerMode kAdvancedDefaultMode = HungerMode::Half;
inline constexpr bool       kAdvancedDefaultHurt = true;

/// The feeding options of an installed (basic or advanced) feeding upgrade.
[[nodiscard]] FeedingOptions optionsOf(UpgradeContext const& ctx, bool advanced);
[[nodiscard]] core::Filter   filterOf(UpgradeContext const& ctx, bool advanced);

/// Food level / health of a player.
[[nodiscard]] EaterState eaterState(Player& player);
/// Sets the player's food level (selftests).
void setHunger(Player& player, float value);
/// Sets the player's health (selftests).
void setHealth(Player& player, float value);

/// Nutrition of a food item, nullopt for non-food.
[[nodiscard]] std::optional<int> nutritionOf(core::ItemStackData const& item);

/// Eats at most one food from the backpack if the rules say so. Returns the backpack slot eaten
/// from, or -1.
int tryFeed(Player& player, UpgradeContext const& ctx, FeedingOptions const& options, core::Filter const& filter);

} // namespace abp::upgrade::feeding
