#include "upgrade/smithing/SmithingLogic.h"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core/utility/NonOwnerPointer.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/crafting/CraftingContext.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/RecipeIngredient.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/crafting/SmithingTransformRecipe.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"

namespace abp::upgrade::smithing {

SmithingOutcome compute(ItemStack const& templ, ItemStack const& base, ItemStack const& addition) {
    SmithingOutcome out;
    if (templ.isNull() || base.isNull() || addition.isNull()) return out;
    auto level = ll::service::getLevel();
    if (!level) return out;

    // The smithing table's crafting grid: template, base, addition in one row.
    CraftingContainer grid(3, 1);
    ItemStack         one[3] = {templ, base, addition};
    for (int i = 0; i < 3; ++i) {
        one[i].set(1);
        grid.setItem(i, one[i]);
    }
    CraftingContext craft(
        Bedrock::NotNullNonOwnerPtr<ILevel>(Bedrock::NonOwnerPointer<ILevel>(static_cast<ILevel&>(*level)))
    );

    level->getRecipes().forEachRecipeFor(HashedString("smithing_table"), [&](Recipe const& recipe) {
        if (!out.result.isNull() || !recipe.matches(grid, craft)) return;
        auto const& results = recipe.assemble(grid, craft);
        if (results.empty() || results.front().isNull()) return;
        ItemStack result;
        static_cast<ItemStackBase&>(result) = results.front();
        // Netherite-style upgrades keep the base item's data (enchantments, damage, name, trim).
        if (recipe.getId() == SmithingTransformRecipe::ID() && !result.mUserData && base.mUserData) {
            result.setUserData(base.mUserData->clone());
        }
        out.result   = std::move(result);
        out.recipeId = recipe.mRecipeId;
    });
    return out;
}

bool isTemplate(ItemStack const& stack) {
    return !stack.isNull() && stack.getTypeName().ends_with("_smithing_template");
}

bool isIngredient(ItemStack const& stack, Ingredient which) {
    if (stack.isNull()) return false;
    auto level = ll::service::getLevel();
    if (!level) return false;
    auto index = static_cast<std::size_t>(which);
    bool found = false;
    bool any   = false;
    level->getRecipes().forEachRecipeFor(HashedString("smithing_table"), [&](Recipe const& recipe) {
        auto const& ingredients = recipe.mMyIngredients.get();
        if (found || ingredients.size() != 3) return;
        any   = true;
        found = recipe.itemValidForRecipe(ingredients[index], stack);
    });
    if (any) return found;
    // No recipe data (should not happen): templates by name, gear as the base, the rest as addition.
    switch (which) {
    case Ingredient::Template:
        return isTemplate(stack);
    case Ingredient::Base:
        return !isTemplate(stack) && stack.isDamageableItem();
    default:
        return !isTemplate(stack) && !stack.isDamageableItem();
    }
}

} // namespace abp::upgrade::smithing
