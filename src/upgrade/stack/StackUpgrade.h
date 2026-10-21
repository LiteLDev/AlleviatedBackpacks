#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"

namespace abp::upgrade::stack {

/// `config/stack.json`. Multipliers per tier (Stack upgrades in one backpack multiply each
/// other), the per-backpack limit of the "stack" group and the items stack upgrades never affect.
struct StackConfig {
    int    version        = 1;
    double starterTier    = 1.5;
    double tier1          = 2.0;
    double tier2          = 4.0;
    double tier3          = 8.0;
    double tier4          = 16.0;
    double omegaTier      = 2147483647.0; ///< effectively unlimited (slot limit capped at INT_MAX)
    double downgradeTier1 = 0.125;
    double downgradeTier2 = 0.0625;
    double downgradeTier3 = 0.03125;
    /// How many Stack upgrades / downgrades one backpack can hold together.
    int    maxPerBackpack = 3;
    /// Item ids (`*` / `?` wildcards) that keep their vanilla stack size in upgraded backpacks.
    std::vector<std::string> nonStackableItems = {"minecraft:bundle", "minecraft:*_bundle", "minecraft:*shulker_box"};
};

/// One Stack upgrade / downgrade tier.
struct TierDef {
    std::string_view id;       ///< upgrade id, e.g. "stack_tier_1"
    std::string_view fallback; ///< English name
    double StackConfig::*multiplier;
};

/// A conversion item: used on an installed `from` upgrade it turns it into `to` in place.
struct ConversionDef {
    std::string_view itemId; ///< "alleviatedbackpacks:stack_conversion_tier_1_to_tier_2"
    std::string_view from;   ///< upgrade id
    std::string_view to;     ///< upgrade id
};

[[nodiscard]] std::span<TierDef const>       tiers();
[[nodiscard]] std::span<ConversionDef const> conversions();
[[nodiscard]] std::string                    itemIdOf(std::string_view upgradeId);
[[nodiscard]] TierDef const*                 tierById(std::string_view upgradeId);
[[nodiscard]] TierDef const*                 tierByItem(std::string_view itemId);
[[nodiscard]] ConversionDef const*           conversionByItem(std::string_view itemId);
[[nodiscard]] double                         multiplierOf(TierDef const& tier);

/// False for items stack upgrades must not affect (config `nonStackableItems`).
[[nodiscard]] bool affects(core::ItemStackData const& item);

/// Combined multiplier of the enabled stack upgrades in `backpack`, skipping `skipSlot` (-1 =
/// none) and multiplied by `extra` (a stack upgrade about to be installed; 1 = none).
[[nodiscard]] double totalMultiplier(game::Backpack const& backpack, int skipSlot = -1, double extra = 1.0);

/// Checks that the backpack's contents fit under `multiplier`; fills `reason` (translated for
/// `player`, may be null) when they don't.
[[nodiscard]] bool fits(game::Backpack& backpack, double multiplier, Player* player, std::string& reason);

/// "×2", "×1.5", "÷8" (downgrades).
[[nodiscard]] std::string formatMultiplier(double m);

} // namespace abp::upgrade::stack
