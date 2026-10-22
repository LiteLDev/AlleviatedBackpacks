#include "upgrade/crafting/RecipeLookup.h"

#include <algorithm>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/utility/NonOwnerPointer.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/crafting/CraftingContext.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"

#include "game/ItemConv.h"

namespace abp::upgrade::crafting {

namespace {

using RecipeMap = std::map<std::string, std::shared_ptr<Recipe>>;

/// The recipes registered under `tag`, or nullptr.
RecipeMap const* recipesFor(Level& level, std::string_view tag) {
    auto& all = *level.getRecipes().mRecipes;
    for (auto const& [key, map] : all) {
        if (key.getString() == tag) return &map;
    }
    return nullptr;
}

Bedrock::NotNullNonOwnerPtr<ILevel> levelPtr(Level& level) {
    return Bedrock::NotNullNonOwnerPtr<ILevel>(Bedrock::NonOwnerPointer<ILevel>(static_cast<ILevel const&>(level)));
}

ItemStack toStack(ItemInstance const& inst) {
    if (inst.isNull() || inst.mCount == 0) return {};
    auto tag = inst.save(SaveContext{SaveContext::SaveUseCase::SaveToDisk});
    if (!tag) return {};
    return ItemStack::fromTag(*tag);
}

/// First result of `recipe` for `container`, or an empty stack.
ItemStack assembleFirst(Recipe const& recipe, CraftingContainer& container, CraftingContext& context) {
    auto const& results = recipe.assemble(container, context);
    if (results.empty()) return {};
    return toStack(results.front());
}

} // namespace

std::vector<CraftMatch> matchAllCrafting(Grid const& grid) {
    std::vector<CraftMatch> out;
    if (std::all_of(grid.begin(), grid.end(), [](ItemStack const& s) { return s.isNull() || s.mCount == 0; })) return out;
    auto level = ll::service::getLevel();
    if (!level) return out;
    CraftingContainer container(3, 3);
    for (int i = 0; i < 9; ++i) {
        auto const& s = grid[static_cast<std::size_t>(i)];
        container.setItem(i, s.isNull() ? ItemStack::EMPTY_ITEM() : s);
    }
    CraftingContext context(levelPtr(*level));
    auto const*     recipes = recipesFor(*level, kCraftingTag);
    if (!recipes) return out;
    std::vector<std::pair<int, std::shared_ptr<Recipe>>> matched;
    for (auto const& [id, recipe] : *recipes) {
        if (!recipe || !recipe->matches(container, context)) continue;
        matched.emplace_back(recipe->mPriority, recipe);
    }
    std::stable_sort(matched.begin(), matched.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    for (auto const& [prio, recipe] : matched) {
        auto result = assembleFirst(*recipe, container, context);
        if (result.isNull()) continue;
        // Bedrock registers some recipes more than once (e.g. per ingredient variant): offer each
        // distinct result once, like the Java recipe book does.
        bool dup = std::any_of(out.begin(), out.end(), [&](CraftMatch const& m) {
            return m.result.mCount == result.mCount && game::ItemConv::toData(m.result).sameItem(game::ItemConv::toData(result));
        });
        if (!dup) out.push_back(CraftMatch{recipe, recipe->mRecipeId, std::move(result)});
    }
    return out;
}

std::vector<StonecutterOption> stonecutterOptions(ItemStack const& input) {
    std::vector<StonecutterOption> out;
    if (input.isNull() || input.mCount == 0) return out;
    auto level = ll::service::getLevel();
    if (!level) return out;
    auto const* recipes = recipesFor(*level, kStonecutterTag);
    if (!recipes) return out;
    CraftingContainer container(3, 3);
    ItemStack         one = input;
    one.set(1);
    container.setItem(0, one);
    CraftingContext context(levelPtr(*level));
    for (auto const& [id, recipe] : *recipes) {
        if (!recipe || !recipe->matches(container, context)) continue;
        auto result = assembleFirst(*recipe, container, context);
        if (result.isNull()) continue;
        out.push_back({recipe->mRecipeId, std::move(result)});
    }
    std::stable_sort(out.begin(), out.end(), [](StonecutterOption const& a, StonecutterOption const& b) {
        auto an = a.result.getTypeName(), bn = b.result.getTypeName();
        if (an != bn) return an < bn;
        if (a.result.getAuxValue() != b.result.getAuxValue()) return a.result.getAuxValue() < b.result.getAuxValue();
        return a.result.mCount < b.result.mCount;
    });
    return out;
}

ItemStack remainderOf(ItemStack const& consumed) {
    if (consumed.isNull()) return {};
    if (auto* item = consumed.mItem.get()) {
        if (Item const* rest = item->mCraftingRemainingItem) {
            auto s = game::ItemConv::make(rest->getSerializedName(), 1);
            if (!s.isNull()) return s;
        }
    }
    // Vanilla container items, in case the game doesn't set `mCraftingRemainingItem` for them.
    static constexpr std::pair<std::string_view, std::string_view> kVanilla[] = {
        {"minecraft:milk_bucket",        "minecraft:bucket"      },
        {"minecraft:water_bucket",       "minecraft:bucket"      },
        {"minecraft:lava_bucket",        "minecraft:bucket"      },
        {"minecraft:powder_snow_bucket", "minecraft:bucket"      },
        {"minecraft:honey_bottle",       "minecraft:glass_bottle"},
    };
    auto id = consumed.getTypeName();
    for (auto const& [from, to] : kVanilla) {
        if (id == from) return game::ItemConv::make(to, 1);
    }
    return {};
}

int recipeCount(std::string_view tag) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    auto const* recipes = recipesFor(*level, tag);
    return recipes ? static_cast<int>(recipes->size()) : 0;
}

} // namespace abp::upgrade::crafting
