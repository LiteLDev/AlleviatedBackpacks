// Sophisticated Backpacks / Sophisticated Core server + common config (see SbConfig.h) and the
// rules that read it directly: tier slot counts, block interaction / connection deny-lists, SC's
// `enabledItems` (recipes of disabled items can't be crafted) and `canBePlacedInContainerItems`.

#include "feature/admin/SbConfig.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerBase.h"
#include "mc/world/inventory/network/crafting/CraftHandlerCrafting.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftHandler.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/inventory/simulation/validation/ShulkerBoxContainerValidation.h"
#include "mc/world/level/block/actor/ShulkerBoxBlockActor.h"
#include "mc/world/SimpleSparseContainer.h"
#include "mc/world/containers/FullContainerName.h"

#include "core/Tier.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/IUpgrade.h"

ABP_REGISTER_CONFIG("sophisticatedbackpacks-server", abp::admin::SbServerConfig);
ABP_REGISTER_CONFIG("sophisticatedbackpacks-common", abp::admin::SbCommonConfig);
ABP_REGISTER_CONFIG("sophisticatedcore-common", abp::admin::ScCommonConfig);

namespace abp::admin {

SbServerConfig const& sbServer() { return config::get<SbServerConfig>(); }
SbCommonConfig const& sbCommon() { return config::get<SbCommonConfig>(); }
ScCommonConfig const& scCommon() { return config::get<ScCommonConfig>(); }

namespace {

SbServerConfig::BackpackSettings const& settingsOf(core::Tier tier) {
    auto const& c = sbServer();
    switch (tier) {
    case core::Tier::Leather:
        return c.leatherBackpack;
    case core::Tier::Copper:
        return c.copperBackpack;
    case core::Tier::Iron:
        return c.ironBackpack;
    case core::Tier::Gold:
        return c.goldBackpack;
    case core::Tier::Diamond:
        return c.diamondBackpack;
    case core::Tier::Netherite:
        return c.netheriteBackpack;
    }
    return c.leatherBackpack;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

std::string withNamespace(std::string_view id) {
    auto s = lower(id);
    if (s.find(':') == std::string::npos) s = "minecraft:" + s;
    return s;
}

bool listContains(std::vector<std::string> const& list, std::string_view id) {
    auto want = withNamespace(id);
    return std::any_of(list.begin(), list.end(), [&](std::string const& e) { return !e.empty() && withNamespace(e) == want; });
}

} // namespace

int inventorySlotCount(core::Tier tier) { return std::clamp(settingsOf(tier).inventorySlotCount, 1, 144); }
int upgradeSlotCount(core::Tier tier) { return std::clamp(settingsOf(tier).upgradeSlotCount, 0, 10); }

double clampedChance(double v, double lo, double hi) { return std::clamp(v, lo, hi); }

bool itemFitsContainerItems(ItemStackBase const& item) {
    return item.isNull() || !game::backpackTierOf(item.getTypeName()) || sbServer().canBePlacedInContainerItems;
}

bool isBlockInteractionDisallowed(std::string_view blockId) { return listContains(sbServer().noInteractionBlocks, blockId); }

bool isBlockConnectionDisallowed(std::string_view blockId) {
    auto const& c = sbServer();
    return c.allBlockConnectionsDisallowed || listContains(c.noConnectionBlocks, blockId);
}

bool isItemEnabled(std::string_view itemId) {
    auto want = withNamespace(itemId);
    for (auto const& e : scCommon().enabledItems) {
        auto bar = e.find('|');
        if (bar == std::string::npos) continue;
        if (withNamespace(std::string_view(e).substr(0, bar)) != want) continue;
        return lower(std::string_view(e).substr(bar + 1)) != "false";
    }
    return true;
}

namespace {

/// Every item of the mod, as SC lists them in `enabledItems`.
std::vector<std::string> supportedItems() {
    std::vector<std::string> out;
    for (auto const& t : core::kTiers) out.push_back(game::backpackItemId(t.tier));
    out.push_back(std::string(game::kNamespace) + ":upgrade_base");
    out.push_back(std::string(game::kNamespace) + ":ender_linker");
    for (auto const& u : upgrade::all()) out.push_back(u->info().itemId);
    return out;
}

/// SC adds every item it is asked about to `enabledItems` as "|true": fill the list once per
/// world start so admins see what they can disable.
void fillEnabledItems() {
    auto&                 cfg = config::mut<ScCommonConfig>();
    std::set<std::string> listed;
    for (auto const& e : cfg.enabledItems) listed.insert(withNamespace(std::string_view(e).substr(0, e.find('|'))));
    bool changed = false;
    for (auto const& id : supportedItems()) {
        if (listed.insert(id).second) {
            cfg.enabledItems.push_back(id + "|true");
            changed = true;
        }
    }
    if (!changed) return;
    for (auto const& entry : config::ConfigRegistry::entries()) {
        if (entry.section == "sophisticatedcore-common") entry.save(config::configDir() / (entry.section + ".json"));
    }
}

// ---- tier slot counts -------------------------------------------------------------------------

/// `<tier>Backpack.inventorySlotCount / upgradeSlotCount`: storage grows to the configured size;
/// it shrinks only when nothing would be lost (the slots to cut are empty), like SB keeps stacks
/// beyond a lowered slot count instead of deleting them.
void applySlotCounts(game::Backpack& bp) {
    auto& d       = bp.data();
    int   slots   = inventorySlotCount(bp.tier());
    int   upgrades = upgradeSlotCount(bp.tier());
    auto& inv     = d.inventory;
    if (inv.size() != slots) {
        bool lossless = true;
        for (int i = slots; i < inv.size(); ++i) lossless = lossless && inv.at(i).empty();
        if (lossless) {
            (void)inv.resize(slots);
            bp.markDirty();
        }
    }
    if (static_cast<int>(d.upgrades.size()) != upgrades) {
        bool lossless = true;
        for (std::size_t i = static_cast<std::size_t>(upgrades); i < d.upgrades.size(); ++i) {
            lossless = lossless && d.upgrades[i].empty();
        }
        if (lossless) {
            d.upgrades.resize(static_cast<std::size_t>(upgrades));
            bp.markDirty();
        }
    }
}

// ---- enabledItems: disabled items can't be crafted -----------------------------------------------

bool disabledResult(CraftHandlerBase& handler) {
    auto& requests = handler.mCraftRequestHandler;
    for (auto const& [result, count] : *requests.mCraftResults) {
        if (!result.isNull() && !isItemEnabled(result.getTypeName())) return true;
    }
    return false;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    SbEnabledItemsCraftHook,
    ll::memory::HookPriority::High,
    CraftHandlerCrafting,
    &CraftHandlerCrafting::$handleConsumedItem,
    ItemStackNetResult,
    FullContainerName const& container,
    uchar const              slot,
    ItemStack const&         consumedItem
) {
    if (scCommon().enabledItems.size() > 0 && disabledResult(*this)) return ItemStackNetResult::Error;
    return origin(container, slot, consumedItem);
}

// SB `canBePlacedInContainerItems` (default false): backpacks can't go into shulker boxes, neither
// through the shulker box screen nor through hoppers.
LL_TYPE_INSTANCE_HOOK(
    SbShulkerScreenHook,
    ll::memory::HookPriority::Normal,
    ShulkerBoxContainerValidation,
    &ShulkerBoxContainerValidation::$isItemAllowedInSlot,
    bool,
    ContainerScreenContext const& context,
    int const                     slot,
    ItemStackBase const&          item,
    int const                     amount,
    bool                          flag
) {
    if (!itemFitsContainerItems(item)) return false;
    return origin(context, slot, item, amount, flag);
}

LL_TYPE_INSTANCE_HOOK(
    SbShulkerPushHook,
    ll::memory::HookPriority::Normal,
    ShulkerBoxBlockActor,
    &ShulkerBoxBlockActor::$canPushInItem,
    bool,
    int              slot,
    int              face,
    ItemStack const& item
) {
    if (!itemFitsContainerItems(item)) return false;
    return origin(slot, face, item);
}

namespace {

class SbConfigModule {
public:
    void install() {
        SbEnabledItemsCraftHook::hook();
        SbShulkerScreenHook::hook();
        SbShulkerPushHook::hook();
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerStartedEvent>([](auto&) {
            fillEnabledItems();
        });
        if (game::Storage::get().available()) fillEnabledItems();
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        SbShulkerPushHook::unhook();
        SbShulkerScreenHook::unhook();
        SbEnabledItemsCraftHook::unhook();
    }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace
} // namespace abp::admin

ABP_REGISTER_BACKPACK_HOOKS([](abp::game::Backpack& bp) { abp::admin::applySlotCounts(bp); });
ABP_REGISTER_MODULE(abp::admin::SbConfigModule);
