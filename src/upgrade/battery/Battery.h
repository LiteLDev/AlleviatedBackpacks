#pragma once

#include <string_view>

#include "upgrade/IUpgrade.h"

namespace abp::upgrade::battery {

/// Battery upgrade (SB "Battery Upgrade", Sophisticated Core `BatteryUpgradeWrapper`): an energy
/// (FE) buffer replacing two storage columns. Items with an energy capability (see
/// `abp::api::CapabilityService::registerItemEnergy`) in its Input slot are discharged into it and
/// items in its Output slot are charged from it, every tick.
///
/// - Capacity: `energyPerSlotRow` (10000 FE) x slot rows of the base layout x adjusted multiplier.
/// - Per operation: `maxInputOutput` (20) x rows x adjusted multiplier FE.
/// - One per backpack, can't be switched off. Other mods see it as the backpack's energy storage.
///
/// Settings JSON (travels with the upgrade item): "energy": <FE>, "in" / "out": ItemStackData.
inline constexpr std::string_view kId     = "battery";
inline constexpr std::string_view kItemId = "alleviatedbackpacks:battery_upgrade";

[[nodiscard]] int capacity(game::Backpack& backpack);
[[nodiscard]] int baseCapacity(game::Backpack& backpack);
[[nodiscard]] int maxInOut(game::Backpack& backpack);
[[nodiscard]] int stored(UpgradeContext const& ctx);
void              setStored(UpgradeContext const& ctx, int energy);
/// SB `receiveEnergy` / `extractEnergy` (limited per operation).
int receive(UpgradeContext const& ctx, int maxReceive, bool simulate);
int extract(UpgradeContext const& ctx, int maxExtract, bool simulate);
/// One SB `tick`: charge from the input item, discharge into the output item.
void tick(UpgradeContext const& ctx);

} // namespace abp::upgrade::battery
