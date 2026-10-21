#pragma once

namespace abp::core::capacity {

/// Sophisticated Backpacks' sizing rules for the Battery and Tank upgrades (defaults are SB's
/// config defaults). `rows` is `slotRows(storage slots)`; `stackMultiplier` the backpack's stack
/// multiplier (Stack upgrades, 1 without); `ratio` the config's `stackMultiplierRatio`.

inline constexpr int    kEnergyPerSlotRow     = 10000; ///< FE per slot row (battery)
inline constexpr int    kBatteryMaxInputOutput = 20;   ///< FE per slot row per operation (battery)
inline constexpr int    kTankPerSlotRow       = 4000;  ///< mB per slot row (tank)
inline constexpr int    kTankMaxInputOutput   = 20;    ///< mB per slot row per operation (tank, at least a bucket)
inline constexpr double kStackMultiplierRatio = 1.0;

/// SB's "number of slot rows": ceil(slots / 9) up to 81 slots, ceil(slots / 12) above.
[[nodiscard]] int slotRows(int storageSlots);

/// `1 + ratio * (stackMultiplier - 1)`.
[[nodiscard]] double adjustedMultiplier(double stackMultiplier, double ratio = kStackMultiplierRatio);

/// Battery capacity: energyPerSlotRow x rows x adjusted multiplier (saturates at INT_MAX).
[[nodiscard]] int batteryCapacity(int rows, double stackMultiplier, int energyPerSlotRow = kEnergyPerSlotRow, double ratio = kStackMultiplierRatio);
/// Battery FE moved per operation: maxInputOutput x rows x adjusted multiplier.
[[nodiscard]] int batteryMaxInOut(int rows, double stackMultiplier, int maxInputOutput = kBatteryMaxInputOutput, double ratio = kStackMultiplierRatio);
/// Tank capacity: capacityPerSlotRow x rows x adjusted multiplier.
[[nodiscard]] int tankCapacity(int rows, double stackMultiplier, int capacityPerSlotRow = kTankPerSlotRow, double ratio = kStackMultiplierRatio);
/// Tank mB moved per operation: max(1000, maxInputOutput x rows x adjusted multiplier).
[[nodiscard]] int tankMaxInOut(int rows, double stackMultiplier, int maxInputOutput = kTankMaxInputOutput, double ratio = kStackMultiplierRatio);

} // namespace abp::core::capacity
