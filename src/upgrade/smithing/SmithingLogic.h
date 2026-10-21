#pragma once

#include <string>

#include "mc/world/item/ItemStack.h"

namespace abp::upgrade::smithing {

struct SmithingOutcome {
    ItemStack   result;   ///< empty: no matching recipe
    std::string recipeId; ///< e.g. "minecraft:smithing_netherite_sword"

    [[nodiscard]] bool hasResult() const { return !result.isNull(); }
};

/// Runs the game's own smithing-table recipes (tag "smithing_table": netherite upgrades, armor
/// trims and any data-driven ones) on one template + base + addition.
[[nodiscard]] SmithingOutcome compute(ItemStack const& templ, ItemStack const& base, ItemStack const& addition);

/// Whether the item looks like a smithing template (used to route shift-clicks).
[[nodiscard]] bool isTemplate(ItemStack const& stack);

} // namespace abp::upgrade::smithing
