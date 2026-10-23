#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"

namespace abp::upgrade::stack {

/// `config/stack.json`. Multipliers per tier (Stack upgrades in one backpack multiply each
/// other) and the items stack upgrades never affect. The per-backpack limit is SB's
/// `maxUpgradesPerStorage` (`stack_upgrades|3`, admin unit).
struct StackConfig {
    int    version        = 3;
    double starterTier    = 1.5;
    double tier1          = 2.0;
    double tier2          = 4.0;
    double tier3          = 8.0;
    double tier4          = 16.0;
    double omegaTier      = 2147483647.0; ///< SB: Integer.MAX_VALUE (slot limit capped at INT_MAX)
    double downgradeTier1 = 0.125;
    double downgradeTier2 = 0.0625;
    double downgradeTier3 = 0.03125;
    /// Item ids (`*` / `?` wildcards) that keep their vanilla stack size in upgraded backpacks
    /// (SC `stackUpgrade.nonStackableItems`: the bundle and the 17 shulker boxes).
    std::vector<std::string> nonStackableItems = {
        "minecraft:bundle",
        "minecraft:undyed_shulker_box",
        "minecraft:white_shulker_box",
        "minecraft:orange_shulker_box",
        "minecraft:magenta_shulker_box",
        "minecraft:light_blue_shulker_box",
        "minecraft:yellow_shulker_box",
        "minecraft:lime_shulker_box",
        "minecraft:pink_shulker_box",
        "minecraft:gray_shulker_box",
        "minecraft:light_gray_shulker_box",
        "minecraft:cyan_shulker_box",
        "minecraft:purple_shulker_box",
        "minecraft:blue_shulker_box",
        "minecraft:brown_shulker_box",
        "minecraft:green_shulker_box",
        "minecraft:red_shulker_box",
        "minecraft:black_shulker_box",
    };

    /// Version 2 had `maxPerBackpack` (the "stack" group limit). SB keeps that limit in
    /// `maxUpgradesPerStorage` (`stack_upgrades|3`): a changed value is handed to the admin unit,
    /// which moves it there (`legacyStackLimit`).
    template <class J>
    static void migrate(J& data) {
        if (!data.is_object() || !data.contains("maxPerBackpack")) return;
        if (data["maxPerBackpack"].is_number_integer()) {
            int old = data["maxPerBackpack"].template get<int>();
            if (old != 3) legacyStackLimit() = old;
        }
        data.erase("maxPerBackpack");
    }
    /// A `maxPerBackpack` other than SB's 3 read from a version 2 `config/stack.json`, until the
    /// admin unit has moved it into `maxUpgradesPerStorage`.
    static std::optional<int>& legacyStackLimit() {
        static std::optional<int> value;
        return value;
    }
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

/// The slot limit (for 64-stack items) of the enabled stack upgrades in `backpack`, skipping
/// `skipSlot` and multiplied by `extra` (SC `StackUpgradeItem.getInventorySlotLimit`).
[[nodiscard]] int slotLimit(game::Backpack const& backpack, int skipSlot = -1, double extra = 1.0);

/// The stack multiplier the contents of the installed upgrade in `slot` need (SC
/// `IStackableContentsUpgrade.getMinimumMultiplierRequired`: tanks and batteries, through their
/// capability handlers). 0 when it needs none.
[[nodiscard]] int minimumMultiplierRequired(std::shared_ptr<game::Backpack> const& backpack, int slot);

/// SC `isMultiplierHighEnough`: checks that the backpack's storage slots and stackable-contents
/// upgrades (except the one in `ignoreUpgradeSlot`) fit under `multiplier`; fills `reason`
/// (translated for `player`, may be null) and `errorUpgradeSlots` when they don't.
[[nodiscard]] bool fits(
    std::shared_ptr<game::Backpack> const& backpack,
    double                                 multiplier,
    int                                    ignoreUpgradeSlot,
    Player*                                player,
    std::string&                           reason,
    std::vector<int>*                      errorUpgradeSlots = nullptr
);

/// "×2", "×1.5", "÷8" (downgrades).
[[nodiscard]] std::string formatMultiplier(double m);

} // namespace abp::upgrade::stack
