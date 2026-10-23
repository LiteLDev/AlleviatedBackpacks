#ifdef ABP_CLIENT

// Upgrade activity particles and sounds (P2-11, GX-1), SC's IUpgradeRenderer for cooking
// (CookingUpgradeRenderer: smoke + flame every call while burning, and the furnace crackle with a
// 10% chance per call) and jukebox (notes while playing):
//   worn   - BackpackLayerRenderer.renderUpgrades: 1 in 32 render frames, at the backpack's back
//            face (0.8 up, 0.7 behind the body; 25 degree tilt and 0.9 when crouching);
//   placed - BackpackBlock.animateTick: 0.41 in front of the block centre, rotated with facing.
//            animateTick runs for random blocks around the player (ClientLevel.animateTick: 667
//            picks per tick at radius 16 and at radius 32, each axis `nextInt(r) - nextInt(r)`),
//            so a placed backpack's chance per tick is computed from its offset to the player.
// Bedrock has no per-frame client hook here, so the per-frame chance of worn backpacks is applied
// per client tick scaled to 60 frames per second.

#include <algorithm>
#include <cmath>
#include <optional>
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

#include "mc/client/game/ClientInstance.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/audio/ServerSoundHandle.h"

#include "client/Client.h"
#include "client/render/ModelState.h"
#include "core/render/BackpackModel.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
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

/// Level.playLocalSound on the client: SoundEvents.FURNACE_FIRE_CRACKLE is Bedrock's
/// "block.furnace.lit" (the furnace's fire_crackle sounds), volume 1, pitch 1.
void playLocal(char const* sound, Vec3 const& at, float volume, float pitch) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        log().info("upgrade particles: playing {} at {:.2f},{:.2f},{:.2f}", sound, at.x, at.y, at.z);
    }
    if (auto* c = currentClient()) c->getSoundEngine()->play(sound, at, volume, pitch, std::nullopt);
}

/// One activity's particles at a backpack whose middle face point is `point(offset)`.
template <class Point>
void renderActivity(ILevel& level, std::string const& activity, Point const& point) {
    if (activity == "cooking") {
        // CookingUpgradeRenderer: level.random.nextDouble() < 0.1 -> the crackle at the face centre.
        if (rnd() < 0.1f) playLocal("block.furnace.lit", point(Vec3{0.0f, 0.0f, 0.0f}), 1.0f, 1.0f);
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

/// Chance per tick that Java's ClientLevel.animateTick picks the block at `d` (block offset from
/// the player's block): 667 picks each at radius 16 and 32, every axis offset distributed as
/// `nextInt(r) - nextInt(r)` (P(d) = (r - |d|) / r^2).
float animateTickChance(int dx, int dy, int dz) {
    auto axis = [](int d, int r) { return std::max(0, r - std::abs(d)) / static_cast<float>(r * r); };
    float p16 = axis(dx, 16) * axis(dy, 16) * axis(dz, 16);
    float p32 = axis(dx, 32) * axis(dy, 32) * axis(dz, 32);
    // Two independent picks per iteration; 1 - (1 - p)^667 per radius.
    float none = std::pow(1.0f - p16, 667.0f) * std::pow(1.0f - p32, 667.0f);
    return 1.0f - none;
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
        if (!snap || snap->model.activity.empty()) continue;
        bool placed = actor->getTypeName() == kDisplayEntityId;
        // Dropped / framed backpacks (ItemEntityModels): SB shows no activity for item entities.
        if (placed && hasEntityXf(actor)) continue;
        if (placed) {
            auto fl = [](float v) { return static_cast<int>(std::floor(v)); };
            if (rnd() >= animateTickChance(fl(pos.x) - fl(me.x), fl(pos.y) - fl(me.y), fl(pos.z) - fl(me.z))) continue;
        } else if (!roll()) {
            continue;
        }
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
