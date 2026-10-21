#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mc/world/item/ItemStack.h"

class Recipe;

namespace abp::upgrade::crafting {

/// Recipe queries against the game's own recipe registry (`Level::getRecipes()`), so data-driven
/// recipes from behavior packs, item tags / ingredient variants and the hardcoded "multi" recipes
/// (map cloning, firework stars, ...) behave exactly like a crafting table / stonecutter.
/// Server thread only; every function returns "nothing" when no level is loaded.

/// Recipe tags used by the vanilla crafting table and stonecutter.
inline constexpr std::string_view kCraftingTag    = "crafting_table";
inline constexpr std::string_view kStonecutterTag = "stonecutter";

/// A matched crafting recipe and what it produces for the current grid.
struct CraftMatch {
    std::shared_ptr<Recipe> recipe; ///< keeps the recipe alive while cached
    std::string             recipeId;
    ItemStack               result;
};

/// A 3x3 crafting grid, row-major (index = row * 3 + column).
using Grid = std::array<ItemStack, 9>;

/// Finds the recipe the crafting table would use for `grid` (shaped, shapeless, multi recipes;
/// the lowest `priority` wins when several match). Scans the registry: cache the result.
[[nodiscard]] std::optional<CraftMatch> matchCrafting(Grid const& grid);

/// One stonecutter choice for an input item.
struct StonecutterOption {
    std::string recipeId;
    ItemStack   result; ///< with the recipe's output count
};

/// Every stonecutter recipe that accepts `input` (one item of it), sorted by result name.
[[nodiscard]] std::vector<StonecutterOption> stonecutterOptions(ItemStack const& input);

/// What stays behind when `consumed` is used up as an ingredient (milk bucket -> bucket,
/// honey bottle -> glass bottle, ...). Empty stack when nothing remains.
[[nodiscard]] ItemStack remainderOf(ItemStack const& consumed);

/// Number of recipes registered under `tag` (selftests / diagnostics).
[[nodiscard]] int recipeCount(std::string_view tag);

} // namespace abp::upgrade::crafting
