#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/feeding/FeedingRules.h"

class Player;

namespace abp::upgrade::feeding {

/// SB `FilteredUpgradeConfig` of one tier.
struct FeedingTierConfig {
    int filterSlots = 9; ///< Number of filter slots (1..20)
    int slotsInRow  = 3; ///< Number of filter slots displayed in a row (1..6)
};

/// `config/feeding.json` (SB server config sections `feedingUpgrade` / `advancedFeedingUpgrade`).
struct FeedingConfig {
    int               version                = 2;
    FeedingTierConfig feedingUpgrade         = {9, 3};
    FeedingTierConfig advancedFeedingUpgrade = {16, 4};
};

[[nodiscard]] FeedingTierConfig tierConfig(bool advanced);

inline constexpr std::string_view kBasicId    = "feeding";
inline constexpr std::string_view kAdvancedId = "advanced_feeding";

inline constexpr int kCooldown             = 100; ///< SC COOLDOWN
inline constexpr int kStillHungryCooldown  = 10;  ///< SC STILL_HUNGRY_COOLDOWN
inline constexpr int kFeedingRange         = 3;   ///< SC FEEDING_RANGE (placed backpacks)

/// Settings keys (in the upgrade's settings JSON).
inline constexpr char const* kFilterKey      = "filter";
inline constexpr char const* kHungerLevelKey = "feedAtHungerLevel";       ///< "any" | "half" | "full"
inline constexpr char const* kWhenHurtKey    = "feedImmediatelyWhenHurt"; ///< bool

/// The feeding options of an installed feeding upgrade (both tiers; the basic one has no UI for
/// them and uses SB's defaults HALF / true).
[[nodiscard]] FeedingOptions optionsOf(UpgradeContext const& ctx);
/// SC FilterLogic: a block list by default (an empty one matches every food).
[[nodiscard]] core::Filter filterOf(UpgradeContext const& ctx, bool advanced);
/// Stores one settings value (feeding registers no inventory hooks, so no hook rebuild).
void writeSetting(UpgradeContext const& ctx, char const* key, nlohmann::json value);

/// Food level / health of a player.
[[nodiscard]] EaterState eaterState(Player& player);
/// Sets the player's food level (selftests).
void setHunger(Player& player, float value);
/// Sets the player's health (selftests).
void setHealth(Player& player, float value);

/// Nutrition of a food item, nullopt for non-food.
[[nodiscard]] std::optional<int> nutritionOf(core::ItemStackData const& item);

/// SC `feedPlayerAndGetHungry`: feeds at most one food from the backpack. Returns true when the
/// player was fed and is still hungry (fast cooldown). `fedSlot` gets the slot eaten from (-1).
bool feedPlayerAndGetHungry(Player& player, UpgradeContext const& ctx, bool advanced, int* fedSlot = nullptr);

} // namespace abp::upgrade::feeding
