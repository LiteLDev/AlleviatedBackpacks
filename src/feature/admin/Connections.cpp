// SB `noConnectionBlocks` / `allBlockConnectionsDisallowed`: blocks that may not connect to
// backpacks. On Bedrock the blocks that reach into placed backpacks are hoppers (and hopper
// minecarts) and droppers (placed/Automation.cpp); a listed one leaves the placed backpack alone,
// as if it were a block without an inventory.

#include <cmath>

#include "ll/api/memory/Hook.h"

#include "mc/world/Container.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/DropperBlock.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/deps/core/math/Vec3.h"

#include "feature/admin/SbConfig.h"
#include "feature/placed/ContainerIO.h"
#include "feature/placed/PlacedBackpacks.h"
#include "mod/Module.h"

namespace abp::admin {

namespace {

BlockPos blockOf(Vec3 const& v) {
    return BlockPos{static_cast<int>(std::floor(v.x)), static_cast<int>(std::floor(v.y)), static_cast<int>(std::floor(v.z))};
}

bool placedBackpackAt(BlockSource& region, BlockPos const& pos) {
    return placed::PlacedBackpacks::isBackpackBlock(region.getBlock(pos))
        && placed::PlacedBackpacks::get().at(placed::locationOf(region, pos)).has_value();
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    SbNoConnectionHopperPushHook,
    ll::memory::HookPriority::High,
    Hopper,
    &Hopper::_pushOutItems,
    bool,
    BlockSource& region,
    Container&   fromContainer,
    Vec3 const&  position,
    int          attachedFace
) {
    if (attachedFace != 1 && isBlockConnectionDisallowed("minecraft:hopper")
        && placedBackpackAt(region, placed::neighbor(blockOf(position), attachedFace))) {
        return false;
    }
    return origin(region, fromContainer, position, attachedFace);
}

LL_TYPE_INSTANCE_HOOK(
    SbNoConnectionHopperPullHook,
    ll::memory::HookPriority::High,
    Hopper,
    &Hopper::_tryPullInItemsFromAboveContainer,
    bool,
    BlockSource& region,
    Container&   toContainer,
    Vec3 const&  pos
) {
    if (isBlockConnectionDisallowed("minecraft:hopper") && placedBackpackAt(region, placed::neighbor(blockOf(pos), 1))) return false;
    return origin(region, toContainer, pos);
}

LL_TYPE_INSTANCE_HOOK(
    SbNoConnectionDropperHook,
    ll::memory::HookPriority::High,
    DropperBlock,
    &DropperBlock::$dispenseFrom,
    void,
    BlockSource&    region,
    BlockPos const& pos
) {
    if (isBlockConnectionDisallowed("minecraft:dropper")) {
        auto face = region.getBlock(pos).getState<int>(VanillaStates::FacingDirection());
        if (face && placedBackpackAt(region, placed::neighbor(pos, *face))) return;
    }
    origin(region, pos);
}

namespace {
class SbConnectionRules {
public:
    void install() {
        SbNoConnectionHopperPushHook::hook();
        SbNoConnectionHopperPullHook::hook();
        SbNoConnectionDropperHook::hook();
    }
    void uninstall() {
        SbNoConnectionDropperHook::unhook();
        SbNoConnectionHopperPullHook::unhook();
        SbNoConnectionHopperPushHook::unhook();
    }
};
} // namespace

} // namespace abp::admin

ABP_REGISTER_MODULE(abp::admin::SbConnectionRules);
