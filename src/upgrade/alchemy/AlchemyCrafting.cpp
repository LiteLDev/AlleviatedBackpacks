// SB `UpgradeNextTierRecipe` for the alchemy upgrades: crafting an Advanced Alchemy Upgrade from an
// Alchemy Upgrade keeps the consumed upgrade's data (its settings embedded in the item's user data:
// filters, conditions, match options).

#include <exception>

#include "ll/api/memory/Hook.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/SimpleSparseContainer.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerBase.h"
#include "mc/world/inventory/network/crafting/CraftHandlerCrafting.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftHandler.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"

#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/alchemy/Alchemy.h"

namespace abp::upgrade::alchemy {

namespace {

void carryOver(CraftHandlerBase& handler, ItemStack const& consumed) {
    if (consumed.isNull() || consumed.getTypeName() != kBasicItem || !consumed.mUserData) return;
    auto& requests = handler.mCraftRequestHandler;
    for (auto& [result, count] : *requests.mCraftResults) {
        if (!result.isNull() && result.getTypeName() == kAdvancedItem) result.setUserData(consumed.mUserData->clone());
    }
    if (auto const& output = requests.mCreatedOutputSparseContainer) {
        for (auto& [index, stack] : *output->mItems) {
            if (!stack.isNull() && stack.getTypeName() == kAdvancedItem) stack.setUserData(consumed.mUserData->clone());
        }
    }
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    AlchemyNextTierHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerCrafting,
    &CraftHandlerCrafting::$handleConsumedItem,
    ItemStackNetResult,
    FullContainerName const& container,
    uchar const              slot,
    ItemStack const&         consumedItem
) {
    auto result = origin(container, slot, consumedItem);
    if (result == ItemStackNetResult::Success) {
        try {
            carryOver(*this, consumedItem);
        } catch (std::exception const& e) {
            log().error("alchemy upgrade craft hook failed: {}", e.what());
        }
    }
    return result;
}

class AlchemyCrafting {
public:
    void install() { AlchemyNextTierHook::hook(); }
    void uninstall() { AlchemyNextTierHook::unhook(); }
};

} // namespace abp::upgrade::alchemy

ABP_REGISTER_MODULE(abp::upgrade::alchemy::AlchemyCrafting);
