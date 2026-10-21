#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "core/Fluid.h"
#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::tank {

/// Tank upgrade (SB "Tank Upgrade"): one fluid tank per installed upgrade (max 2 per backpack).
/// Its contents live in the upgrade's settings (`settings()["tank"]`) and so travel with the
/// upgrade item. Capacity = 4000 mB per 9 storage slots x the stack multiplier of the backpack
/// (see `capacity()`).
///
/// This header is the fluid API of a backpack, used by the Pump and XP Pump upgrades.
inline constexpr std::string_view kId     = "tank";
inline constexpr std::string_view kItemId = "alleviatedbackpacks:tank_upgrade";

using core::fluid::FluidStack;

/// The backpack's stack multiplier (from stack upgrades' limit modifiers; 1 without).
[[nodiscard]] int stackMultiplier(game::Backpack& backpack);
/// Capacity of one tank in `backpack` (mB).
[[nodiscard]] int capacity(game::Backpack& backpack);

/// Contexts of the installed tank upgrades (in slot order).
[[nodiscard]] std::vector<UpgradeContext> tanks(std::shared_ptr<game::Backpack> const& backpack);
/// True if `ctx` still points to an installed tank upgrade.
[[nodiscard]] bool isTank(UpgradeContext const& ctx);

/// Reads / writes the tank of one tank upgrade (`write` marks the backpack dirty).
[[nodiscard]] core::fluid::Tank read(UpgradeContext const& ctx);
void                            write(UpgradeContext const& ctx, core::fluid::Tank const& tank);

// ---- backpack-wide fluid access (all tanks) ---------------------------------------------------
/// Fills tanks already holding `stack.id` first, then empty ones. `exact`: all or nothing.
/// Returns the amount filled.
int fill(std::shared_ptr<game::Backpack> const& backpack, FluidStack const& stack, bool simulate = false, bool exact = false);
/// Drains up to `max` mB of `fluid` (empty = the fluid of the first non-empty tank). `exact`:
/// all or nothing.
FluidStack drain(
    std::shared_ptr<game::Backpack> const& backpack,
    std::string_view                       fluid,
    int                                    max,
    bool                                   simulate = false,
    bool                                   exact    = false
);
/// Total amount of `fluid` in all tanks.
[[nodiscard]] long long amountOf(std::shared_ptr<game::Backpack> const& backpack, std::string_view fluid);
/// Distinct fluids currently in the tanks (slot order).
[[nodiscard]] std::vector<std::string> fluids(std::shared_ptr<game::Backpack> const& backpack);

// ---- containers -------------------------------------------------------------------------------
/// `fill(stack, simulate)` must be all-or-nothing and return the amount accepted.
using FillFn = std::function<int(FluidStack const&, bool simulate)>;
/// `drain(fluid, amount, simulate)` must be all-or-nothing.
using DrainFn = std::function<FluidStack(std::string_view fluid, int amount, bool simulate)>;

/// Empties one filled container (`filled`, count ignored) into `fill`. Returns the empty
/// container (count 1), or nullopt if it isn't a fluid container or the fluid doesn't fit.
std::optional<core::ItemStackData> emptyContainer(core::ItemStackData const& filled, FillFn const& fill, bool simulate = false);
/// Fills one empty container with `fluid` taken from `drain`. Returns the filled item (count 1).
std::optional<core::ItemStackData>
fillContainer(core::ItemStackData const& empty, std::string_view fluid, DrainFn const& drain, bool simulate = false);

/// FillFn / DrainFn of one tank upgrade and of a whole backpack.
[[nodiscard]] FillFn  fillerOf(UpgradeContext const& ctx);
[[nodiscard]] DrainFn drainerOf(UpgradeContext const& ctx);
[[nodiscard]] FillFn  fillerOf(std::shared_ptr<game::Backpack> const& backpack);
[[nodiscard]] DrainFn drainerOf(std::shared_ptr<game::Backpack> const& backpack);

// ---- display ----------------------------------------------------------------------------------
/// Translated fluid name ("Water", "Potion #7", ...).
[[nodiscard]] std::string fluidName(Player const* player, std::string_view fluid);
/// "Water: 3,000 / 12,000 mB" style line (or "Empty (12,000 mB)").
[[nodiscard]] std::string describe(Player const* player, core::fluid::Tank const& tank);
/// An icon for a fluid (its bucket or bottle; a bottle o' enchanting for XP; a barrier for none).
[[nodiscard]] ItemStack fluidIcon(std::string_view fluid, std::string const& name, std::vector<std::string> const& lore = {});

} // namespace abp::upgrade::tank
