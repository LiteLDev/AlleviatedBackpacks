#pragma once

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::magnet {

/// Magnet upgrade (basic / advanced), as Sophisticated Core's `MagnetUpgradeWrapper`:
///
/// - Items: item entities inside the cube `AABB(pos).inflate(magnetRange)` around the carrier's
///   (or the placed backpack's) block position that are alive, not on infinite pickup delay, pass
///   the contents filter and carry neither the `PreventRemoteMovement` tag (nor, for a placed
///   backpack, `PreventRemoteMovement` without `AllowMachineRemoteMovement`) are inserted into the
///   backpack. Items the carrier just threw are pulled as well. Walking over items works like a
///   Pickup upgrade with the same filter.
/// - XP: only when the backpack's fluid handler (tank upgrades, via the capability API) accepts
///   liquid experience: every orb in range is turned into 20 mB per point; whatever does not fit
///   is spawned again as an orb.
/// - Timing: a carried magnet runs on SB's shared clock (once every 11 ticks for every carried
///   magnet); a placed one uses its own cooldown (10 ticks, 40 when nothing could be taken).
///
/// Settings JSON (`ctx.settings()`): {"filter": core::Filter, "pickupItems": bool, "pickupXp": bool}.

inline constexpr std::string_view kBasicId    = "magnet";
inline constexpr std::string_view kAdvancedId = "advanced_magnet";

inline constexpr int kCooldownTicks     = 10; ///< SC COOLDOWN_TICKS
inline constexpr int kFullCooldownTicks = 40; ///< SC FULL_COOLDOWN_TICKS

/// Entity tags that keep a magnet away from an item entity / orb (SC `PreventRemoteMovement`,
/// `AllowMachineRemoteMovement`; Bedrock entity tags, e.g. `/tag @e add PreventRemoteMovement`).
inline constexpr char const* kPreventRemoteMovement = "PreventRemoteMovement";
inline constexpr char const* kAllowMachineMovement  = "AllowMachineRemoteMovement";

/// SB `MagnetUpgradeConfig` of one tier.
struct MagnetTierConfig {
    int filterSlots = 9; ///< Number of filter slots (1..20)
    int slotsInRow  = 3; ///< Number of filter slots displayed in a row (1..6)
    int magnetRange = 3; ///< Range around storage in blocks at which magnet will pickup items (1..20)
};

/// `config/magnet.json` (SB server config sections `magnetUpgrade` / `advancedMagnetUpgrade`).
struct MagnetConfig {
    int              version               = 2;
    MagnetTierConfig magnetUpgrade         = {9, 3, 3};
    MagnetTierConfig advancedMagnetUpgrade = {16, 4, 5};
};

/// The (clamped) config of a tier.
[[nodiscard]] MagnetTierConfig tierConfig(bool advanced);

/// Decoded per-upgrade settings.
struct Settings {
    bool         pickupItems = true;
    bool         pickupXp    = true;
    bool         byStorage   = false; ///< SC ContentsFilterType.STORAGE ("Match Backpack Contents")
    core::Filter filter;

    [[nodiscard]] static Settings read(nlohmann::json const& settings, int filterSlots, bool advanced);
};

/// SC `ContentsFilterLogic.matchesFilter`: the filter, or with "match backpack contents" any
/// stored stack / memorized slot of the same item.
[[nodiscard]] bool matchesFilter(Settings const& s, game::Backpack& backpack, core::ItemStackData const& item);

/// Something that can take XP collected by a magnet. Kept for other units (XP Pump); the magnet
/// itself now fills the backpack's tanks like SB.
using XpConsumer = std::function<int(UpgradeContext const& magnet, Player* carrier, int amount)>;

/// Registers an XP consumer under `owner` (lower `priority` asks first).
void addXpConsumer(std::string owner, int priority, XpConsumer fn);
/// Removes every consumer registered under `owner`.
void removeXpConsumers(std::string const& owner);
/// Offers `amount` XP to the consumers; returns what is left.
int offerXp(UpgradeContext const& magnet, Player* carrier, int amount);

} // namespace abp::upgrade::magnet
