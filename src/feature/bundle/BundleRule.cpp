// SB `canBePlacedInContainerItems` for bundles (see BundleRule.h): the storage item hook (server
// and client prediction) and the server's announcement of the rule to companion clients.

#include "feature/bundle/BundleRule.h"

#include <atomic>
#include <chrono>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/inventory/simulation/validation/DynamicContainerValidation.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/admin/SbConfig.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Companion.h"

namespace abp::bundle {

namespace {
/// -1 unknown (not announced yet), 0 refused, 1 allowed.
std::atomic<int> gClientRule{-1};
} // namespace

void noteDecision(bool clientSide, bool allowed, ItemStackBase const& item) {
    using Clock = std::chrono::steady_clock;
    static std::atomic<std::int64_t> last[2]{};
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    auto& l  = last[clientSide ? 1 : 0];
    if (now - l.load() < 1000) return;
    l = now;
    log().info("bundle: {} {} {}", clientSide ? "client" : "server", allowed ? "allows" : "refuses", item.getTypeName());
}

bool serverAllowsBackpacks() { return admin::sbServer().canBePlacedInContainerItems; }

std::optional<bool> clientRule() {
    int v = gClientRule.load();
    if (v < 0) return std::nullopt;
    return v == 1;
}

void setClientRule(std::optional<bool> allowed) {
    int v = allowed ? (*allowed ? 1 : 0) : -1;
    if (gClientRule.exchange(v) != v && allowed) {
        log().info("bundle: server rule canBePlacedInContainerItems={}", *allowed);
    }
}

bool fitsBundles(ItemStackBase const& item, bool clientSide) {
    if (item.isNull() || !game::backpackTierOf(item.getTypeName())) return true;
    return clientSide ? clientRule().value_or(false) : serverAllowsBackpacks();
}

} // namespace abp::bundle

// Bundles (any `minecraft:storage_item`) ask this for every item put in through an item stack
// request (the client predicts with ContainerManagerController, see BundleRuleClient.cpp).
LL_TYPE_INSTANCE_HOOK(
    BundleBackpackHook,
    ll::memory::HookPriority::Normal,
    DynamicContainerValidation,
    &DynamicContainerValidation::$isItemAllowedInSlot,
    bool,
    ContainerScreenContext const& screenContext,
    int const                     slot,
    ItemStackBase const&          item,
    int const                     amount,
    bool                          isInternalTransfer
) {
    if (!item.isNull() && abp::game::backpackTierOf(item.getTypeName())) {
        Player* player     = screenContext.mPlayer;
        bool    clientSide = player && player->isClientSide();
        bool    allowed    = abp::bundle::fitsBundles(item, clientSide);
        abp::bundle::noteDecision(clientSide, allowed, item);
        if (!allowed) return false;
    }
    return origin(screenContext, slot, item, amount, isInternalTransfer);
}

namespace abp::bundle {
namespace {

/// Tells companion clients the rule once a second when they don't know it yet: after the
/// handshake, and after a `/backpack reload` changed it.
class BundleRuleModule {
public:
    void install() {
        BundleBackpackHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent& ev) {
            if (++mTicks % 20 == 0) sync(ev.level());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& ev) {
            mSent.erase(ev.self().getOrCreateUniqueID().rawID);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([this](auto&) { mSent.clear(); }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        mSent.clear();
        BundleBackpackHook::unhook();
    }

private:
    void sync(Level& level) {
        bool allowed = serverAllowsBackpacks();
        level.forEachPlayer([&](Player& player) {
            if (player.isClientSide() || !net::hasCompanion(player)) return true;
            auto id = player.getOrCreateUniqueID().rawID;
            auto it = mSent.find(id);
            if (it != mSent.end() && it->second == allowed) return true;
            net::send(player, BundleRulePacket{nlohmann::json{{"allowed", allowed}}});
            mSent[id] = allowed;
            return true;
        });
    }

    std::vector<ll::event::ListenerPtr>    mListeners;
    std::unordered_map<std::int64_t, bool> mSent;
    int                                    mTicks = 0;
};

} // namespace
} // namespace abp::bundle

ABP_REGISTER_MODULE(abp::bundle::BundleRuleModule);
