#ifdef ABP_CLIENT

// Upgrade activity particles (P2-11), SC's IUpgradeRenderer for cooking (smoke + flame while
// burning) and jukebox (notes while playing):
//   worn   - BackpackLayerRenderer.renderUpgrades: 1 in 32 render frames, at the backpack's back
//            face (0.8 up, 0.7 behind the body; 25 degree tilt and 0.9 when crouching);
//   placed - BackpackBlock.animateTick: 0.41 in front of the block centre, rotated with facing.
// Bedrock has no per-frame client hook here, so the per-frame chance is applied per client tick
// scaled to 60 frames per second.

#include <cmath>
#include <random>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/ILevel.h"

#include "client/Client.h"
#include "client/render/ModelState.h"
#include "core/render/BackpackModel.h"
#include "game/BackpackItem.h"
#include "mod/Module.h"

namespace abp::client::render {

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

constexpr float kPi = 3.14159265f;

/// Rotates (x, y, z) about +Y by `deg` degrees (right handed).
Vec3 rotY(Vec3 v, float deg) {
    float a = deg * kPi / 180.0f, c = std::cos(a), s = std::sin(a);
    return Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}
Vec3 rotX(Vec3 v, float deg) {
    float a = deg * kPi / 180.0f, c = std::cos(a), s = std::sin(a);
    return Vec3{v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}

void spawn(ILevel& level, char const* effect, Vec3 const& at) { level.addParticleEffect(HashedString(effect), at); }

/// One activity's particles at a backpack whose middle face point is `point(offset)`.
template <class Point>
void renderActivity(ILevel& level, std::string const& activity, Point const& point) {
    if (activity == "cooking") {
        Vec3 at = point(Vec3{rnd() * 0.6f - 0.3f, rnd() * 6.0f / 16.0f, 0.02f});
        spawn(level, "minecraft:basic_smoke_particle", at);
        spawn(level, "minecraft:basic_flame_particle", at);
    } else if (activity == "jukebox") {
        if (std::uniform_int_distribution<int>(0, 1)(rng()) != 0) return;
        spawn(level, "minecraft:note_particle", point(Vec3{rnd() * 0.6f - 0.3f, 0.5f + rnd() * 6.0f / 16.0f, rnd() * 0.6f - 0.1f}));
    }
}

bool roll() {
    // 1 in 32 per frame at 60 frames per second = 3 in 32 per client tick.
    return std::uniform_int_distribution<int>(0, core::render::kUpgradeParticleChance - 1)(rng()) < 3;
}

void tick() {
    auto* player = currentPlayer();
    if (!player) return;
    auto& level = player->getLevel();
    auto  me    = player->getPosition();
    for (auto* actor : level.getRuntimeActorList()) {
        if (!actor) continue;
        auto pos = actor->getPosition();
        if ((pos - me).lengthSqr() > 32.0f * 32.0f) continue;
        auto const& chest = actor->getArmor(SharedTypes::Legacy::ArmorSlot::Torso);
        if (chest.isNull() || !game::backpackTierOf(chest.getTypeName())) continue;
        auto snap = modelFor(chest);
        if (!snap || snap->model.activity.empty() || !roll()) continue;
        bool placed = actor->getTypeName() == kDisplayEntityId;
        for (auto const& activity : snap->model.activity) {
            if (placed) {
                // BackpackBlock.getBackpackMiddleFacePoint: +0.41 z, rotated by the facing.
                float yaw = actor->getRotation().y;
                renderActivity(level, activity, [&](Vec3 v) {
                    v.z += 0.41f;
                    return rotY(v, -yaw) + pos;
                });
            } else {
                // BackpackLayerRenderer.getBackpackMiddleFacePoint.
                bool  crouch  = actor->isSneaking();
                float bodyYaw = actor->hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(actor)->getYBodyRotation() : actor->getRotation().y;
                Vec3  feet    = actor->getFeetPos();
                renderActivity(level, activity, [&](Vec3 v) {
                    v = rotX(v, crouch ? 25.0f : 0.0f);
                    v = v + Vec3{0.0f, 0.8f, crouch ? 0.9f : 0.7f};
                    return rotY(v, -(bodyYaw - 180.0f)) + feet;
                });
            }
        }
    }
}

} // namespace

class UpgradeParticleModule {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); });
    }
    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick.reset();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace abp::client::render

ABP_REGISTER_MODULE(abp::client::render::UpgradeParticleModule);

#endif // ABP_CLIENT
