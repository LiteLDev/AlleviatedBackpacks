#pragma once

// Sophisticated Backpacks' nerfs (see Nerf.cpp).

class MobEffect;
class Player;

namespace abp::game {
struct CarriedBackpack;
}

namespace abp::nerf {

/// The configured `nerfEffect` (slowness if unknown).
[[nodiscard]] MobEffect const* nerfEffect();
/// Applies the too-many-backpacks effect to `player` now (if on and over the limit). Returns the
/// number of levels applied (0 = none).
int applySlowness(Player& player);
/// `onlyWornBackpackTriggersUpgrades`: whether a carried backpack's upgrades run.
[[nodiscard]] bool upgradesActive(game::CarriedBackpack const& carried);

} // namespace abp::nerf
