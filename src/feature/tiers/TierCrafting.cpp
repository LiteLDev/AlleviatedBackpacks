// Crafting result hooks: a crafted backpack (tier upgrade, dyeing, netherite smithing) becomes the
// same backpack as the consumed one, so it keeps its contents, upgrades and settings.
//
// Flow of a vanilla craft request on the server (ItemStackRequestActionCraftHandler):
//   CraftRecipe action  -> CraftHandlerBase::handleCraftAction (results computed)
//   Consume actions     -> CraftHandler*::handleConsumedItem(container, slot, item)   (one per input)
//   Take/Place from the created-output slot, then postRequest(success).
// On every consumed input we re-plan; when exactly one backpack was consumed, every backpack in
// the craft results (and in the created-output container, if already filled) gets the consumed
// backpack's user data. Colours from dyes are written to the storage only once the request
// succeeded.

#include <algorithm>
#include <chrono>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/SimpleSparseContainer.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerBase.h"
#include "mc/world/inventory/network/crafting/CraftHandlerCrafting.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftHandler.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"

#include "core/Appearance.h"
#include "feature/tiers/Tiers.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::tiers {

int craftGridIndex(int uiSlot) {
    auto const first3 = static_cast<int>(PlayerUISlot::Crafting3x3Input1);
    auto const first2 = static_cast<int>(PlayerUISlot::Crafting2x2Input1);
    if (uiSlot >= first3 && uiSlot < first3 + 9) return uiSlot - first3;
    if (uiSlot >= first2 && uiSlot < first2 + 4) {
        int i = uiSlot - first2;
        return (i / 2) * 3 + (i % 2);
    }
    return -1;
}

std::optional<BackpackCraft> analyse(std::vector<ConsumedInput> const& inputs) {
    ConsumedInput const* backpack = nullptr;
    for (auto const& in : inputs) {
        if (!in.item || !game::isBackpack(*in.item)) continue;
        if (backpack) return std::nullopt; // two backpacks: not ours to merge
        backpack = &in;
    }
    if (!backpack) return std::nullopt;
    BackpackCraft craft;
    craft.source = backpack->item;
    auto gridIndex = [](ConsumedInput const& in) { return in.inGrid ? craftGridIndex(in.slot) : -1; };
    int  bpIndex   = gridIndex(*backpack);
    std::vector<std::uint32_t> main, accent;
    for (auto const& in : inputs) {
        if (!in.item || in.item->isNull()) continue;
        auto dye = core::dyeFromItemId(in.item->getTypeName());
        if (!dye) continue;
        auto argb  = core::dyeInfo(*dye).argb;
        int  index = gridIndex(in);
        // Unknown position (consumed from outside the grid): colour the main fabric.
        bool isMain = index < 0 || bpIndex < 0 || core::dyeTargetFor(index, bpIndex) == core::DyeTarget::Main;
        (isMain ? main : accent).push_back(argb);
    }
    craft.mainColor   = core::mixColors(main);
    craft.accentColor = core::mixColors(accent);
    return craft;
}

std::string carryOver(ItemStackBase& result, BackpackCraft& craft) {
    if (!craft.source || craft.source->isNull()) return {};
    auto uuid = game::backpackUuid(*craft.source);
    if (!uuid && craft.dyes()) {
        // Colours live in the storage: bind the never-opened source to a new storage entry.
        bool changed = false;
        if (auto bp = game::Storage::get().forItem(*craft.source, &changed)) {
            uuid                 = bp->uuid();
            craft.createdStorage = changed;
        }
    }
    if (craft.source->mUserData) result.setUserData(craft.source->mUserData->clone());
    return uuid.value_or(std::string{});
}

bool commitColors(std::string const& uuid, BackpackCraft const& craft) {
    if (uuid.empty() || !craft.dyes()) return false;
    auto bp = game::Storage::get().find(uuid);
    if (!bp) return false;
    if (craft.mainColor) bp->data().mainColor = *craft.mainColor;
    if (craft.accentColor) bp->data().accentColor = *craft.accentColor;
    bp->markDirty();
    game::Storage::get().save(*bp);
    return true;
}

namespace {

/// A request is handled within one call; anything older belongs to an aborted request.
constexpr auto kStaleAfter = std::chrono::seconds(2);

struct PendingCraft {
    std::vector<std::pair<int, ConsumedInput>> inputs; ///< (container, input)
    std::optional<BackpackCraft>               craft;
    std::string                                uuid;
    std::string                                createdUuid; ///< storage entry made for this craft
    int                                        applied = 0; ///< results bound to the backpack
    std::chrono::steady_clock::time_point      started = std::chrono::steady_clock::now();
};

std::unordered_map<CraftHandlerBase const*, PendingCraft> gPending;

bool isBackpackStack(ItemStackBase const& s) { return !s.isNull() && game::backpackTierOf(s.getTypeName()).has_value(); }

bool hasBackpackResult(CraftHandlerBase& handler) {
    auto& requests = handler.mCraftRequestHandler;
    for (auto const& [result, count] : *requests.mCraftResults) {
        if (isBackpackStack(result)) return true;
    }
    if (auto const& output = requests.mCreatedOutputSparseContainer) {
        for (auto const& [index, stack] : *output->mItems) {
            if (isBackpackStack(stack)) return true;
        }
    }
    return false;
}

/// Drops a pending craft; a storage entry created for it is removed unless the craft succeeded.
void dropPending(CraftHandlerBase const* handler, bool keepCreated) {
    auto it = gPending.find(handler);
    if (it == gPending.end()) return;
    if (!keepCreated && !it->second.createdUuid.empty()) game::Storage::get().erase(it->second.createdUuid);
    gPending.erase(it);
}

/// Returns false to reject the craft.
bool onConsumed(CraftHandlerBase& handler, FullContainerName const& container, int slot, ItemStack const& item) {
    if (item.isNull()) return true;
    if (auto it = gPending.find(&handler);
        it != gPending.end() && std::chrono::steady_clock::now() - it->second.started > kStaleAfter) {
        dropPending(&handler, false);
    }
    auto& pending = gPending[&handler];
    auto  name    = static_cast<int>(container.mName);
    // The same slot can be reported twice (derived + base handler): keep the first report, its
    // stack may already have been bound to a storage entry by `carryOver`.
    if (std::any_of(pending.inputs.begin(), pending.inputs.end(), [&](auto const& in) {
            return in.first == name && in.second.slot == slot;
        })) {
        return true;
    }
    bool inGrid = container.mName == ContainerEnumName::CraftingInputContainer;
    pending.inputs.push_back({name, ConsumedInput{slot, std::make_shared<ItemStack>(item), inGrid}});
    std::vector<ConsumedInput> inputs;
    for (auto const& [_, in] : pending.inputs) inputs.push_back(in);
    // Never let a craft turn two backpacks into one (e.g. the vanilla repair recipe on damageable
    // backpacks): the second one's contents would be orphaned.
    auto backpacks = std::count_if(inputs.begin(), inputs.end(), [](auto const& in) { return in.item && game::isBackpack(*in.item); });
    if (backpacks > 1 && hasBackpackResult(handler)) {
        log().debug("backpack craft rejected: {} backpacks consumed", backpacks);
        return false;
    }
    pending.craft = analyse(inputs);
    if (!pending.craft) return true;

    auto& requests = handler.mCraftRequestHandler;
    int   applied  = 0;
    auto  apply    = [&](ItemStackBase& stack) {
        pending.uuid = carryOver(stack, *pending.craft);
        if (pending.craft->createdStorage) pending.createdUuid = pending.uuid;
        ++applied;
    };
    for (auto& [result, count] : *requests.mCraftResults) {
        if (isBackpackStack(result)) apply(result);
    }
    if (auto const& output = requests.mCreatedOutputSparseContainer) {
        for (auto& [index, stack] : *output->mItems) {
            if (isBackpackStack(stack)) apply(stack);
        }
    }
    pending.applied = applied;
    return true;
}

void onPostRequest(CraftHandlerBase& handler, bool success) {
    auto it = gPending.find(&handler);
    if (it == gPending.end()) return;
    auto pending = std::move(it->second);
    dropPending(&handler, true);
    if (!success) {
        if (!pending.createdUuid.empty()) game::Storage::get().erase(pending.createdUuid);
        return;
    }
    if (!pending.craft || !pending.applied) return;
    bool dyed = commitColors(pending.uuid, *pending.craft);
    log().info("backpack craft: {} carried into the crafted backpack{}", pending.uuid.empty() ? "(unopened)" : pending.uuid, dyed ? " (dyed)" : "");
}
} // namespace

LL_TYPE_INSTANCE_HOOK(
    TierCraftStartHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerBase,
    &CraftHandlerBase::handleCraftAction,
    ItemStackNetResult,
    ItemStackRequestActionCraftBase const& requestAction,
    ItemStackNetResult                     currentResult
) {
    dropPending(this, false); // a new craft starts: forget a previous unfinished one
    return origin(requestAction, currentResult);
}

LL_TYPE_INSTANCE_HOOK(
    TierCraftConsumeHook,
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
            if (!onConsumed(*this, container, slot, consumedItem)) return ItemStackNetResult::Error;
        } catch (std::exception const& e) {
            log().error("backpack craft hook failed: {}", e.what());
        }
    }
    return result;
}

// Other craft handlers (smithing table) use the base implementation.
LL_TYPE_INSTANCE_HOOK(
    TierBaseConsumeHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerBase,
    &CraftHandlerBase::$handleConsumedItem,
    ItemStackNetResult,
    FullContainerName const& container,
    uchar const              slot,
    ItemStack const&         consumedItem
) {
    auto result = origin(container, slot, consumedItem);
    if (result == ItemStackNetResult::Success) {
        try {
            if (!onConsumed(*this, container, slot, consumedItem)) return ItemStackNetResult::Error;
        } catch (std::exception const& e) {
            log().error("backpack craft hook failed: {}", e.what());
        }
    }
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    TierCraftPostHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerBase,
    &CraftHandlerBase::postRequest,
    void,
    bool wasSuccess
) {
    origin(wasSuccess);
    try {
        onPostRequest(*this, wasSuccess);
    } catch (std::exception const& e) {
        log().error("backpack craft hook failed: {}", e.what());
    }
}

class TierCrafting {
public:
    void install() {
        TierCraftStartHook::hook();
        TierCraftConsumeHook::hook();
        TierBaseConsumeHook::hook();
        TierCraftPostHook::hook();
    }
    void uninstall() {
        TierCraftStartHook::unhook();
        TierCraftConsumeHook::unhook();
        TierBaseConsumeHook::unhook();
        TierCraftPostHook::unhook();
        gPending.clear();
    }
};

} // namespace abp::tiers

ABP_REGISTER_MODULE(abp::tiers::TierCrafting);
