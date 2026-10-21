#include "core/StackMultiplier.h"

#include <algorithm>
#include <cmath>

namespace abp::core::stack {

namespace {
constexpr double kMaxMultiplier = static_cast<double>(kMaxSlotLimit) / 64.0;
}

double combine(std::span<double const> multipliers) {
    // Multiply in full and clamp once, so the result doesn't depend on the order (a downgrade
    // after an Omega upgrade gives the same as before it). Doubles can't overflow here.
    double product = 1.0;
    for (double m : multipliers) {
        if (!(m > 0.0) || !std::isfinite(m)) continue; // also skips NaN
        product *= m;
    }
    return std::min(product, kMaxMultiplier);
}

int scaledLimit(int base, double multiplier) {
    if (!(multiplier > 0.0)) multiplier = 1.0;
    double v = std::floor(static_cast<double>(std::max(base, 1)) * multiplier);
    if (v >= static_cast<double>(kMaxSlotLimit)) return kMaxSlotLimit;
    return std::max(1, static_cast<int>(v));
}

int baseSlotLimit(double multiplier) { return scaledLimit(64, multiplier); }

int firstSlotOver(
    BackpackInventory const&                         inventory,
    double                                           multiplier,
    std::function<bool(ItemStackData const&)> const& affected
) {
    for (int i = 0; i < inventory.size(); ++i) {
        auto const& s = inventory.at(i);
        if (s.empty() || s.count <= 1) continue;
        int limit = (!affected || affected(s)) ? scaledLimit(s.maxStack, multiplier) : std::max(s.maxStack, 1);
        if (s.count > limit) return i;
    }
    return -1;
}

} // namespace abp::core::stack
