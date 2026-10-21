#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "upgrade/IUpgrade.h"
#include "upgrade/anvil/StationMenu.h" // shared station framework (same feature unit, U18)
#include "upgrade/smithing/SmithingLogic.h"

namespace abp::upgrade::smithing {

/// The smithing tab: template (slot 2), base (3), addition (4), result (6), plus the backpack
/// storage below. Settings keys: "template", "base", "addition".
class SmithingMenu : public anvil::StationMenu {
public:
    SmithingMenu(std::string title, anvil::StationRef ref);

    static constexpr int kTemplateSlot = 2;
    static constexpr int kBaseSlot     = 3;
    static constexpr int kAdditionSlot = 4;
    static constexpr int kResultSlot   = 6;

    static bool open(Player& player, UpgradeContext const& ctx);

    /// The current result (cached until the inputs change).
    [[nodiscard]] SmithingOutcome const& outcome();

protected:
    void buildStation(Player& player) override;
    int  quickMoveStation(Player& player, ItemStack const& stack) override;

private:
    std::optional<anvil::OutputSlot::Crafted> craft(Player& player);

    std::optional<SmithingOutcome> mCache;
    std::uint64_t                  mCacheRevision = 0;
};

class SmithingUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const&        info() const override;
    void                                    openSettings(Player& player, UpgradeContext const& ctx) override;
    [[nodiscard]] std::vector<std::string>  describe(Player& player, UpgradeContext const& ctx) const override;
};

} // namespace abp::upgrade::smithing
