#pragma once

#include <vector>

namespace abp::core::columns {

/// Where the items of a backpack go when upgrade inventory parts (Tank, Battery) take storage
/// columns and the inventory shrinks to `targetSlots` (Sophisticated Core's
/// `InventoryLayoutFitter` for one-slot parts):
/// 1. items keep their slot when it is below `targetSlots`; items above move to the free slots
///    after the last item that stayed;
/// 2. if that does not fit, everything is compacted to the front in slot order, except `fixed`
///    slots (memory / no-sort slots holding an item), which never move.
struct FitResult {
    bool             fits = false;
    std::vector<int> target;     ///< per source slot: its new slot (-1 = empty source slot)
    std::vector<int> errorSlots; ///< source slots that could not be placed (when !fits)
};

/// `occupied[i]`: slot i holds an item; `fixed[i]` (may be shorter): it must not move;
/// `blocked[i]` (may be shorter): an empty slot that must not receive another slot's item (an
/// empty memory slot remembering a different item).
[[nodiscard]] FitResult
fit(std::vector<bool> const& occupied, std::vector<bool> const& fixed, int targetSlots, std::vector<bool> const& blocked = {});

} // namespace abp::core::columns
