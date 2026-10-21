#include "core/Capacity.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace abp::core::capacity {

namespace {
/// `base * multiplier` truncated, saturating at INT_MAX (SB: `MAX / mult < base ? MAX : (int)(base * mult)`).
int scaled(long long base, double multiplier) {
    if (base <= 0 || !(multiplier > 0)) return 0;
    double v = static_cast<double>(base) * multiplier;
    if (v >= static_cast<double>(INT_MAX)) return INT_MAX;
    return static_cast<int>(v);
}
} // namespace

int slotRows(int storageSlots) {
    if (storageSlots <= 0) return 0;
    int per = storageSlots <= 81 ? 9 : 12;
    return (storageSlots + per - 1) / per;
}

double adjustedMultiplier(double stackMultiplier, double ratio) {
    if (!std::isfinite(stackMultiplier) || !std::isfinite(ratio)) return 1.0;
    return 1.0 + ratio * (stackMultiplier - 1.0);
}

int batteryCapacity(int rows, double stackMultiplier, int energyPerSlotRow, double ratio) {
    return scaled(static_cast<long long>(energyPerSlotRow) * std::max(0, rows), adjustedMultiplier(stackMultiplier, ratio));
}

int batteryMaxInOut(int rows, double stackMultiplier, int maxInputOutput, double ratio) {
    return scaled(static_cast<long long>(maxInputOutput) * std::max(0, rows), adjustedMultiplier(stackMultiplier, ratio));
}

int tankCapacity(int rows, double stackMultiplier, int capacityPerSlotRow, double ratio) {
    return scaled(static_cast<long long>(capacityPerSlotRow) * std::max(0, rows), adjustedMultiplier(stackMultiplier, ratio));
}

int tankMaxInOut(int rows, double stackMultiplier, int maxInputOutput, double ratio) {
    return std::max(1000, scaled(static_cast<long long>(maxInputOutput) * std::max(0, rows), adjustedMultiplier(stackMultiplier, ratio)));
}

} // namespace abp::core::capacity
