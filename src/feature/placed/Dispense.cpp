// SB BackpackDispenseBehavior: a dispenser places a backpack as a block in front of it (against
// the floor when there is one, else against the dispenser's side), facing the dispenser (north
// when the dispenser faces up or down). If it can't be placed, nothing happens: the backpack stays
// in the dispenser (SB OptionalDispenseItemBehavior), it is never shot out as an item.
//
// Bedrock: DispenserBlock::dispenseFrom asks the item to `dispense` itself; backpacks place
// through the same path as a sneak-use (`PlacedBackpacks::place`).

#include <cmath>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/DispenserBlock.h"
#include "mc/world/level/block/VanillaStates.h"

#include "feature/placed/PlacedBackpacks.h"
#include "feature/placed/TestSite.h"
#include "game/BackpackItem.h"
#include "game/Storage.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace abp::placed {

namespace {

// LL_TYPE_INSTANCE_HOOK declares an unused counter in manual-registration mode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"

/// Places the backpack in `slot` of a dispenser (see the file comment). Always "handled".
bool dispenseBackpack(::BlockSource& region, ::Container& container, int slot, ::Vec3 const& pos, uchar face) {
    auto const& stack = container.getItem(slot);
    // `pos` is the dispense position (0.7 blocks from the dispenser's centre, in its facing): its
    // block is the one in front of the dispenser.
    BlockPos  target{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    ItemStack item = stack;
    item.set(1);
    auto uuidBefore = game::backpackUuid(item);
    PlacedBackpacks::PlaceResult result = PlacedBackpacks::PlaceResult::Failed;
    try {
        result = PlacedBackpacks::get().placeFromDispenser(region, target, static_cast<int>(face), item);
    } catch (std::exception const& e) {
        log().error("placed: dispensing a backpack failed: {}", e.what());
    }
    if (result == PlacedBackpacks::PlaceResult::Ok) {
        container.removeItem(slot, 1);
    } else if (game::backpackUuid(item) != uuidBefore && stack.mCount == 1) {
        // place() bound a storage to the item before failing: keep that binding.
        container.setItem(slot, item);
    }
    return true; // handled: a backpack is never shot out of a dispenser (nor put on a player)
}

bool isOurs(::BlockSource& region, ::Container& container, int slot) {
    auto const& stack = container.getItem(slot);
    return !stack.isNull() && game::isBackpack(stack) && !region.getLevel().isClientSide();
}

// Backpack items are data-driven (ComponentItem): its `dispense` would put a wearable on an entity
// in front of the dispenser or shoot it out.
LL_TYPE_INSTANCE_HOOK(
    AbpBackpackDispenseHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$dispense,
    bool,
    ::BlockSource& region,
    ::Container&   container,
    int            slot,
    ::Vec3 const&  pos,
    uchar          face
) {
    if (!isOurs(region, container, slot)) return origin(region, container, slot, pos, face);
    return dispenseBackpack(region, container, slot, pos, face);
}

LL_TYPE_INSTANCE_HOOK(
    AbpBackpackDispenseItemHook,
    ll::memory::HookPriority::Normal,
    Item,
    &Item::$dispense,
    bool,
    ::BlockSource& region,
    ::Container&   container,
    int            slot,
    ::Vec3 const&  pos,
    uchar          face
) {
    if (!isOurs(region, container, slot)) return origin(region, container, slot, pos, face);
    return dispenseBackpack(region, container, slot, pos, face);
}

#pragma clang diagnostic pop

class BackpackDispense {
public:
    void install() {
        AbpBackpackDispenseHook::hook();
        AbpBackpackDispenseItemHook::hook();
    }
    void uninstall() {
        AbpBackpackDispenseItemHook::unhook();
        AbpBackpackDispenseHook::unhook();
    }
};

} // namespace

} // namespace abp::placed

ABP_REGISTER_MODULE(abp::placed::BackpackDispense);

// ---- selftests ------------------------------------------------------------------------------------

namespace abp::placed::test {
namespace {

using core::Tier;
using selftest::Context;

/// A dispenser at `at` facing `face` (Facing), holding only `item`.
Container* dispenserAt(Context& ctx, Site& site, BlockPos const& at, int face, ItemStack const& item) {
    auto dispenser = Block::tryGetFromRegistry(HashedString("minecraft:dispenser"));
    ctx.require(dispenser.has_value(), "dispenser block");
    auto facing = dispenser->setState(VanillaStates::FacingDirection(), face);
    ctx.require(facing.has_value(), "dispenser facing");
    site.set(at, *facing);
    auto* c = containerAt(site.region(), at);
    ctx.require(c != nullptr, "dispenser container");
    c->removeAllItems();
    c->setItem(0, item);
    return c;
}

void fire(Site& site, BlockPos const& at) {
    auto const& type = static_cast<DispenserBlock const&>(site.region().getBlock(at).getBlockType());
    type.dispenseFrom(site.region(), at);
}

/// Dropped items near `pos` that are the backpack `uuid`. The test area is shared by every
/// selftest (`toGround`), so other tests' leftovers may lie around: only count this backpack.
int backpackDropsNear(Site const& site, BlockPos const& pos, std::string const& uuid) {
    int n = 0;
    for (auto* a : site.dropsNear(pos)) {
        if (game::isBackpack(a->item()) && game::backpackUuid(a->item()) == uuid) n += a->item().mCount;
    }
    return n;
}

ABP_REGISTER_SELFTEST("placed.dispenser_places", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    // Dispenser facing east (5) on the ground; the backpack goes into the block east of it.
    auto dp     = site.at(0, 0);
    auto target = site.at(1, 0);
    site.touch(target);
    site.set(site.at(1, -1), "minecraft:stone");
    site.set(target, "minecraft:air");
    for (auto* a : site.dropsNear(target)) ctx.info("leftover drop near the dispenser: " + a->item().getTypeName());
    auto  bp = ctx.createBackpack(Tier::Iron);
    auto* c  = dispenserAt(ctx, site, dp, 5, ctx.backpackItem(*bp));
    fire(site, dp);
    auto& placed = PlacedBackpacks::get();
    auto  entry  = placed.at(locationOf(site.region(), target));
    ctx.require(entry.has_value(), "the dispenser placed the backpack in front of it");
    ctx.check(entry->uuid == bp->uuid(), "the same backpack (no copy)");
    ctx.check(entry->facing == 1, "faces the dispenser (west), got " + std::to_string(entry->facing));
    ctx.check(countIn(*c, game::backpackItemId(Tier::Iron)) == 0, "taken out of the dispenser");
    ctx.check(backpackDropsNear(site, target, bp->uuid()) == 0, "nothing shot out as an item");
    co_await ctx.ticks(1);
    ctx.check(PlacedBackpacks::isBackpackBlock(site.region().getBlock(target)), "the block is there");

    // Blocked (a backpack already stands there): the next one stays in the dispenser.
    auto other = ctx.createBackpack(Tier::Leather);
    c->setItem(0, ctx.backpackItem(*other));
    fire(site, dp);
    ctx.check(countIn(*c, game::backpackItemId(Tier::Leather)) == 1, "placing failed: the backpack stays in the dispenser");
    ctx.check(backpackDropsNear(site, target, other->uuid()) == 0, "and is not dropped");
    ctx.check(placed.at(locationOf(site.region(), target))->uuid == bp->uuid(), "the placed one is untouched");
    c->removeAllItems();
});

ABP_REGISTER_SELFTEST("placed.dispenser_facing", [](Context& ctx) -> ll::coro::CoroTask<> {
    // SB: opposite of the dispenser's direction; north for up / down.
    ctx.check(PlacedBackpacks::facingForDispenser(2) == 0, "north -> south");
    ctx.check(PlacedBackpacks::facingForDispenser(3) == 2, "south -> north");
    ctx.check(PlacedBackpacks::facingForDispenser(4) == 3, "west -> east");
    ctx.check(PlacedBackpacks::facingForDispenser(5) == 1, "east -> west");
    ctx.check(PlacedBackpacks::facingForDispenser(0) == 2 && PlacedBackpacks::facingForDispenser(1) == 2, "up / down -> north");
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    // Dispenser facing up, two blocks above the ground: the backpack floats on top of it (placed
    // against the dispenser), facing north.
    auto dp     = site.at(0, 1);
    auto target = site.at(0, 2);
    site.touch(target);
    site.set(target, "minecraft:air");
    auto  bp = ctx.createBackpack(Tier::Gold);
    auto* c  = dispenserAt(ctx, site, dp, 1, ctx.backpackItem(*bp));
    fire(site, dp);
    auto entry = PlacedBackpacks::get().at(locationOf(site.region(), target));
    ctx.require(entry.has_value(), "placed above an upward dispenser");
    ctx.check(entry->facing == 2, "faces north, got " + std::to_string(entry->facing));
    ctx.check(c->getItem(0).isNull(), "consumed");
    co_await ctx.ticks(1);
});

} // namespace
} // namespace abp::placed::test
