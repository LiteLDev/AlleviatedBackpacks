// GX-3 / P2-12: the Diamond backpack as the base of the netherite smithing recipe without
// durability.
//
// Bedrock checks a `minecraft:recipe_smithing_transform` when the behavior pack's recipes are
// loaded: "Smithing Transform Recipe: <id> base item is not a piece of armor or equipment" unless
// the base item has armor points (a wearable with protection > 0) or is damageable (durability).
// Item tags (`minecraft:transformable_items`), `minecraft:enchantable` and a wearable with
// protection 0 don't pass (tried with the server harness). SB's backpack has neither armor points
// nor durability, so instead of giving the item durability (the old workaround, with damage reset
// by TierLore) the mod answers that check: while a recipe is being loaded
// (`Recipes::_loadRecipe`), the Diamond backpack reports itself damageable
// (`ComponentItem::isDamageable`). Nothing else sees it as damageable; the smithing table's slot
// validation and recipe matching accept it as it is (selftest tiers.smithing_without_durability,
// client e2e gx3).
//
// Recipes are loaded when the world is created (BDS: before mods are enabled), so the hooks are
// installed at mod load, in both builds (the client's local worlds load recipes too).

#include <string_view>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/item/crafting/Recipes.h"

#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::tiers {

namespace {
thread_local int gLoadingRecipe = 0;
int              gAnswered      = 0;

bool isSmithingBaseBackpack(Item const& item) {
    static std::string const id = game::backpackItemId(core::Tier::Diamond);
    return item.mFullName->getString() == id;
}

bool answer(Item const& item) {
    if (gLoadingRecipe <= 0 || !isSmithingBaseBackpack(item)) return false;
    if (gAnswered++ == 0) log().info("smithing: the diamond backpack is accepted as the netherite smithing base (no durability)");
    return true;
}
} // namespace

LL_TYPE_INSTANCE_HOOK(
    SmithingRecipeLoadHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::_loadRecipe,
    bool,
    std::string const&        recipeId,
    RecipeType                type,
    Json::Value const&        objData,
    MinEngineVersion const&   minEngineVersion,
    SemVersion const&         formatVersion,
    bool                      isBaseGamePack,
    Recipes::Buffers&         buffers
) {
    ++gLoadingRecipe;
    struct Leave {
        ~Leave() { --gLoadingRecipe; }
    } leave;
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

LL_TYPE_INSTANCE_HOOK(SmithingBaseDamageableHook, ll::memory::HookPriority::Normal, ComponentItem, &ComponentItem::$isDamageable, bool) {
    if (answer(*this)) return true;
    return origin();
}

class SmithingBase {
public:
    void load() { hook(); }
    void install() { hook(); }
    void uninstall() {
        if (!mHooked) return;
        SmithingBaseDamageableHook::unhook();
        SmithingRecipeLoadHook::unhook();
        mHooked = false;
    }

private:
    void hook() {
        if (mHooked) return;
        SmithingRecipeLoadHook::hook();
        SmithingBaseDamageableHook::hook();
        mHooked = true;
    }
    bool mHooked = false;
};

} // namespace abp::tiers

ABP_REGISTER_MODULE(abp::tiers::SmithingBase);
