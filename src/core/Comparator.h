#pragma once

#include "core/BackpackInventory.h"

namespace abp::core {

/// Comparator output of a backpack inventory (SC InventoryHelper.getAnalogOutputSignal): the
/// average slot fill (count / effective slot limit) times 14, plus 1 when anything is stored.
[[nodiscard]] int comparatorSignal(BackpackInventory const& inventory);

} // namespace abp::core