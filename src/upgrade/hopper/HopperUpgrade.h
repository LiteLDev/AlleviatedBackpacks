#pragma once

#include <array>

#include "nlohmann/json.hpp"

#include "upgrade/IUpgrade.h"

namespace abp::upgrade::hopper {

/// `config/hopper.json`.
struct HopperConfig {
    int version = 1;
    int basicFilterSlots       = 2; ///< ghost slots of each filter (input / output)
    int basicTransferTicks     = 8; ///< ticks between transfers
    int basicTransferAmount    = 1; ///< items per transfer and direction
    int advancedFilterSlots    = 8;
    int advancedTransferTicks  = 2;
    int advancedTransferAmount = 4;
};

/// Facing order used for directions: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
using Directions = std::array<bool, 6>;

/// Hopper / Advanced Hopper upgrade. Works while the backpack is placed as a block (like the
/// Java mod): pulls items from the container above into the backpack and pushes items from the
/// backpack into the container below, through an input and an output filter. The advanced
/// version transfers faster, has bigger filters and configurable pull / push directions.
/// Placed backpacks next to each other count as containers too.
class HopperUpgradeBase : public IUpgrade {
public:
    explicit HopperUpgradeBase(bool advanced);

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    [[nodiscard]] bool               advanced() const { return mAdvanced; }

    void onPlacedTick(PlacedTickContext const& tick) override;
    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override;
    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override;

    [[nodiscard]] int filterSlots() const;
    /// Pull / push directions of an installed upgrade (basic: up / down).
    [[nodiscard]] Directions pullDirections(UpgradeContext const& ctx) const;
    [[nodiscard]] Directions pushDirections(UpgradeContext const& ctx) const;
    /// Advanced only: changes one direction flag ("pull" / "push").
    void setDirection(UpgradeContext const& ctx, std::string const& key, int face, bool on) const;

private:
    UpgradeInfo mInfo;
    bool        mAdvanced;
};

class HopperUpgrade final : public HopperUpgradeBase {
public:
    HopperUpgrade() : HopperUpgradeBase(false) {}
};

class AdvancedHopperUpgrade final : public HopperUpgradeBase {
public:
    AdvancedHopperUpgrade() : HopperUpgradeBase(true) {}
};

} // namespace abp::upgrade::hopper
