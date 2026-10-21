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

} // namespace abp::core::stack
