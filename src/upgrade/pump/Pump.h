#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::pump {

/// Pump upgrades (SB "Pump Upgrade" / "Advanced Pump Upgrade"). They move fluids between the
/// backpack's tanks (Tank upgrade) and:
/// - the bucket in the carrier's main hand or offhand ("hand", both tiers);
/// - the world ("world", advanced): fluid source blocks the carrier stands in are pumped up
///   (input mode); sneak + use with an empty hand on a block pumps the fluid in front of the face
///   / places fluid there (output mode), and drains / fills cauldrons.
/// The advanced pump has a 4-slot fluid filter (allow list; empty = everything).
inline constexpr std::string_view kBasicId    = "pump";
inline constexpr std::string_view kAdvancedId = "advanced_pump";
inline constexpr int              kFilterSlots = 4;

struct Settings {
    bool                     output = false; ///< false: fill the tanks (input); true: empty them (output)
    bool                     hand   = true;
    bool                     world  = true;  ///< advanced only
    std::vector<std::string> filter;         ///< advanced only: allowed fluid ids ("" = free slot)
    bool                     advanced = false;

    [[nodiscard]] static Settings of(UpgradeContext const& ctx);
    void                          save(UpgradeContext const& ctx) const;
    /// Filter check (basic pumps and empty filters allow everything).
    [[nodiscard]] bool allows(std::string_view fluid) const;
};

/// The first enabled pump upgrade in `backpack`.
[[nodiscard]] std::optional<UpgradeContext> activePump(std::shared_ptr<game::Backpack> const& backpack);

/// One pumping cycle for a carried backpack (hand, then world). True if something moved.
bool pumpCycle(Player& player, UpgradeContext const& pump);
/// Fills / empties the bucket in the main hand (then the offhand). True if something moved.
bool interactHand(Player& player, UpgradeContext const& pump, Settings const& settings);
/// Input: pumps the fluid source block at `pos` into the tanks. True if pumped.
bool pumpFromWorld(Player& player, BlockPos const& pos, UpgradeContext const& pump, Settings const& settings);
/// Sneak + use with an empty hand on block `pos`, face `face` (FacingID value). True if handled.
bool useOnBlock(Player& player, BlockPos const& pos, int face, UpgradeContext const& pump, Settings const& settings);

} // namespace abp::upgrade::pump
