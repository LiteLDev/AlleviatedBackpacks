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

/// Vanilla SmithingMenu slot rules: whether some smithing_table recipe takes `stack` as its
/// template (index 0), base (1) or addition (2) ingredient.
enum class Ingredient { Template = 0, Base = 1, Addition = 2 };
[[nodiscard]] bool isIngredient(ItemStack const& stack, Ingredient which);

} // namespace abp::upgrade::smithing
