#pragma once

#include <string>

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
/// Pump mB moved per operation with tanks / pipes: max(1000, maxInputOutput x rows x
/// (1 + (int)(ratio x (stackMultiplier - 1)))) (SC `PumpUpgradeWrapper.getMaxInOut`: the pump's
/// adjusted multiplier is truncated to an int).
[[nodiscard]] int pumpMaxInOut(int rows, double stackMultiplier, int maxInputOutput = 20, double ratio = kStackMultiplierRatio);

/// SB's "... to get at least %sx higher capacity" value: `required` (contents / capacity after
/// the change) rounded up to one decimal, printed like Java's `DecimalFormat("0.#")` ("1.5", "2").
[[nodiscard]] std::string formatRequiredMultiplier(double required);

// ---- inventory columns taken by upgrade inventory parts (Tank, Battery) ---------------------------

/// Storage slots left when `columns` columns of a `baseSlots` layout are taken by upgrade
/// inventory parts: `baseSlots - slotRows(baseSlots) * columns` (never negative).
[[nodiscard]] int slotsAfterColumns(int baseSlots, int columns);
/// The inverse of `slotsAfterColumns`: the layout's base slot count for `slots` remaining storage
/// slots with `columns` taken (`slots` itself when no column is taken).
[[nodiscard]] int baseSlotsFor(int slots, int columns);

} // namespace abp::core::capacity
