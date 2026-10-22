#pragma once

#include <vector>

#include "core/BackpackInventory.h"

namespace abp::core {

/// Comparator output of a backpack inventory (SC InventoryHelper.getAnalogOutputSignal): the
/// average slot fill (count / effective slot limit) times 14, plus 1 when anything is stored.
[[nodiscard]] int comparatorSignal(BackpackInventory const& inventory);
/// The same over several inventories seen as one (SB: a placed backpack's IO handler with an
/// Inception upgrade combines the backpacks stored inside; every slot of every inventory counts).
[[nodiscard]] int comparatorSignal(std::vector<BackpackInventory const*> const& inventories);

} // namespace abp::core
