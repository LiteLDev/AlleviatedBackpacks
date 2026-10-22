#include "upgrade/compacting/CompactingRecipes.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/RecipeIngredient.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"

#include "game/ItemConv.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::upgrade::compacting {

namespace {

constexpr std::string_view kCraftingTag = "crafting_table";

std::atomic<std::uint64_t> gGeneration{1}; // bumped from any thread when recipes reload

struct Cache {
    std::uint64_t                                                generation = 0;
    Recipes const*                                               recipes    = nullptr;
    std::unordered_map<std::string, std::optional<Compaction>>  entries;
    std::unordered_map<std::string, bool>                       uncompact; ///< uncompactMatches
    std::unordered_map<std::string, CompactingShapes>           shapes;    ///< compactingShapes
};
Cache gCache;

std::string cacheKey(core::ItemStackData const& item, Shape const& shape) {
    return item.id + '|' + std::to_string(item.aux) + '|' + std::to_string(item.nbtHash()) + '|' + std::to_string(shape.width) + 'x'
         + std::to_string(shape.height) + ':' + std::to_string(shape.filled);
}

/// One-count game stack of a recipe result, keeping block states.
core::ItemStackData toData(ItemInstance const& inst) {
    if (inst.isNull()) return {};
    if (auto tag = inst.save(SaveContext{SaveContext::SaveUseCase::SaveToDisk})) {
        tag->putByte("Count", static_cast<uchar>(1));
        auto stack = ItemStack::fromTag(*tag);
        if (!stack.isNull()) return game::ItemConv::toData(stack);
    }
    return game::ItemConv::toData(game::ItemConv::make(inst.getTypeName(), 1, inst.getAuxValue()));
}

/// The single result of a plain crafting recipe, or nullptr.
ItemInstance const* singleResult(Recipe const& recipe) {
    if (recipe.isMultiRecipe()) return nullptr; // data-driven results are fine: all JSON recipes have them
    auto const& results = recipe.getResultItems();
    if (results.size() != 1 || results.front().isNull() || results.front().mCount < 1) return nullptr;
    return &results.front();
}

bool sameKind(core::ItemStackData const& a, core::ItemStackData const& b) { return a.id == b.id && a.aux == b.aux; }

/// True if `recipe` crafts `stack` laid out as `shape` (shaped: same size, filled cells exactly
/// where the shape has them, possibly mirrored; shapeless: `shape.count` ingredients).
bool matchesShape(Recipe const& recipe, Shape const& shape, ItemStack const& stack) {
    if (recipe.isShapeless()) {
        int n = 0;
        for (auto const& ing : *recipe.mMyIngredients) {
            if (ing.isNull()) continue;
            if (!recipe.itemValidForRecipe(ing, stack)) return false;
            n += std::max<int>(ing.mStackSize, 1);
        }
        return n == shape.count;
    }
    int w = recipe.mWidth;
    int h = recipe.mHeight;
    if (w != shape.width || h != shape.height) return false;
    for (int mirror = 0; mirror < 2; ++mirror) {
        bool ok = true;
        for (int y = 0; y < h && ok; ++y) {
            for (int x = 0; x < w && ok; ++x) {
                auto const& ing    = recipe.getIngredient(mirror ? w - 1 - x : x, y);
                bool        filled = shape.isFilled(x, y);
                if (ing.isNull()) ok = !filled;
                else ok = filled && ing.mStackSize <= 1 && recipe.itemValidForRecipe(ing, stack);
            }
        }
        if (ok) return true;
    }
    return false;
}

/// True if one `result` crafts (1x1) back into exactly `count` items of `original`.
bool isReversible(Recipes const& recipes, core::ItemStackData const& result, core::ItemStackData const& original, int count) {
    auto resultStack = game::ItemConv::toStack(result, 1);
    if (resultStack.isNull()) return false;
    bool found = false;
    recipes.forEachRecipeFor(HashedString(kCraftingTag), [&](Recipe const& recipe) {
        if (found) return;
        auto const* out = singleResult(recipe);
        if (!out || out->mCount != count) return;
        RecipeIngredient const* only = nullptr;
        int                     n    = 0;
        if (recipe.isShapeless()) {
            for (auto const& ing : *recipe.mMyIngredients) {
                if (ing.isNull()) continue;
                only  = &ing;
                n    += std::max<int>(ing.mStackSize, 1);
            }
        } else {
            for (int y = 0; y < recipe.mHeight; ++y) {
                for (int x = 0; x < recipe.mWidth; ++x) {
                    auto const& ing = recipe.getIngredient(x, y);
                    if (ing.isNull()) continue;
                    only  = &ing;
                    n    += std::max<int>(ing.mStackSize, 1);
                }
            }
        }
        if (n != 1 || !only || !recipe.itemValidForRecipe(*only, resultStack)) return;
        if (!sameKind(toData(*out), original)) return;
        found = true;
    });
    return found;
}

std::optional<Compaction> lookup(Recipes const& recipes, core::ItemStackData const& item, Shape const& shape) {
    auto stack = game::ItemConv::toStack(item, 1);
    if (stack.isNull() || stack.mUserData || item.damage > 0) return std::nullopt;
    // SC: one matching recipe -> it; several -> the first whose result crafts back into the item,
    // else the first.
    std::optional<Compaction> first;
    std::optional<Compaction> reversible;
    recipes.forEachRecipeFor(HashedString(kCraftingTag), [&](Recipe const& recipe) {
        if (reversible) return;
        auto const* out = singleResult(recipe);
        if (!out || !matchesShape(recipe, shape, stack)) return;
        auto result = toData(*out);
        if (result.empty()) return;
        result.count = out->mCount;
        bool       rev = isReversible(recipes, result.withCount(1), item, shape.count);
        Compaction c{result, shape.count, rev};
        if (rev) reversible = c;
        else if (!first) first = c;
    });
    return reversible ? reversible : first;
}

void dropCache() {
    gCache.entries.clear();
    gCache.uncompact.clear();
    gCache.shapes.clear();
    gCache.recipes = nullptr;
}

// Recipes are (re)loaded through Recipes::init (world load, data reload): drop the cache then.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable" // _AutoHookCount inside the macro
#endif
LL_TYPE_INSTANCE_HOOK(
    CompactingRecipesInitHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::init,
    void,
    ResourcePackManager&     resourcePackManager,
    ExternalRecipeStore&     recipeStorage,
    BaseGameVersion const&   baseGameVersion,
    Experiments const&       experiments
) {
    origin(resourcePackManager, recipeStorage, baseGameVersion, experiments);
    gGeneration.fetch_add(1);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

class CompactingRecipesModule {
public:
    void install() {
        CompactingRecipesInitHook::hook();
        mStarted = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerStartedEvent>([](auto&) {
            gGeneration.fetch_add(1);
        });
        gGeneration.fetch_add(1);
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mStarted);
        CompactingRecipesInitHook::unhook();
        dropCache();
    }

private:
    ll::event::ListenerPtr mStarted;
};

} // namespace

namespace {
/// The current recipes with a cache that belongs to them (nullptr outside a world).
Recipes const* currentRecipes() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Recipes const& recipes = level->getRecipes();
    auto           gen     = gGeneration.load();
    if (gCache.generation != gen || gCache.recipes != &recipes) {
        dropCache();
        gCache.generation = gen;
        gCache.recipes    = &recipes;
    }
    return &recipes;
}
} // namespace

std::optional<Compaction> findCompaction(core::ItemStackData const& item, Shape const& shape) {
    if (item.empty() || shape.count < 1) return std::nullopt;
    auto const* rp = currentRecipes();
    if (!rp) return std::nullopt;
    Recipes const& recipes = *rp;
    auto key = cacheKey(item.withCount(1), shape);
    if (auto it = gCache.entries.find(key); it != gCache.entries.end()) return it->second;
    std::optional<Compaction> found;
    try {
        found = lookup(recipes, item, shape);
    } catch (std::exception const& e) {
        log().error("compacting recipe lookup for {} failed: {}", item.id, e.what());
    }
    gCache.entries.emplace(std::move(key), found);
    return found;
}

std::optional<Compaction> findCompaction(core::ItemStackData const& item, int gridSize) {
    if (gridSize != 2 && gridSize != 3) return std::nullopt;
    return findCompaction(item, Shape::full(gridSize));
}

bool uncompactMatches(core::ItemStackData const& result, core::ItemStackData const& item, int count) {
    if (result.empty() || item.empty()) return false;
    auto const* recipes = currentRecipes();
    if (!recipes) return false;
    auto key = cacheKey(result.withCount(1), Shape::full(1)) + '>' + cacheKey(item.withCount(1), Shape{}) + '#' + std::to_string(count);
    if (auto it = gCache.uncompact.find(key); it != gCache.uncompact.end()) return it->second;
    bool found = false;
    try {
        found = isReversible(*recipes, result.withCount(1), item, count);
    } catch (std::exception const& e) {
        log().error("uncompacting recipe lookup for {} failed: {}", result.id, e.what());
    }
    gCache.uncompact.emplace(std::move(key), found);
    return found;
}

namespace {
/// SC `isPartOfCompactingLoop`.
bool isPartOfCompactingLoop(core::ItemStackData const& first, core::ItemStackData const& firstResult) {
    std::vector<core::ItemStackData> queue{firstResult.withCount(1)};
    std::vector<core::ItemStackData> seen;
    int                              iterations = 0;
    while (!queue.empty()) {
        auto it = queue.front();
        queue.erase(queue.begin());
        for (int grid : {2, 3}) {
            auto c = findCompaction(it, grid);
            if (!c) continue;
            if (sameKind(c->result, first)) return true;
            for (auto const& s : seen) {
                if (sameKind(s, c->result)) return false; // a loop the first item isn't part of
            }
            queue.push_back(c->result.withCount(1));
        }
        seen.push_back(it);
        if (++iterations > 30) return true; // too many follow-ups to tell: don't compact
    }
    return false;
}
} // namespace

CompactingShapes compactingShapes(core::ItemStackData const& item) {
    CompactingShapes out;
    if (item.empty() || !currentRecipes()) return out;
    auto key = cacheKey(item.withCount(1), Shape{});
    if (auto it = gCache.shapes.find(key); it != gCache.shapes.end()) return it->second;
    for (int grid : {2, 3}) {
        auto c = findCompaction(item, grid);
        if (!c || sameKind(c->result, item) || isPartOfCompactingLoop(item, c->result)) continue;
        (grid == 2 ? out.twoByTwo : out.threeByThree)                       = true;
        (grid == 2 ? out.twoByTwoUncraftable : out.threeByThreeUncraftable) = c->reversible;
    }
    gCache.shapes.emplace(std::move(key), out);
    return out;
}

void invalidateCache() { gGeneration.fetch_add(1); }

} // namespace abp::upgrade::compacting

ABP_REGISTER_MODULE(abp::upgrade::compacting::CompactingRecipesModule);
