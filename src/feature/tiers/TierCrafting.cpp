// Crafting result hooks: a crafted backpack (tier upgrade, dyeing, netherite smithing) becomes the
// same backpack as the consumed one, so it keeps its contents, upgrades, settings and colours;
// a "next tier" upgrade keeps the consumed upgrade's settings (SC `upgrade_next_tier`).
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
#include "upgrade/IUpgrade.h"

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
    craft.source  = backpack->item;
    auto column   = [](ConsumedInput const& in) {
        int index = in.inGrid ? craftGridIndex(in.slot) : -1;
        return index < 0 ? -1 : index % 3;
    };
    int bpColumn = column(*backpack);
    for (auto const& in : inputs) {
        if (!in.item || in.item->isNull()) continue;
        auto dye = core::dyeFromItemId(in.item->getTypeName());
        if (!dye) continue;
        auto argb = core::dyeInfo(*dye).argb;
        int  col  = column(in);
        // Unknown position (consumed from outside the grid): as if in the backpack's column.
        auto target = (col < 0 || bpColumn < 0) ? core::DyeColumnTarget{true, true} : core::dyeColumnTarget(col, bpColumn);
        if (target.main) craft.mainDyes.push_back(argb);
        if (target.accent) craft.accentDyes.push_back(argb);
    }
    return craft;
}

BackpackColors craftedColors(BackpackCraft const& craft) {
    std::shared_ptr<game::Backpack> bp;
    if (craft.source) {
        if (auto uuid = game::backpackUuid(*craft.source)) bp = game::Storage::get().find(*uuid);
    }
    auto current  = craft.source ? colorsOf(*craft.source, bp.get()) : defaultColors();
    auto defaults = defaultColors();
    return {
        core::calculateColor(current.main, defaults.main, craft.mainDyes),
        core::calculateColor(current.accent, defaults.accent, craft.accentDyes),
    };
}

std::string carryOver(ItemStackBase& result, BackpackCraft const& craft) {
    if (!craft.source || craft.source->isNull()) return {};
    if (craft.source->mUserData) result.setUserData(craft.source->mUserData->clone());
    if (craft.dyes()) {
        auto colors = craftedColors(craft);
        setItemColors(result, colors);
        applyTint(result, colors);
    }
    return game::backpackUuid(*craft.source).value_or(std::string{});
}

bool commitColors(std::string const& uuid, BackpackCraft const& craft) {
    if (uuid.empty() || !craft.dyes()) return false;
    auto bp = game::Storage::get().find(uuid);
    if (!bp) return false;
    auto colors           = craftedColors(craft);
    bp->data().mainColor   = colors.main;
    bp->data().accentColor = colors.accent;
    bp->markDirty();
    game::Storage::get().save(*bp);
    return true;
}

bool isNextTierUpgrade(std::string_view sourceItemId, std::string_view resultItemId) {
    // SophisticatedBackpacks' `sophisticatedcore:upgrade_next_tier` recipes that consume an upgrade.
    static constexpr std::pair<std::string_view, std::string_view> kPairs[] = {
        {"pickup_upgrade",       "advanced_pickup_upgrade"      },
        {"filter_upgrade",       "advanced_filter_upgrade"      },
        {"pickup_upgrade",       "magnet_upgrade"               },
        {"advanced_pickup_upgrade", "advanced_magnet_upgrade"   },
        {"magnet_upgrade",       "advanced_magnet_upgrade"      },
        {"feeding_upgrade",      "advanced_feeding_upgrade"     },
        {"compacting_upgrade",   "advanced_compacting_upgrade"  },
        {"void_upgrade",         "advanced_void_upgrade"        },
        {"restock_upgrade",      "advanced_restock_upgrade"     },
        {"deposit_upgrade",      "advanced_deposit_upgrade"     },
        {"refill_upgrade",       "advanced_refill_upgrade"      },
        {"jukebox_upgrade",      "advanced_jukebox_upgrade"     },
        {"tool_swapper_upgrade", "advanced_tool_swapper_upgrade"},
        {"pump_upgrade",         "advanced_pump_upgrade"        },
        {"smelting_upgrade",     "auto_smelting_upgrade"        },
        {"smoking_upgrade",      "auto_smoking_upgrade"         },
        {"blasting_upgrade",     "auto_blasting_upgrade"        },
    };
    auto strip = [](std::string_view id) -> std::optional<std::string_view> {
        if (!id.starts_with(game::kNamespace) || id.size() <= game::kNamespace.size() + 1 || id[game::kNamespace.size()] != ':') {
            return std::nullopt;
        }
        return id.substr(game::kNamespace.size() + 1);
    };
    auto from = strip(sourceItemId), to = strip(resultItemId);
    if (!from || !to) return false;
    for (auto const& [a, b] : kPairs) {
        if (a == *from && b == *to) return true;
    }
    return false;
}
namespace {

/// A request is handled within one call; anything older belongs to an aborted request.
constexpr auto kStaleAfter = std::chrono::seconds(2);

struct PendingCraft {
    std::vector<std::pair<int, ConsumedInput>> inputs; ///< (container, input)
    std::optional<BackpackCraft>               craft;
    std::string                                uuid;
    int                                        applied = 0; ///< results bound to the backpack
    std::chrono::steady_clock::time_point      started = std::chrono::steady_clock::now();
};

std::unordered_map<CraftHandlerBase const*, PendingCraft> gPending;

bool isBackpackStack(ItemStackBase const& s) { return !s.isNull() && game::backpackTierOf(s.getTypeName()).has_value(); }
bool isUpgradeStack(ItemStackBase const& s) {
    return !s.isNull() && upgrade::byItemId(s.getTypeName()) != nullptr;
}

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

void dropPending(CraftHandlerBase const* handler) { gPending.erase(handler); }

/// Every result stack of the craft (computed results and the created-output container).
template <class Fn>
void forEachResult(CraftHandlerBase& handler, Fn&& fn) {
    auto& requests = handler.mCraftRequestHandler;
    for (auto& [result, count] : *requests.mCraftResults) fn(result);
    if (auto const& output = requests.mCreatedOutputSparseContainer) {
        for (auto& [index, stack] : *output->mItems) fn(stack);
    }
}

/// SC `upgrade_next_tier`: the single consumed upgrade's data goes to the crafted upgrade.
void carryUpgradeData(CraftHandlerBase& handler, std::vector<ConsumedInput> const& inputs) {
    ConsumedInput const* source = nullptr;
    for (auto const& in : inputs) {
        if (!in.item || !isUpgradeStack(*in.item)) continue;
        if (source) return; // several upgrades consumed (e.g. Omega from Tier 4s): plain recipe
        source = &in;
    }
    if (!source || !source->item->mUserData) return;
    auto sourceId = source->item->getTypeName();
    forEachResult(handler, [&](ItemStackBase& result) {
        if (isUpgradeStack(result) && isNextTierUpgrade(sourceId, result.getTypeName())) {
            result.setUserData(source->item->mUserData->clone());
        }
    });
}

/// Returns false to reject the craft.
bool onConsumed(CraftHandlerBase& handler, FullContainerName const& container, int slot, ItemStack const& item) {
    if (item.isNull()) return true;
    if (auto it = gPending.find(&handler);
        it != gPending.end() && std::chrono::steady_clock::now() - it->second.started > kStaleAfter) {
        dropPending(&handler);
    }
    auto& pending = gPending[&handler];
    auto  name    = static_cast<int>(container.mName);
    // The same slot can be reported twice (derived + base handler): keep the first report.
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
    if (!pending.craft) {
        carryUpgradeData(handler, inputs);
        return true;
    }
    int applied = 0;
    forEachResult(handler, [&](ItemStackBase& stack) {
        if (!isBackpackStack(stack)) return;
        pending.uuid = carryOver(stack, *pending.craft);
        ++applied;
    });
    pending.applied = applied;
    return true;
}

void onPostRequest(CraftHandlerBase& handler, bool success) {
    auto it = gPending.find(&handler);
    if (it == gPending.end()) return;
    auto pending = std::move(it->second);
    dropPending(&handler);
    if (!success || !pending.craft || !pending.applied) return;
    std::string dyed;
    if (pending.craft->dyes()) {
        auto colors = craftedColors(*pending.craft);
        dyed        = " (dyed main=" + core::colorHex(colors.main) + " accent=" + core::colorHex(colors.accent) + ")";
    }
    commitColors(pending.uuid, *pending.craft);
    log().info("backpack craft: {} carried into the crafted backpack{}", pending.uuid.empty() ? "(unopened)" : pending.uuid, dyed);
}} // namespace

LL_TYPE_INSTANCE_HOOK(
    TierCraftStartHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerBase,
    &CraftHandlerBase::handleCraftAction,
    ItemStackNetResult,
    ItemStackRequestActionCraftBase const& requestAction,
    ItemStackNetResult                     currentResult
) {
    dropPending(this); // a new craft starts: forget a previous unfinished one
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
