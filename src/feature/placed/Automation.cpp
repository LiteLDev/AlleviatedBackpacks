// Vanilla automation for placed backpacks: hoppers (and hopper minecarts) above or beside a placed
// backpack insert into it, hoppers below pull from it, droppers facing it insert into it. All of
// it goes through the backpack inventory with `Source::Automation`, so upgrade filters apply.
//
// The backpack block has no block entity (custom blocks can't), so vanilla finds no container
// there. We hook the hopper's push / pull-from-above steps and the dropper's dispense and handle
// placed-backpack targets ourselves; everything else goes to vanilla unchanged.

#include <cmath>
#include <random>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/world/Container.h"
#include "mc/world/Facing.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/DropperBlock.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/deps/core/math/Vec3.h"

#include "feature/placed/ContainerIO.h"
#include "feature/placed/PlacedBackpacks.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/Module.h"
#include "upgrade/inception/Inception.h"

namespace abp::placed {

namespace {
core::AccessContext const kAutomation{core::Source::Automation};

BlockPos blockOf(Vec3 const& v) {
    return BlockPos{
        static_cast<int>(std::floor(v.x)),
        static_cast<int>(std::floor(v.y)),
        static_cast<int>(std::floor(v.z)),
    };
}

} // namespace

// Hopper pushing into the block it faces (down or sideways).
LL_TYPE_INSTANCE_HOOK(
    PlacedHopperPushHook,
    ll::memory::HookPriority::Normal,
    Hopper,
    &Hopper::_pushOutItems,
    bool,
    BlockSource& region,
    Container&   fromContainer,
    Vec3 const&  position,
    int          attachedFace
) {
    if (config::get<PlacedConfig>().hopperAutomation && attachedFace != 1) {
        auto target = neighbor(blockOf(position), attachedFace);
        if (auto bp = PlacedBackpacks::get().backpackAt(region, target)) {
            // Vanilla hoppers move one item per transfer. SB getInventoryForInputOutput: with
            // Inception the backpacks inside take it when the first inventory doesn't.
            for (auto const& inv : upgrade::inception::upgradeInventories(bp)) {
                if (pullFromContainer(fromContainer, inv->inventory(), 1, attachedFace, kAutomation) > 0) return true;
            }
            return false;
        }
    }
    return origin(region, fromContainer, position, attachedFace);
}

// Hopper pulling from the block above it.
LL_TYPE_INSTANCE_HOOK(
    PlacedHopperPullHook,
    ll::memory::HookPriority::Normal,
    Hopper,
    &Hopper::_tryPullInItemsFromAboveContainer,
    bool,
    BlockSource& region,
    Container&   toContainer,
    Vec3 const&  pos
) {
    if (config::get<PlacedConfig>().hopperAutomation) {
        auto above = neighbor(blockOf(pos), 1);
        if (auto bp = PlacedBackpacks::get().backpackAt(region, above)) {
            for (auto const& inv : upgrade::inception::upgradeInventories(bp)) {
                if (pushToContainer(inv->inventory(), toContainer, 1, 0, kAutomation) > 0) return true;
            }
            return false;
        }
    }
    return origin(region, toContainer, pos);
}

// Dropper facing a placed backpack: inserts one random item instead of spitting it out.
LL_TYPE_INSTANCE_HOOK(
    PlacedDropperHook,
    ll::memory::HookPriority::Normal,
    DropperBlock,
    &DropperBlock::$dispenseFrom,
    void,
    BlockSource&    region,
    BlockPos const& pos
) {
    if (config::get<PlacedConfig>().dropperAutomation) {
        auto face = region.getBlock(pos).getState<int>(VanillaStates::FacingDirection());
        if (face) {
            auto target = neighbor(pos, *face);
            if (auto bp = PlacedBackpacks::get().backpackAt(region, target)) {
                auto* container = containerAt(region, pos);
                std::vector<int> filled;
                for (int i = 0; container && i < container->getContainerSize(); ++i) {
                    if (!container->getItem(i).isNull()) filled.push_back(i);
                }
                // No container / empty dropper: vanilla (it plays the "failed" click).
                if (filled.empty()) return origin(region, pos);
                static std::mt19937 rng{std::random_device{}()};
                int  slot = filled[std::uniform_int_distribution<std::size_t>(0, filled.size() - 1)(rng)];
                auto data = game::ItemConv::toData(container->getItem(slot)).withCount(1);
                bool ok   = upgrade::inception::insertForUpgrades(bp, data, kAutomation) > 0;
                if (ok) container->removeItem(slot, 1);
                region.getLevel().broadcastLevelEvent(
                    ok ? SharedTypes::Legacy::LevelEvent::SoundClick : SharedTypes::Legacy::LevelEvent::SoundClickFail,
                    pos.center(),
                    0,
                    nullptr
                );
                return;
            }
        }
    }
    origin(region, pos);
}

/// Installs the automation hooks.
class PlacedAutomation {
public:
    void install() {
        PlacedHopperPushHook::hook();
        PlacedHopperPullHook::hook();
        PlacedDropperHook::hook();
    }
    void uninstall() {
        PlacedDropperHook::unhook();
        PlacedHopperPullHook::unhook();
        PlacedHopperPushHook::unhook();
    }
};

} // namespace abp::placed

ABP_REGISTER_MODULE(abp::placed::PlacedAutomation);
