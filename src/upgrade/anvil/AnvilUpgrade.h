#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "upgrade/IUpgrade.h"
#include "upgrade/anvil/AnvilLogic.h"
#include "upgrade/anvil/StationMenu.h"

namespace abp::upgrade::anvil {

/// The anvil tab: left input (slot 2), right input (4), cost (5), result (6), rename (8), plus
/// the backpack storage below. Settings keys: "left", "right" (items), "name" (pending rename).
class AnvilMenu : public StationMenu {
public:
    AnvilMenu(std::string title, StationRef ref);

    static constexpr int kLeftSlot   = 2;
    static constexpr int kRightSlot  = 4;
    static constexpr int kCostSlot   = 5;
    static constexpr int kResultSlot = 6;
    static constexpr int kRenameSlot = 8;

    /// Opens the anvil tab of an installed anvil upgrade.
    static bool open(Player& player, UpgradeContext const& ctx);
    /// Closes the menu, asks for a name with a form and reopens the anvil.
    static void openRenameForm(Player& player, StationRef ref);

    /// The name typed for the item in the left slot (nullopt = keep the name).
    [[nodiscard]] static std::optional<std::string> pendingName(StationRef const& ref);
    static void                                     setPendingName(StationRef const& ref, std::optional<std::string> const& name);

    /// The current anvil result for `player` (cached until the inputs change).
    [[nodiscard]] AnvilOutcome const& outcome(Player& player);

    void onTick(Player& player) override;

protected:
    void buildStation(Player& player) override;
    int  quickMoveStation(Player& player, ItemStack const& stack) override;

private:
    std::optional<OutputSlot::Crafted> craft(Player& player);

    std::optional<AnvilOutcome> mCache;
    std::uint64_t               mCacheRevision = 0;
    bool                        mCacheCreative = false;
    std::optional<std::string>  mBuiltName; ///< pending name shown on the rename button
};

class AnvilUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const&        info() const override;
    void                                    openSettings(Player& player, UpgradeContext const& ctx) override;
    [[nodiscard]] std::vector<std::string>  describe(Player& player, UpgradeContext const& ctx) const override;
};

} // namespace abp::upgrade::anvil
