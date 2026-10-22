#include "core/Comparator.h"

#include <cmath>

namespace abp::core {

int comparatorSignal(BackpackInventory const& inventory) {
    int n = inventory.size();
    if (n <= 0) return 0;
    double filled = 0;
    bool   empty  = true;
    for (int slot = 0; slot < n; ++slot) {
        auto const& stack = inventory.at(slot);
        if (stack.empty()) continue;
        empty     = false;
        int limit = inventory.slotLimit(slot, stack);
        if (limit > 0) filled += static_cast<double>(stack.count) / static_cast<double>(limit);
    }
    double percent = filled / static_cast<double>(n);
    return static_cast<int>(std::floor(percent * 14.0)) + (empty ? 0 : 1);
}

} // namespace abp::core