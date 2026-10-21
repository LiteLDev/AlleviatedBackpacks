#pragma once

#include <limits>
#include <string>
#include <string_view>

#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::filter {

inline constexpr std::string_view kBasicId    = "filter";
inline constexpr std::string_view kAdvancedId = "advanced_filter";
/// Sophisticated Backpacks doesn't limit how many filter upgrades a backpack holds.
inline constexpr int kMaxPerBackpack = std::numeric_limits<int>::max();

/// Settings key of the direction.
inline constexpr std::string_view kDirectionKey = "direction";
/// Control-row slot of the direction button in the settings menu.
inline constexpr int kDirectionButton = 5;

/// Which automated transfers the filter checks.
enum class Direction : int {
    Both   = 0, ///< inserts and extracts
    Input  = 1, ///< only what automation puts into the backpack
    Output = 2, ///< only what automation takes out of the backpack
};

[[nodiscard]] Direction   directionOf(UpgradeContext const& ctx);
void                      setDirection(UpgradeContext const& ctx, Direction direction);
[[nodiscard]] std::string directionName(Player& player, Direction direction);

} // namespace abp::upgrade::filter
