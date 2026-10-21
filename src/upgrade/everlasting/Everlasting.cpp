// Everlasting upgrade: a dropped backpack with this upgrade never despawns, can't be destroyed
// (fire, lava, cactus, explosions, any damage) and is rescued when it falls out of the world.
// Applies to every dropped backpack item whose storage has the upgrade, so a backpack dropped on
// death is everlasting too.

#include "upgrade/everlasting/Everlasting.h"

#include <algorithm>
#include <cmath>
#include <optional>
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
#include "mc/world/level/Level.h"

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
    return isEverlasting(actor.item());
}

/// A safe spot for an item at `pos`: on top of the nearest column with solid ground.
std::optional<Vec3> groundNear(BlockSource& region, Vec3 const& pos, int radius) {
    int   x0   = static_cast<int>(std::floor(pos.x));
    int   z0   = static_cast<int>(std::floor(pos.z));
    short minY = region.getMinHeight();
    for (int r = 0; r <= radius; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue; // ring only
                short top = region.getAboveTopSolidBlock(x0 + dx, z0 + dz, false, false);
                if (top > minY) {
                    return Vec3{
                        static_cast<float>(x0 + dx) + 0.5f,
                        static_cast<float>(top) + 0.25f,
                        static_cast<float>(z0 + dz) + 0.5f
                    };
                }
            }
        }
    }
    return std::nullopt;
}
} // namespace

bool rescueFromVoid(ItemActor& actor) {
    if (!everlastingActor(actor)) return false;
    auto& region = actor.getDimensionBlockSource();
    auto  pos    = actor.getPosition();
    auto  target = groundNear(region, pos, std::max(0, config::get<EverlastingConfig>().rescueRadius));
    if (!target) {
        // No ground anywhere near (the End, void worlds): hover just above the bottom of the world.
        target = Vec3{pos.x, static_cast<float>(region.getMinHeight()) + 1.0f, pos.z};
        SynchedActorDataAccess::setActorFlag(actor.getEntityContext(), ActorFlags::HasGravity, false);
    }
    const_cast<Vec3&>(actor.getPosDelta()) = Vec3::ZERO();
    actor.teleport(*target, actor.getDimensionId());
    return true;
}

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

// Falling below the world kills items: rescue everlasting backpacks instead.
LL_TYPE_INSTANCE_HOOK(EverlastingOutOfWorldHook, ll::memory::HookPriority::Normal, Actor, &Actor::$outOfWorld, void) {
    if (hasType(ActorType::ItemEntity) && rescueFromVoid(static_cast<ItemActor&>(static_cast<Actor&>(*this)))) return;
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
            trFor(player, "abp.everlasting.describe1", "§7Dropped, this backpack never despawns"),
            trFor(player, "abp.everlasting.describe2", "§7and can't be destroyed or lost in the void."),
        };
    }
};

ABP_REGISTER_UPGRADE(EverlastingUpgrade);

// ---- module -------------------------------------------------------------------------------------

/// Installs the hooks and keeps dropped everlasting backpacks from despawning (age reset) and
/// from falling out of the world.
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
        EverlastingOutOfWorldHook::unhook();
        EverlastingFireImmuneHook::unhook();
        EverlastingInvulnerableHook::unhook();
        EverlastingHurtHook::unhook();
    }

private:
    void tick(Level& level) {
        if (++mTicks < std::max(1, config::get<EverlastingConfig>().checkInterval)) return;
        mTicks = 0;
        if (!game::Storage::get().available()) return;
        for (auto* actor : level.getRuntimeActorList()) {
            if (!actor || actor->mRemoved || !actor->hasType(ActorType::ItemEntity)) continue;
            auto& item = static_cast<ItemActor&>(*actor);
            if (!game::isBackpack(item.item()) || !everlastingActor(item)) continue;
            item.age()   = 0; // never reaches the despawn age
            auto& region = item.getDimensionBlockSource();
            if (item.getPosition().y < static_cast<float>(region.getMinHeight())) {
                (void)rescueFromVoid(item);
            } else if (!item.getStatusFlag(ActorFlags::HasGravity) && groundNear(region, item.getPosition(), 0)) {
                // Hovering since a void rescue without ground: there is ground now, fall again.
                SynchedActorDataAccess::setActorFlag(item.getEntityContext(), ActorFlags::HasGravity, true);
            }
        }
    }

    ll::event::ListenerPtr mListener;
    int                    mTicks = 0;
};

} // namespace abp::upgrade::everlasting

ABP_REGISTER_MODULE(abp::upgrade::everlasting::EverlastingModule);
