#include "core/Comparator.h"

#include <cmath>

namespace abp::core {

int comparatorSignal(BackpackInventory const& inventory) { return comparatorSignal(std::vector<BackpackInventory const*>{&inventory}); }

int comparatorSignal(std::vector<BackpackInventory const*> const& inventories) {
    int    n      = 0;
    double filled = 0;
    bool   empty  = true;
    for (auto const* inventory : inventories) {
        if (!inventory) continue;
        int size  = inventory->size();
        n        += size;
        for (int slot = 0; slot < size; ++slot) {
            auto const& stack = inventory->at(slot);
            if (stack.empty()) continue;
            empty     = false;
            int limit = inventory->slotLimit(slot, stack);
            if (limit > 0) filled += static_cast<double>(stack.count) / static_cast<double>(limit);
        }
    }
    if (n <= 0) return 0;
    double percent = filled / static_cast<double>(n);
    return static_cast<int>(std::floor(percent * 14.0)) + (empty ? 0 : 1);
}

} // namespace abp::core
