#pragma once

#include <climits>
#include <functional>
#include <span>

#include "core/BackpackInventory.h"

namespace abp::core::stack {

/// Largest slot limit a stack multiplier can produce (counts are `int`).
inline constexpr int kMaxSlotLimit = INT_MAX;

/// Product of stack-size multipliers (Stack upgrades multiply each other), clamped once at the
/// end so that `64 * product` never exceeds `kMaxSlotLimit` (order-independent). An empty list
/// yields 1. Non-positive, infinite or NaN entries are ignored.
[[nodiscard]] double combine(std::span<double const> multipliers);

/// `base` (an item's max stack size, or a limit computed so far) scaled by `multiplier`, rounded
/// down, clamped to [1, kMaxSlotLimit].
[[nodiscard]] int scaledLimit(int base, double multiplier);

/// The slot limit for a 64-stack item, `64 * multiplier` (shown to players, e.g. "128").
[[nodiscard]] int baseSlotLimit(double multiplier);

/// Whether `inventory` still fits when the stack multiplier becomes `multiplier`: returns the
/// first slot whose count exceeds `scaledLimit(item.maxStack, multiplier)` (or, for items where
/// `affected(item)` is false, the item's own max stack size), or -1 when everything fits.
[[nodiscard]] int firstSlotOver(
    BackpackInventory const&                         inventory,
    double                                           multiplier,
    std::function<bool(ItemStackData const&)> const& affected
);

// ---- SophisticatedCore's exact rules (StackUpgradeItem / InventoryHandler) ----------------------

/// SC `getInventorySlotLimit(multiplier)`: `INT_MAX / 64 < m ? INT_MAX : max(1, (int)(m * 64))`.
[[nodiscard]] int slotLimitOf(double multiplier);

/// SC `getInventorySlotLimit(wrapper)`: the stack upgrades' multipliers multiplied in order
/// (starting at 1), short-circuiting to INT_MAX as soon as the product would exceed INT_MAX, then
/// `slotLimitOf`. Non-positive / non-finite entries are ignored.
[[nodiscard]] int slotLimitOf(std::span<double const> multipliers);

/// SC `InventoryHandler.getBaseStackLimit`: how many of an item with `maxStack` fit in a slot whose
/// limit (for 64-stack items) is `slotLimit`. Below 64: `max(1, (int)(maxStack * slotLimit / 64.0))`;
/// otherwise `maxStack * (slotLimit / 64) + (slotLimit % 64) * maxStack / 64` (integer math,
/// saturating at INT_MAX). Unstackable items (maxStack 1) are multiplied too.
[[nodiscard]] int stackLimit(int maxStack, int slotLimit);

/// SC `StackUpgradeItem.isMultiplierHighEnough` (storage part): the first slot holding more than one
/// item whose `count / maxStack` exceeds `slotLimitOf(multiplier) / 64`, or -1 when all fit.
[[nodiscard]] int firstSlotNeedingMore(BackpackInventory const& inventory, double multiplier);

} // namespace abp::core::stack
