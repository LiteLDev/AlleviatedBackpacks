#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "api/abp/Fluid.h"
#include "upgrade/IUpgrade.h"

class Player;
class BlockSource;

namespace abp::upgrade::pump {

/// Pump upgrades (SB "Pump Upgrade" / "Advanced Pump Upgrade", Sophisticated Core
/// `PumpUpgradeWrapper`). They move fluids between the backpack's tanks and
/// - the fluid container held in the carrier's main hand, then offhand ("hand"; a placed backpack
///   serves players within 3 blocks), every 3 ticks;
/// - fluid source blocks within 4 blocks (input) or the blocks around the backpack (output, not
///   above) ("world");
/// - adjacent tanks and pipes, `maxInputOutput` x rows x multiplier (at least a bucket) per
///   operation ("handlers").
/// Defaults: basic = hand off, world off, handlers on; advanced = hand on, world off, handlers on.
/// Both pump into the backpack ("input") by default. The advanced pump has a 4 slot fluid filter.
inline constexpr std::string_view kBasicId    = "pump";
inline constexpr std::string_view kAdvancedId = "advanced_pump";

struct Settings {
    bool                     input    = true;
    bool                     hand     = false;
    bool                     world    = false;
    bool                     handlers = true;
    std::vector<std::string> filter; ///< phase-1 fluid ids ("" = empty slot), `filterSlots` long
    bool                     advanced = false;

    [[nodiscard]] static Settings of(UpgradeContext const& ctx);
    void                          save(UpgradeContext const& ctx) const;
    /// SB FluidFilterLogic.fluidMatches: an empty filter matches everything.
    [[nodiscard]] bool matches(api::FluidStack const& fluid) const;
};

/// Number of fluid filter slots (`config/pump.json` filterSlots, SB default 4).
[[nodiscard]] int filterSlots();

/// One SB pump tick for a carried backpack (`carrier` set) or a placed one (`region` + `pos`).
/// Returns the cooldown it chose (ticks).
int pumpTick(UpgradeContext const& ctx, Player* carrier, game::ItemLocation const* backpackItem, BlockSource& region, BlockPos const& pos);

/// SB FluidUtil.tryFluidTransfer: moves up to `resource.amount` of that fluid from `source` to
/// `dest`. Returns what moved.
api::FluidStack tryFluidTransfer(api::IFluidHandler& dest, api::IFluidHandler& source, api::FluidStack const& resource);

} // namespace abp::upgrade::pump
