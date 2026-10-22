#ifdef ABP_CLIENT

// Client side of "open another player's backpack" (SB's CommonEventHandler.interactWithEntity on
// the client): a right click in game with the crosshair on another player sends the hit to the
// server (`openOtherPlayer` key action), which checks it and opens the backpack. Like SB, the
// client only sends when the hit is on the body from behind.

#include <cmath>
#include <numbers>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/MouseInputEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/HitResult.h"

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/input/InputState.h"
#include "client/net/ClientNet.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::access {

namespace {

float radians(float deg) { return deg * std::numbers::pi_v<float> / 180.0f; }

/// Client thread: the crosshair is on another player's back -> ask the server to open it.
void tryOpenLookedAt() {
    if (!client::net::serverReady()) return;
    auto* self = client::currentPlayer();
    if (!self) return;
    auto& hit = self->getLevel().getHitResult();
    if (hit.mType != HitResultType::Entity) return;
    auto* actor = hit.getEntity();
    if (!actor || !actor->isPlayer() || actor == self) return;
    auto& target = static_cast<Player&>(*actor);
    Vec3  at     = hit.mPos;
    // SB checks on the client before sending: body height and from behind.
    auto  feet   = target.getFeetPos();
    float localY = at.y - feet.y;
    if (!(localY >= 0.9f && localY < 1.6f)) return;
    auto  from = self->getFeetPos();
    float dx = feet.x - from.x, dz = feet.z - from.z, dy = feet.y - from.y;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-4f) return;
    float pitch = radians(target.getRotation().x);
    float yaw   = radians(target.getYBodyRotation());
    if ((dx / len) * (-std::sin(yaw) * std::cos(pitch)) + (dz / len) * (std::cos(yaw) * std::cos(pitch)) <= 0.0f) return;
    log().info("access: asking to open {}'s backpack", target.getRealName());
    client::net::sendKeyAction(nlohmann::json{
        {"action", "openOtherPlayer"},
        {"target", target.getRuntimeID().rawID},
        {"hit", nlohmann::json::array({at.x, at.y, at.z})},
    });
}

class OtherPlayerClientModule {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::MouseInputEvent>(
            [](ll::event::MouseInputEvent& ev) {
                if (static_cast<int>(ev.actionButtonId()) != MouseAction::ActionRight) return;
                if (ev.buttonData() != MouseAction::DataDown) return;
                auto& snap = client::input::detail::snapshot();
                if (!snap.inLevel.load() || snap.cursorScreen.load()) return;
                client::input::post([] { tryOpenLookedAt(); });
            }
        );
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace
} // namespace abp::access

ABP_REGISTER_MODULE(abp::access::OtherPlayerClientModule);

#endif // ABP_CLIENT
