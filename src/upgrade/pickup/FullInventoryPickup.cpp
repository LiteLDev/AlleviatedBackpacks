// Pickup with a full player inventory.
//
// The foundation feeds pickup upgrades from `PlayerPickUpItemEvent` (Player::take), which runs
// before vanilla puts the item into the inventory, so backpacks get the first pick. But vanilla
// only calls Player::take when the inventory has room: with a full inventory a pickup upgrade would
// never see the item. Sophisticated Backpacks picks such items up into the backpack, so this hook
// runs after ItemActor::playerTouch and, if vanilla did not try to take the item although it was
// pickable, publishes the same `PlayerPickUpItemEvent` itself. The foundation's listener then
// dispatches it to the carried backpacks exactly like a normal pickup, and other mods can still
// cancel it (e.g. protected areas).

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPickUpItemEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/PlayerScan.h"
#include "mod/Module.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::pickup {

namespace {

/// Set by the PlayerPickUpItemEvent listener while a playerTouch runs.
thread_local bool gTakeAttempted = false;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable" // LL_TYPE_INSTANCE_HOOK's _AutoHookCount
LL_TYPE_INSTANCE_HOOK(
    FullInventoryPickupHook,
    ll::memory::HookPriority::Normal,
    ItemActor,
    &ItemActor::$playerTouch,
    void,
    Player& player
) {
    bool outer     = gTakeAttempted;
    gTakeAttempted = false;
    origin(player);
    bool attempted = gTakeAttempted;
    gTakeAttempted = outer;
    if (attempted || player.isClientSide() || all().empty()) return;
    // Only items vanilla would have picked up if there had been room.
    if (mRemoved || mPickupDelay > 0 || mIsInItemFrame || !isAlive()) return;
    if (!player.isAlive() || player.isSpectator()) return;
    if (item().isNull() || game::scanBackpacks(player).empty()) return;
    int count   = item().mCount;
    int favored = -1;
    auto ev     = ll::event::PlayerPickUpItemEvent(player, *this, count, favored);
    ll::event::EventBus::getInstance().publish(ev);
    gTakeAttempted = outer;
}
#pragma clang diagnostic pop

class FullInventoryPickup {
public:
    void install() {
        FullInventoryPickupHook::hook();
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerPickUpItemEvent>(
            [](ll::event::PlayerPickUpItemEvent&) { gTakeAttempted = true; },
            ll::event::EventPriority::Highest
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        FullInventoryPickupHook::unhook();
    }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace
} // namespace abp::upgrade::pickup

ABP_REGISTER_MODULE(abp::upgrade::pickup::FullInventoryPickup);
