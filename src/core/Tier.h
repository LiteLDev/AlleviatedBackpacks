#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace abp::core {

/// Backpack tiers, in upgrade order. The numeric value is persisted: append only.
enum class Tier : int {
    Leather   = 0,
    Copper    = 1,
    Iron      = 2,
    Gold      = 3,
    Diamond   = 4,
    Netherite = 5,
};

struct TierInfo {
    Tier             tier;
    std::string_view id;           ///< "leather", "copper", ... (item id is "alleviatedbackpacks:<id>_backpack")
    int              slots;        ///< storage slots
    int              upgradeSlots; ///< upgrade slots
};

inline constexpr std::array<TierInfo, 6> kTiers{{
    {Tier::Leather,   "leather",   27,  1},
    {Tier::Copper,    "copper",    45,  1},
    {Tier::Iron,      "iron",      54,  2},
    {Tier::Gold,      "gold",      81,  3},
    {Tier::Diamond,   "diamond",   108, 5},
    {Tier::Netherite, "netherite", 120, 7},
}};

[[nodiscard]] constexpr TierInfo const& tierInfo(Tier t) { return kTiers[static_cast<std::size_t>(t)]; }

[[nodiscard]] std::optional<Tier> tierFromId(std::string_view id);
[[nodiscard]] std::optional<Tier> tierFromInt(int value);

/// The next tier (Leather -> Copper -> ...), or nullopt for Netherite.
[[nodiscard]] std::optional<Tier> nextTier(Tier t);

} // namespace abp::core
