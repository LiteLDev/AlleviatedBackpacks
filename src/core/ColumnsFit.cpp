#include "core/ColumnsFit.h"

#include <algorithm>

namespace abp::core::columns {

namespace {
bool isFixed(std::vector<bool> const& fixed, std::size_t i) { return i < fixed.size() && fixed[i]; }

/// One pass of SC's fitter. `compact`: every movable item goes to the next free slot.
FitResult pass(std::vector<bool> const& occupied, std::vector<bool> const& fixed, std::vector<bool> const& blocked, int targetSlots, bool compact) {
    FitResult         r;
    std::size_t const n = occupied.size();
    int const         limit = std::max(0, targetSlots);
    r.target.assign(n, -1);
    std::vector<bool> used(static_cast<std::size_t>(limit), false);
    for (std::size_t i = 0; i < blocked.size() && i < used.size(); ++i) {
        if (blocked[i] && (i >= n || !occupied[i])) used[i] = true;
    }
    // Fixed items claim their slots first (they never move).
    for (std::size_t i = 0; i < n; ++i) {
        if (!occupied[i] || !isFixed(fixed, i)) continue;
        if (static_cast<int>(i) >= limit) {
            r.errorSlots.push_back(static_cast<int>(i));
            continue;
        }
        used[i]     = true;
        r.target[i] = static_cast<int>(i);
    }
    int next = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!occupied[i] || isFixed(fixed, i)) continue;
        int slot = -1;
        if (!compact && static_cast<int>(i) < limit && !used[i]) {
            slot = static_cast<int>(i);
        } else {
            int from = compact || static_cast<int>(i) >= limit ? next : std::max(next, static_cast<int>(i));
            for (int s = from; s < limit; ++s) {
                if (!used[static_cast<std::size_t>(s)]) {
                    slot = s;
                    break;
                }
            }
        }
        if (slot < 0) {
            r.errorSlots.push_back(static_cast<int>(i));
            continue;
        }
        used[static_cast<std::size_t>(slot)] = true;
        r.target[i]                          = slot;
        next                                 = std::max(next, slot + 1);
    }
    std::sort(r.errorSlots.begin(), r.errorSlots.end());
    r.fits = r.errorSlots.empty();
    return r;
}
} // namespace

FitResult fit(std::vector<bool> const& occupied, std::vector<bool> const& fixed, int targetSlots, std::vector<bool> const& blocked) {
    auto keep = pass(occupied, fixed, blocked, targetSlots, false);
    if (keep.fits) return keep;
    return pass(occupied, fixed, blocked, targetSlots, true);
}

} // namespace abp::core::columns
