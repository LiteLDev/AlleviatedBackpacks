#pragma once

#include <limits>
#include <string_view>

namespace abp::upgrade::pickup {

inline constexpr std::string_view kBasicId    = "pickup";
inline constexpr std::string_view kAdvancedId = "advanced_pickup";
/// Sophisticated Backpacks doesn't limit how many pickup upgrades a backpack holds; each one
/// picks up what its own filter accepts.
inline constexpr int kMaxPerBackpack = std::numeric_limits<int>::max();

} // namespace abp::upgrade::pickup
