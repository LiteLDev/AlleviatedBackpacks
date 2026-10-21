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
};
Cache gCache;

std::string cacheKey(core::ItemStackData const& item, int gridSize) {
    return item.id + '|' + std::to_string(item.aux) + '|' + std::to_string(item.nbtHash()) + '|'
         + std::to_string(gridSize);
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

/// Collects one entry per filled crafting cell into `out`; false unless the recipe fills exactly a
/// `gridSize` x `gridSize` grid (shapeless: that many items).
bool collectIngredients(Recipe const& recipe, int gridSize, std::vector<RecipeIngredient const*>& out) {
    out.clear();
    if (recipe.isShapeless()) {
        for (auto const& ing : *recipe.mMyIngredients) {
            if (ing.isNull()) continue;
            for (int i = 0; i < std::max<int>(ing.mStackSize, 1); ++i) out.push_back(&ing);
        }
        return static_cast<int>(out.size()) == gridSize * gridSize;
    }
    int w = recipe.mWidth;
    int h = recipe.mHeight;
    if (w != gridSize || h != gridSize) return false;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto const& ing = recipe.getIngredient(x, y);
            if (ing.isNull() || ing.mStackSize > 1) return false;
            out.push_back(&ing);
        }
    }
    return static_cast<int>(out.size()) == gridSize * gridSize;
}

bool sameKind(core::ItemStackData const& a, core::ItemStackData const& b) { return a.id == b.id && a.aux == b.aux; }

/// True if one `result` crafts back into exactly `expected` items of `original`.
bool isReversible(Recipes const& recipes, core::ItemStackData const& result, core::ItemStackData const& original, int expected) {
    auto resultStack = game::ItemConv::toStack(result, 1);
    if (resultStack.isNull()) return false;
    bool found = false;
    recipes.forEachRecipeFor(HashedString(kCraftingTag), [&](Recipe const& recipe) {
        if (found) return;
        auto const* out = singleResult(recipe);
        if (!out || out->mCount != expected) return;
        std::vector<RecipeIngredient const*> ings;
        if (recipe.isShapeless()) {
            if (!collectIngredients(recipe, 1, ings)) return;
        } else {
            // A 1x1 shaped recipe (or a bigger shape with a single filled cell).
            int w = recipe.mWidth;
            int h = recipe.mHeight;
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    auto const& ing = recipe.getIngredient(x, y);
                    if (ing.isNull()) continue;
                    if (ing.mStackSize > 1) return;
                    ings.push_back(&ing);
                }
            }
            if (ings.size() != 1) return;
        }
        if (!recipe.itemValidForRecipe(*ings.front(), resultStack)) return;
        if (!sameKind(toData(*out), original)) return;
        found = true;
    });
    return found;
}

std::optional<Compaction> lookup(Recipes const& recipes, core::ItemStackData const& item, int gridSize) {
    auto stack = game::ItemConv::toStack(item, 1);
    if (stack.isNull() || stack.mUserData || item.damage > 0) return std::nullopt;
    int                                  need = gridSize * gridSize;
    std::optional<Compaction>            best;
    std::vector<RecipeIngredient const*> ings;
    recipes.forEachRecipeFor(HashedString(kCraftingTag), [&](Recipe const& recipe) {
        if (best && best->reversible) return; // a reversible match wins; keep the first one
        auto const* out = singleResult(recipe);
        if (!out || !collectIngredients(recipe, gridSize, ings)) return;
        for (auto const* ing : ings) {
            if (!recipe.itemValidForRecipe(*ing, stack)) return;
        }
        auto result = toData(*out);
        if (result.empty() || sameKind(result, item)) return;
        result.count   = out->mCount;
        bool reversible = need % result.count == 0 && isReversible(recipes, result.withCount(1), item, need / result.count);
        if (!best || reversible) best = Compaction{result, need, reversible};
    });
    return best;
}

void dropCache() {
    gCache.entries.clear();
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

std::optional<Compaction> findCompaction(core::ItemStackData const& item, int gridSize) {
    if (item.empty() || (gridSize != 2 && gridSize != 3)) return std::nullopt;
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    Recipes const& recipes = level->getRecipes();
    auto           gen     = gGeneration.load();
    if (gCache.generation != gen || gCache.recipes != &recipes) {
        dropCache();
        gCache.generation = gen;
        gCache.recipes    = &recipes;
    }
    auto key = cacheKey(item, gridSize);
    if (auto it = gCache.entries.find(key); it != gCache.entries.end()) return it->second;
    std::optional<Compaction> found;
    try {
        found = lookup(recipes, item, gridSize);
    } catch (std::exception const& e) {
        log().error("compacting recipe lookup for {} failed: {}", item.id, e.what());
    }
    gCache.entries.emplace(std::move(key), found);
    return found;
}

void invalidateCache() { gGeneration.fetch_add(1); }

} // namespace abp::upgrade::compacting

ABP_REGISTER_MODULE(abp::upgrade::compacting::CompactingRecipesModule);
