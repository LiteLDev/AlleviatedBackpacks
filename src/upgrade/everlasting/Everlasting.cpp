// Everlasting upgrade: a dropped backpack with this upgrade never despawns, can't be destroyed
// (fire, lava, cactus, explosions, any damage) and floats up out of liquids and the void, then
// hovers (SB `EverlastingBackpackItemEntity`).
// Applies to every dropped backpack item whose storage has the upgrade, so a backpack dropped on
// death is everlasting too.

#include "upgrade/everlasting/Everlasting.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <unordered_map>
#include <variant>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/deps/shared_types/v1_26_20/block/MaterialType.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/level/Level.h"

#include "feature/wearable/Wearable.h"
#include "game/BackpackItem.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("everlasting", abp::upgrade::everlasting::EverlastingConfig);

namespace abp::upgrade::everlasting {

bool isEverlasting(ItemStack const& stack) {
    if (stack.isNull() || !game::isBackpack(stack)) return false;
    auto uuid = game::backpackUuid(stack);
    if (!uuid) return false;
    auto& storage = game::Storage::get();
    if (!storage.available()) return false;
    auto bp = storage.find(*uuid);
    return bp && hasActive(*bp, kId);
}

namespace {
/// Server-side item actors only: client-side copies (client build) run on the client thread and
/// must not touch the storage.
bool everlastingActor(ItemActor const& actor) {
    if (actor.getLevel().isClientSide()) return false;
    auto const& stack = actor.item();
    if (game::isBackpack(stack) && !game::backpackUuid(stack)) {
        // A backpack dropped by a mob (U19) generates its contents, random upgrades included, on
        // first open. Generate them now so a rolled Everlasting upgrade protects the drop too.
        if (auto loot = wearable::pendingLoot(stack); loot && loot->upgrades && game::Storage::get().available()) {
            auto& mutableStack = const_cast<ItemActor&>(actor).item();
            bool  changed      = false;
            (void)game::Storage::get().forItem(mutableStack, &changed);
        }
    }
    return isEverlasting(stack);
}

} // namespace

namespace {
// ---- hooks --------------------------------------------------------------------------------------

ActorHurtResult notHurt() {
    ActorHurtResult r;
    r.mHurtOrDamage         = std::variant<bool, float>(false);
    r.mShouldAllowKnockback = false;
    return r;
}

// The hook macro defines an unused counter variable.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
LL_TYPE_INSTANCE_HOOK(
    EverlastingHurtHook,
    ll::memory::HookPriority::Normal,
    ItemActor,
    &ItemActor::$_hurt,
    ActorHurtResult,
    ActorDamageSource const& source,
    float                    damage,
    HurtParameters const&    params
) {
    if (everlastingActor(*this)) return notHurt();
    return origin(source, damage, params);
}

LL_TYPE_INSTANCE_HOOK(
    EverlastingInvulnerableHook,
    ll::memory::HookPriority::Normal,
    ItemActor,
    &ItemActor::$isInvulnerableTo,
    bool,
    ActorDamageSource const& source
) {
    if (everlastingActor(*this)) return true;
    return origin(source);
}

LL_TYPE_INSTANCE_HOOK(
    EverlastingFireImmuneHook,
    ll::memory::HookPriority::Normal,
    ItemActor,
    &ItemActor::$isFireImmune,
    bool
) {
    if (everlastingActor(*this)) return true;
    return origin();
}

// Falling below the world kills items. SB's entity ignores it (`onBelowWorld` does nothing): below
// `minY + 1` it counts as "in water" and floats back up (see EverlastingModule::tick).
LL_TYPE_INSTANCE_HOOK(EverlastingOutOfWorldHook, ll::memory::HookPriority::Normal, Actor, &Actor::$outOfWorld, void) {
    if (hasType(ActorType::ItemEntity) && everlastingActor(static_cast<ItemActor&>(static_cast<Actor&>(*this)))) return;
    origin();
}
#pragma clang diagnostic pop
} // namespace

// ---- the upgrade --------------------------------------------------------------------------------

class EverlastingUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const info{
            .id           = std::string(kId),
            .itemId       = "alleviatedbackpacks:everlasting_upgrade",
            .nameKey      = "abp.everlasting.name",
            .nameFallback = "Everlasting Upgrade",
            .toggleable   = false,
        };
        return info;
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const&) const override {
        return {
            trFor(player, "abp.everlasting.describe1", "§7Backpack becomes indestructible"),
            trFor(player, "abp.everlasting.describe2", "§7Can't despawn or fall into void"),
        };
    }
};

ABP_REGISTER_UPGRADE(EverlastingUpgrade);

// ---- module -------------------------------------------------------------------------------------

/// SB `EverlastingBackpackItemEntity` for Bedrock item actors: every tick a dropped everlasting
/// backpack never ages (lifespan = max), shows a happy villager particle with a 1/20 chance, and
/// while it still has gravity: in water or lava (or below `minY + 1`, which SB treats as water) it
/// floats up (`dy = min(0.7, dy + 0.06)`, vanilla's bubble column rise); once out after floating it
/// hovers for good (no gravity, zero velocity). Invulnerability and fire immunity are the hooks above.
class EverlastingModule {
public:
    void install() {
        EverlastingHurtHook::hook();
        EverlastingInvulnerableHook::hook();
        EverlastingFireImmuneHook::hook();
        EverlastingOutOfWorldHook::hook();
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [this](ll::event::ServerLevelTickEvent& ev) { tick(ev.level()); }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        mTracked.clear();
        EverlastingOutOfWorldHook::unhook();
        EverlastingFireImmuneHook::unhook();
        EverlastingInvulnerableHook::unhook();
        EverlastingHurtHook::unhook();
    }

private:
    struct State {
        bool                floatingUp = false; ///< SB `wasFloatingUp` (not persisted, as in SB)
        std::optional<Vec3> hover;              ///< where it hovers (no gravity, no motion)
    };

    void tick(Level& level) {
        if (!game::Storage::get().available()) return;
        // Discover dropped everlasting backpacks (a storage lookup each) every few ticks; tracked
        // ones are processed every tick, as SB's entity tick.
        if (++mTicks >= std::max(1, config::get<EverlastingConfig>().checkInterval)) {
            mTicks = 0;
            for (auto* actor : level.getRuntimeActorList()) {
                if (!actor || actor->mRemoved || !actor->hasType(ActorType::ItemEntity)) continue;
                auto id = actor->getOrCreateUniqueID().rawID;
                if (mTracked.contains(id)) continue;
                auto& item = static_cast<ItemActor&>(*actor);
                if (game::isBackpack(item.item()) && everlastingActor(item)) mTracked.emplace(id, State{});
            }
        }
        for (auto it = mTracked.begin(); it != mTracked.end();) {
            ActorUniqueID uid;
            uid.rawID   = it->first;
            auto* actor = level.fetchEntity(uid, false);
            if (!actor || actor->mRemoved || !actor->hasType(ActorType::ItemEntity)) {
                it = mTracked.erase(it);
                continue;
            }
            tickOne(level, static_cast<ItemActor&>(*actor), it->second);
            ++it;
        }
    }

    void tickOne(Level& level, ItemActor& item, State& state) {
        item.age() = 0; // never reaches the despawn age
        auto pos   = item.getPosition();
        if (mRandom() % 20 == 0) {
            std::uniform_real_distribution<float> d(-0.5f, 0.5f), h(0.0f, 0.5f);
            Vec3 at{pos.x + d(mRandom), pos.y + h(mRandom), pos.z + d(mRandom)};
            level.spawnParticleEffect("minecraft:villager_happy", at, &item.getDimension());
        }
        auto& delta = const_cast<Vec3&>(item.getPosDelta());
        if (!item.getStatusFlag(ActorFlags::HasGravity)) {
            // Hovering for good (SB: no gravity, zero motion). Bedrock's item physics still pulls
            // items down without the gravity flag: keep it where it stopped.
            if (state.hover) {
                delta = Vec3::ZERO();
                if (pos.distanceTo(*state.hover) > 0.001f) item._setPos(*state.hover);
            }
            return;
        }
        auto& region  = item.getDimensionBlockSource();
        bool  inWater = item.isInWater() || pos.y < static_cast<float>(region.getMinHeight()) + 1.0f;
        bool  inLava  = !inWater && region.containsMaterial(item.getAABB(), SharedTypes::v1_26_20::MaterialType::Lava);
        if (inWater || inLava) {
            delta.y          = std::min(0.7f, delta.y + 0.06f);
            state.floatingUp = true;
        } else if (state.floatingUp) {
            SynchedActorDataAccess::setActorFlag(item.getEntityContext(), ActorFlags::HasGravity, false);
            delta       = Vec3::ZERO();
            state.hover = pos;
        }
    }

    ll::event::ListenerPtr                   mListener;
    int                                      mTicks = 0;
    std::unordered_map<std::int64_t, State>  mTracked;
    std::minstd_rand                         mRandom{std::random_device{}()};
};

} // namespace abp::upgrade::everlasting

ABP_REGISTER_MODULE(abp::upgrade::everlasting::EverlastingModule);
