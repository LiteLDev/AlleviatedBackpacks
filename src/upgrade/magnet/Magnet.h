#pragma once

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::magnet {

/// Magnet upgrade (basic / advanced): pulls item entities and XP orbs around the carrier.
///
/// - Items: every cooldown, item entities within `range` blocks of the carrier that pass the
///   filter, have no pickup delay, were not just thrown by the carrier and fit into the backpack
///   are inserted directly into the backpack (`Source::Pickup`). The magnet also acts as a Pickup
///   upgrade for items the carrier walks over (filtered the same way).
/// - XP: XP orbs in range are offered to the registered XP consumers (e.g. an XP Pump / tank
///   upgrade of the same backpack); whatever nobody takes goes to the carrier the vanilla way
///   (mending applies).
///
/// Settings JSON (`ctx.settings()`): {"filter": core::Filter, "pickupItems": bool, "pickupXp": bool}.

inline constexpr std::string_view kBasicId    = "magnet";
inline constexpr std::string_view kAdvancedId = "advanced_magnet";
inline constexpr int              kBasicSlots    = 9;
inline constexpr int              kAdvancedSlots = 16;

/// `config/magnet.json`.
struct MagnetConfig {
    int version = 1;
    /// Pull radius in blocks around the carrier's bounding box.
    int basicRange    = 3;
    int advancedRange = 5;
    /// Ticks between pulls, and after a pull where an item did not fit into the backpack.
    int cooldownTicks     = 10;
    int fullCooldownTicks = 40;
    /// Items the carrier threw themselves are left alone for this many ticks (so you can drop
    /// things without the magnet sucking them straight back in).
    int ownDropDelayTicks = 60;
};

/// Decoded per-upgrade settings.
struct Settings {
    bool         pickupItems = true;
    bool         pickupXp    = true;
    core::Filter filter;

    [[nodiscard]] static Settings read(nlohmann::json const& settings, int filterSlots);
};

/// Something that can take XP collected by a magnet (XP Pump, experience tanks, ...). Gets the
/// magnet's context (its backpack), the carrier (may be null) and the orb's XP; returns how much
/// XP it took (0..amount).
using XpConsumer = std::function<int(UpgradeContext const& magnet, Player* carrier, int amount)>;

/// Registers an XP consumer under `owner` (lower `priority` asks first).
void addXpConsumer(std::string owner, int priority, XpConsumer fn);
/// Removes every consumer registered under `owner`.
void removeXpConsumers(std::string const& owner);
/// Offers `amount` XP to the consumers; returns what is left.
int offerXp(UpgradeContext const& magnet, Player* carrier, int amount);

} // namespace abp::upgrade::magnet
