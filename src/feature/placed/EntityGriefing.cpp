// GX-2: SB `BackpackBlock.canEntityDestroy` returns false for a placed backpack with an
// Everlasting upgrade, so the Wither (charging / hurt explosion block breaking) and the Ender
// Dragon (breaking through blocks it flies into) leave it alone, as they leave bedrock alone.
//
// Bedrock: WitherBoss::_destroyBlocks and EnderDragon::_checkWalls break the blocks. While one of
// them runs on this thread, removing / replacing / destroying an Everlasting placed backpack block
// is refused (Level::destroyBlock, BlockSource::removeBlock and setBlock). Should the game remove
// it another way, the block-changed handler of PlacedBackpacks re-places it on the next tick (as
// for explosions), so the backpack never drops and never moves.

#include "ll/api/memory/Hook.h"

#include "mc/world/actor/boss/WitherBoss.h"
#include "mc/world/actor/monster/EnderDragon.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/phys/AABB.h"

#include "feature/placed/PlacedBackpacks.h"
#include "game/Storage.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/UpgradeManager.h"

namespace abp::placed {

namespace {
thread_local int tBreaking = 0;

struct BreakingScope {
    BreakingScope() { ++tBreaking; }
    ~BreakingScope() { --tBreaking; }
    BreakingScope(BreakingScope const&)            = delete;
    BreakingScope& operator=(BreakingScope const&) = delete;
};

/// True if the block at `pos` must survive the entity breaking blocks right now.
bool protectedNow(BlockSource const& region, BlockPos const& pos) {
    if (tBreaking <= 0) return false;
    if (!PlacedBackpacks::isBackpackBlock(region.getBlock(pos))) return false;
    if (!everlastingAt(region, pos)) return false;
    return true;
}
} // namespace

bool entityBreakingBlocks() { return tBreaking > 0; }

bool everlastingAt(BlockSource const& region, BlockPos const& pos) {
    auto entry = PlacedBackpacks::get().at(locationOf(region, pos));
    if (!entry || !game::Storage::get().available()) return false;
    auto bp = game::Storage::get().find(entry->uuid);
    return bp && upgrade::hasActive(*bp, "everlasting");
}

// LL_TYPE_INSTANCE_HOOK declares an unused counter in manual-registration mode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"

LL_TYPE_INSTANCE_HOOK(
    AbpWitherDestroyBlocksHook,
    ll::memory::HookPriority::Normal,
    WitherBoss,
    &WitherBoss::_destroyBlocks,
    void,
    ::Level&                       level,
    ::AABB const&                  bb,
    ::BlockSource&                 region,
    int                            range,
    ::WitherBoss::WitherAttackType attackType
) {
    BreakingScope scope;
    origin(level, bb, region, range, attackType);
}

LL_TYPE_INSTANCE_HOOK(AbpDragonCheckWallsHook, ll::memory::HookPriority::Normal, EnderDragon, &EnderDragon::_checkWalls, bool, ::AABB bb) {
    BreakingScope scope;
    return origin(bb);
}

LL_TYPE_INSTANCE_HOOK(
    AbpGriefDestroyBlockHook,
    ll::memory::HookPriority::Normal,
    Level,
    &Level::$destroyBlock,
    bool,
    ::BlockSource&              region,
    ::BlockPos const&           pos,
    bool                        dropResources,
    ::BlockChangeContext const& changeSourceContext
) {
    if (protectedNow(region, pos)) return false;
    return origin(region, pos, dropResources, changeSourceContext);
}

LL_TYPE_INSTANCE_HOOK(
    AbpGriefRemoveBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    &BlockSource::$removeBlock,
    bool,
    ::BlockPos const&           pos,
    ::BlockChangeContext const& changeSourceContext
) {
    if (protectedNow(*this, pos)) return false;
    return origin(pos, changeSourceContext);
}

LL_TYPE_INSTANCE_HOOK(
    AbpGriefSetBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    &BlockSource::$setBlock,
    bool,
    ::BlockPos const&              pos,
    ::Block const&                 block,
    int                            updateFlags,
    ::ActorBlockSyncMessage const* syncMsg,
    ::BlockChangeContext const&    changeSourceContext
) {
    if (tBreaking > 0 && !PlacedBackpacks::isBackpackBlock(block) && protectedNow(*this, pos)) return false;
    return origin(pos, block, updateFlags, syncMsg, changeSourceContext);
}

#pragma clang diagnostic pop

class EntityGriefing {
public:
    void install() {
        AbpWitherDestroyBlocksHook::hook();
        AbpDragonCheckWallsHook::hook();
        AbpGriefDestroyBlockHook::hook();
        AbpGriefRemoveBlockHook::hook();
        AbpGriefSetBlockHook::hook();
    }
    void uninstall() {
        AbpGriefSetBlockHook::unhook();
        AbpGriefRemoveBlockHook::unhook();
        AbpGriefDestroyBlockHook::unhook();
        AbpDragonCheckWallsHook::unhook();
        AbpWitherDestroyBlocksHook::unhook();
    }
};

} // namespace abp::placed

ABP_REGISTER_MODULE(abp::placed::EntityGriefing);
