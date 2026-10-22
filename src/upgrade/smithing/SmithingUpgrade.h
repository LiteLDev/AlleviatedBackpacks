#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "upgrade/IUpgrade.h"
#include "upgrade/anvil/StationMenu.h" // shared station framework (same feature unit, U18)
#include "upgrade/smithing/SmithingLogic.h"

namespace abp::upgrade::smithing {

// ---- the smithing station (shared by the native tab and the chest menu) ---------------------------
// Settings keys: "template", "base", "addition".

[[nodiscard]] SmithingOutcome stationOutcome(anvil::StationRef const& ref);
/// Takes the result: consumes one of each input. nullopt when there is no result.
[[nodiscard]] std::optional<anvil::OutputSlot::Crafted> craft(Player& player, anvil::StationRef const& ref);

[[nodiscard]] std::shared_ptr<anvil::InputSlot>  templateSlot(anvil::StationRef const& ref);
[[nodiscard]] std::shared_ptr<anvil::InputSlot>  baseSlot(anvil::StationRef const& ref);
[[nodiscard]] std::shared_ptr<anvil::InputSlot>  additionSlot(anvil::StationRef const& ref);
[[nodiscard]] std::shared_ptr<anvil::OutputSlot> resultSlot(anvil::StationRef const& ref, std::optional<anvil::PendingUndo>& pending);

/// The smithing tab as a chest menu (simulated players / selftests; companions get the native
/// tab): template (slot 2), base (3), addition (4), result (6), plus the backpack storage below.
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
