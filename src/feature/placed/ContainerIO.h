#pragma once

#include <functional>

#include "core/BackpackInventory.h"

class BlockPos;
class BlockSource;
class Container;
class ItemStack;

namespace abp::placed {

/// Item predicate used by the transfer helpers (filters). Empty = everything.
using ItemPredicate = std::function<bool(core::ItemStackData const&)>;

/// Adds up to `count` items like `item` to the vanilla container `c`: first onto matching stacks,
/// then into empty slots, honouring `Container::canPushInItem(slot, face, item)`. `face` is the
/// direction the items travel in (Facing: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east), as
/// vanilla hoppers pass it. Returns how many items went in (or would, with `simulate`).
int addToContainer(Container& c, ItemStack const& item, int count, int face, bool simulate = false);

/// Moves up to `max` items from the vanilla container `from` into `inv` (one source slot after
/// the other, first slot first), honouring `canPullOutItem(slot, face, item)`, `accept` and the
/// inventory's insert filters for `ctx`. Returns how many items moved.
int pullFromContainer(
    Container&                 from,
    core::BackpackInventory&   inv,
    int                        max,
    int                        face,
    core::AccessContext const& ctx,
    ItemPredicate const&       accept = {}
);

/// Moves up to `max` items from `inv` (first slot first) into the vanilla container `to`,
/// honouring the inventory's extract filters for `ctx` and `accept`. Returns how many items moved.
int pushToContainer(
    core::BackpackInventory&   inv,
    Container&                 to,
    int                        max,
    int                        face,
    core::AccessContext const& ctx,
    ItemPredicate const&       accept = {}
);

/// Moves up to `max` items from one backpack inventory to another (first slot first).
int moveBetween(
    core::BackpackInventory&   from,
    core::BackpackInventory&   to,
    int                        max,
    core::AccessContext const& fromCtx,
    core::AccessContext const& toCtx,
    ItemPredicate const&       accept = {}
);

/// The vanilla container block (chest, furnace, hopper, ...) at `pos`, or nullptr.
[[nodiscard]] Container* containerAt(BlockSource& region, BlockPos const& pos);

/// The block next to `pos` in direction `face` (`pos` itself for an invalid face).
[[nodiscard]] BlockPos neighbor(BlockPos const& pos, int face);

/// Facing index (0 down, 1 up, 2 north, 3 south, 4 west, 5 east) of the opposite direction.
[[nodiscard]] constexpr int oppositeFace(int face) { return face ^ 1; }

} // namespace abp::placed
