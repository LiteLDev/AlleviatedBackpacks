#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "api/abp/Fluid.h"
#include "core/Fluid.h"
#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::tank {

/// Tank upgrade (SB "Tank Upgrade", Sophisticated Core `TankUpgradeWrapper`): one fluid tank per
/// installed upgrade (at most two per backpack). It replaces two storage columns (see
/// `upgrade/tank/Columns.h`) with a fluid bar on the backpack screen.
///
/// - Capacity: `capacityPerSlotRow` (4000 mB) x slot rows of the base layout x
///   (1 + `stackMultiplierRatio` x (stack multiplier - 1)).
/// - Per operation (pipes, pumps, containers) at most max(1000, `maxInputOutput` (20) x rows x
///   adjusted multiplier) mB move.
/// - Four item slots: Input (filled containers are drained into the tank), Output (empty
///   containers are filled), and their result slots; one bucket per `autoFillDrainContainerCooldown`
///   (20) ticks.
///
/// Settings JSON (travels with the upgrade item): "tank": {"fluid": <phase-1 fluid id>, "amount"},
/// "in" / "out" / "inResult" / "outResult": ItemStackData.
inline constexpr std::string_view kId     = "tank";
inline constexpr std::string_view kItemId = "alleviatedbackpacks:tank_upgrade";

using core::fluid::FluidStack;

/// Capacity of one tank in `backpack` (mB).
[[nodiscard]] int capacity(game::Backpack& backpack);
/// The base capacity (without the stack multiplier), SB `getBaseCapacity`.
[[nodiscard]] int baseCapacity(game::Backpack& backpack);
/// mB one tank moves per operation (SB `getMaxInOut`).
[[nodiscard]] int maxInOut(game::Backpack& backpack);

/// Contexts of the installed tank upgrades (in slot order).
[[nodiscard]] std::vector<UpgradeContext> tanks(std::shared_ptr<game::Backpack> const& backpack);
/// True if `ctx` still points to an installed tank upgrade.
[[nodiscard]] bool isTank(UpgradeContext const& ctx);

/// Reads / writes the tank of one tank upgrade (`write` marks the backpack dirty).
[[nodiscard]] core::fluid::Tank read(UpgradeContext const& ctx);
void                            write(UpgradeContext const& ctx, core::fluid::Tank const& tank);
/// The tank's contents as an API stack ("minecraft:water", ...).
[[nodiscard]] api::FluidStack contents(UpgradeContext const& ctx);

/// SB `TankUpgradeWrapper.fill` / `drain` of one tank: `ignoreLimit` = no per-operation limit.
int             fillTank(UpgradeContext const& ctx, api::FluidStack const& resource, bool simulate, bool ignoreLimit = false);
api::FluidStack drainTank(UpgradeContext const& ctx, int maxDrain, bool simulate, bool ignoreLimit = false);

/// The backpack's tanks as one handler (SB `BackpackFluidHandler` without the void upgrades):
/// fill pours into every tank in slot order, drain collects. `ignoreLimit` lifts the per-operation
/// limit (manual XP pump actions). nullptr without tanks.
[[nodiscard]] std::shared_ptr<api::IFluidHandler> storageHandler(std::shared_ptr<game::Backpack> const& backpack, bool ignoreLimit);

// ---- backpack-wide fluid access by phase-1 fluid id, without per-operation limits ----------------
/// Fills tanks already holding `stack.id` first, then empty ones. `exact`: all or nothing.
int fill(std::shared_ptr<game::Backpack> const& backpack, FluidStack const& stack, bool simulate = false, bool exact = false);
/// Drains up to `max` mB of `fluid` (empty = the fluid of the first non-empty tank).
FluidStack drain(
    std::shared_ptr<game::Backpack> const& backpack,
    std::string_view                       fluid,
    int                                    max,
    bool                                   simulate = false,
    bool                                   exact    = false
);
[[nodiscard]] long long                amountOf(std::shared_ptr<game::Backpack> const& backpack, std::string_view fluid);
[[nodiscard]] std::vector<std::string> fluids(std::shared_ptr<game::Backpack> const& backpack);

// ---- swap containers by item data (capability API registry) --------------------------------------
using FillFn  = std::function<int(FluidStack const&, bool simulate)>;
using DrainFn = std::function<FluidStack(std::string_view fluid, int amount, bool simulate)>;
/// Empties one filled container into `fill`. Returns the empty container (count 1).
std::optional<core::ItemStackData> emptyContainer(core::ItemStackData const& filled, FillFn const& fill, bool simulate = false);
/// Fills one empty container with `fluid` taken from `drain`. Returns the filled item (count 1).
std::optional<core::ItemStackData>
fillContainer(core::ItemStackData const& empty, std::string_view fluid, DrainFn const& drain, bool simulate = false);
[[nodiscard]] FillFn  fillerOf(UpgradeContext const& ctx);
[[nodiscard]] DrainFn drainerOf(UpgradeContext const& ctx);

// ---- SB container logic (TankUpgradeWrapper) ------------------------------------------------------
/// One Input / Output slot step (SB `tick`): drains the container in the input slot into the tank
/// and fills the container in the output slot from it; full / empty containers go to the result
/// slots. Returns true if something moved (the caller starts the 20 tick cooldown).
bool processSlots(UpgradeContext const& ctx);
/// SB `interactWithCursorStack` (clicking the tank bar): fills or drains the item on the cursor.
/// Returns the new cursor item, or nullopt if nothing happened.
[[nodiscard]] std::optional<ItemStack> interactWithCursor(UpgradeContext const& ctx, ItemStack const& cursor);

// ---- display ----------------------------------------------------------------------------------
/// Translated fluid name ("Water", "Potion #7", ...).
[[nodiscard]] std::string fluidName(Player const* player, std::string_view fluid);
/// "Water: 3,000 / 12,000 mB" style line (or "Empty (12,000 mB)").
[[nodiscard]] std::string describe(Player const* player, core::fluid::Tank const& tank);

} // namespace abp::upgrade::tank
