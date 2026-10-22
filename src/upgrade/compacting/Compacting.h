#pragma once

// Compacting upgrade (basic: 2x2; advanced: 3x3 then 2x2), as Sophisticated Core's
// CompactingUpgradeWrapper. See Compacting.cpp.

#include <optional>
#include <string>
#include <vector>

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/compacting/CompactingRecipes.h"

namespace abp::upgrade::compacting {

inline constexpr std::string_view kBasicId    = "compacting";
inline constexpr std::string_view kAdvancedId = "advanced_compacting";

/// Settings keys.
inline constexpr std::string_view kFilterKey           = "filter";
inline constexpr std::string_view kCompactAnythingKey  = "compactNonUncraftable"; ///< SC COMPACT_NON_UNCRAFTABLE (default false)
inline constexpr std::string_view kWorkInGuiKey        = "workInGui";             ///< SC SHOULD_WORK_IN_GUI (default false)

/// SB `CompactingUpgradeConfig` (`compactingUpgrade`; its shapes apply to both tiers, as in SB).
struct CompactingBasicConfig {
    int filterSlots = 9; ///< Number of filter slots (1..20)
    int slotsInRow  = 3; ///< Number of filter slots displayed in a row (1..6)
    /// Additional crafting shapes compacting will try ("111/101/111" = hollow 3x3 of 8 items).
    std::vector<std::string> additionalCompactingShapes = {"111/101/111"};
    /// Item-specific additional shapes: "mod:item=shape", e.g. "some_mod:tiny_coal=111/101/111".
    std::vector<std::string> additionalCompactingShapeOverrides = {};
};
/// SB `FilteredUpgradeConfig` (`advancedCompactingUpgrade`).
struct CompactingAdvancedConfig {
    int filterSlots = 16;
    int slotsInRow  = 4;
};

/// `config/compacting.json`.
struct CompactingConfig {
    int                      version                   = 2;
    CompactingBasicConfig    compactingUpgrade         = {};
    CompactingAdvancedConfig advancedCompactingUpgrade = {};
};

[[nodiscard]] int filterSlots(bool advanced);
[[nodiscard]] int slotsInRow(bool advanced);

/// Settings of one installed compacting upgrade.
struct CompactingSettings {
    core::Filter filter;
    bool         advanced        = false;
    bool         compactAnything = false; ///< "Compact Anything" (else only uncraftable)
    bool         workInGui       = false;

    [[nodiscard]] static CompactingSettings read(nlohmann::json const& settings, bool advanced);
};

/// SC `CompactingDefinition`: what one craft of `item` makes and how many it takes.
struct CompactingDefinition {
    core::ItemStackData result;
    int                 count = 0;
};

/// SC `getCompactingDefinition(stack, upgradeItem, shouldCompactNonUncraftable)`.
[[nodiscard]] std::optional<CompactingDefinition> compactingDefinition(core::ItemStackData const& item, bool advanced, bool compactAnything);

} // namespace abp::upgrade::compacting
