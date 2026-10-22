#pragma once

// Per-game-tick driver for the fluid / energy upgrades. Sophisticated Backpacks ticks upgrades
// every game tick and gives each its own cooldown (pump hand interaction: 3 ticks, XP pump: 5,
// battery: every tick), finer than `GeneralConfig::upgradeTickInterval`. An upgrade's `onTick`
// calls `keepAlive`; the driver then runs `fn` on every server tick while `onTick` keeps
// refreshing it (the backpack is still carried, the upgrade still installed and enabled).

#include <cstdint>
#include <functional>
#include <string>

#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::fasttick {

/// Server ticks counted by the driver (monotonic while the mod is enabled). Use it for SB-style
/// "cooldown until game time" state.
[[nodiscard]] std::int64_t now();

/// `ctx` has the carrier as player; `where` / `depth` as in `TickContext`.
using TickFn = std::function<void(Player& carrier, UpgradeContext const& ctx, game::ItemLocation where, int depth)>;

/// Runs `fn` every server tick for the upgrade of `tick` until `onTick` stops refreshing it.
/// `kind` separates several drivers of one upgrade.
void keepAlive(TickContext const& tick, std::string_view kind, TickFn fn);

} // namespace abp::upgrade::fasttick
