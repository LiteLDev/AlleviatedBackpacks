// Mob Catcher capture effect on the client (client build): SB MobCatcherCaptureEffectRenderer.
// A copy of the captured mob (display entity with the mob's synced data and properties) is drawn
// in the world pass after the block entities, pinned to the mob's yaw, scaled by
// 1 - easeInCubic(progress) about the collapse position for the animation's ticks (SB: 5). The
// poof particles and pop sound at the end come from the server (CaptureEffect.h).

#ifdef ABP_CLIENT

#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/network/packet/Packet.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/world/actor/Actor.h"

#include "client/render/EntityPreview.h"
#include "client/render/WorldRender.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/mobcatcher/CaptureEffect.h"

namespace abp::upgrade::mobcatcher {

namespace {

namespace render = ::abp::client::render;

double now() {
    using namespace std::chrono;
    static auto const start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

struct Effect {
    std::unique_ptr<render::DisplayEntity> entity;
    std::string                            type;
    Vec3                                   pos{};
    Vec3                                   collapse{};
    float                                  yaw   = 0;
    double                                 start = 0;
    int                                    ticks = kCaptureAnimationTicks;
};
std::vector<Effect>             gEffects;
std::unordered_set<std::string> gFailures; ///< SB ENTITY_RENDER_FAILURES

/// True when a server-to-client packet really reached the client (not this process's server).
bool fromServer(NetEventCallback& callback) {
    auto handler = ll::service::getServerNetworkHandler();
    return !handler || static_cast<NetEventCallback*>(handler.as_ptr()) != &callback;
}

Vec3 vec(nlohmann::json const& j) {
    if (!j.is_array() || j.size() != 3) return {};
    return Vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

/// SB Easing.EASE_IN_CUBIC.
float easeInCubic(float t) { return t * t * t; }

void renderEffects(BaseActorRenderContext& rc) {
    if (gEffects.empty()) return;
    double t = now();
    std::erase_if(gEffects, [&](Effect& e) {
        float progress = static_cast<float>((t - e.start) / (e.ticks * 0.05));
        if (progress >= 1.f) return true;
        if (!e.entity) return false;
        float     scale = 1.f - easeInCubic(std::max(0.f, progress));
        Vec3      c     = render::toCameraSpace(rc, e.collapse);
        glm::mat4 m     = glm::translate(glm::mat4(1.f), glm::vec3(c.x, c.y, c.z));
        m               = glm::scale(m, glm::vec3(scale));
        m               = glm::translate(m, glm::vec3(e.pos.x - e.collapse.x, e.pos.y - e.collapse.y, e.pos.z - e.collapse.z));
        if (!render::renderEntityInWorld(rc, e.entity->actor(), m, e.yaw, &e.pos)) {
            if (gFailures.insert(e.type).second) log().warn("mob catcher: unable to render the capture effect for {}", e.type);
            e.entity.reset();
        }
        return false;
    });
}

class CaptureEffectHandler : public ll::network::PacketHandlerBase<CaptureEffectHandler, MobCaptureEffectPacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, MobCaptureEffectPacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        try {
            Effect e;
            e.type     = p.body.value("type", std::string{});
            e.pos      = vec(p.body.value("pos", nlohmann::json()));
            e.collapse = vec(p.body.value("collapse", nlohmann::json()));
            e.yaw      = p.body.value("yaw", 0.f);
            e.ticks    = std::max(1, p.body.value("ticks", kCaptureAnimationTicks));
            e.start    = now();
            if (e.type.empty()) return;
            // SB addNonRenderingEffect for types that failed before: nothing is drawn.
            if (!gFailures.contains(e.type)) {
                e.entity = render::createDisplayEntity(e.type, p.body.value("data", nlohmann::json::array()));
                if (e.entity) {
                    if (auto props = p.body.value("properties", std::string{}); !props.empty()) render::applyProperties(e.entity->actor(), props);
                } else if (gFailures.insert(e.type).second) {
                    log().warn("mob catcher: unable to create the capture effect entity for {}", e.type);
                }
            }
            log().info("mob catcher: capture effect for {} ({} ticks)", e.type, e.ticks);
            gEffects.push_back(std::move(e));
        } catch (std::exception const& ex) {
            log().warn("mob catcher: bad capture effect packet: {}", ex.what());
        }
    }
};

} // namespace

class CaptureEffectClient {
public:
    void install() {
        render::addWorldPass("mobcatcher.capture_effect", [](BaseActorRenderContext& rc) { renderEffects(rc); });
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { gEffects.clear(); });
    }
    void uninstall() {
        render::removeWorldPass("mobcatcher.capture_effect");
        ll::event::EventBus::getInstance().removeListener(mExit);
        gEffects.clear();
    }

private:
    ll::event::ListenerPtr mExit;
};

} // namespace abp::upgrade::mobcatcher

ABP_REGISTER_MODULE(abp::upgrade::mobcatcher::CaptureEffectClient);

#endif // ABP_CLIENT
