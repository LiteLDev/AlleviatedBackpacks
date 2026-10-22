#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "upgrade/IUpgrade.h"
#include "upgrade/anvil/AnvilLogic.h"
#include "upgrade/anvil/StationMenu.h"

namespace abp::upgrade::anvil {

// ---- the anvil station (shared by the native tab and the chest menu) ------------------------------
// Settings keys: "left", "right" (items), "name" (the item name text box, SB AnvilUpgradeWrapper
// ITEM_NAME: the left item's name when it is put in, "" while the slot is empty) and
// "nameDefault" (the left item's name as the player's client shows it, so a translated default
// name doesn't count as a rename).

/// The text of the item name box.
[[nodiscard]] std::string itemName(StationRef const& ref);
/// Sets the item name box text (cut to 50 characters). `clientDefault`: the left item's name as
/// the client shows it (nullopt = keep the stored one).
void setItemName(StationRef const& ref, std::string const& name, std::optional<std::string> const& clientDefault = std::nullopt);
/// The rename that the name box asks for (nullopt = keep the name; "" = remove the custom name).
[[nodiscard]] std::optional<std::string> requestedName(StationRef const& ref);
/// The anvil result for `player` (creative players are never "too expensive").
[[nodiscard]] AnvilOutcome stationOutcome(Player& player, StationRef const& ref);
/// Takes the result: consumes the inputs, charges the levels. nullopt when it can't be taken.
[[nodiscard]] std::optional<OutputSlot::Crafted> craft(Player& player, StationRef const& ref);

/// Slot objects of the station (the native tab clicks go through them, like the chest menu's).
[[nodiscard]] std::shared_ptr<InputSlot>  leftSlot(StationRef const& ref);
[[nodiscard]] std::shared_ptr<InputSlot>  rightSlot(StationRef const& ref);
[[nodiscard]] std::shared_ptr<OutputSlot> resultSlot(StationRef const& ref, std::optional<PendingUndo>& pending);

/// The anvil tab as a chest menu (simulated players / selftests; companions get the native tab):
/// left input (slot 2), right input (4), cost (5), result (6), plus the backpack storage below.
class AnvilMenu : public StationMenu {
public:
    AnvilMenu(std::string title, StationRef ref);

    static constexpr int kLeftSlot   = 2;
    static constexpr int kRightSlot  = 4;
    static constexpr int kCostSlot   = 5;
    static constexpr int kResultSlot = 6;

    /// Opens the anvil tab of an installed anvil upgrade.
    static bool open(Player& player, UpgradeContext const& ctx);

    /// Compatibility helpers: the requested rename / set the name box.
    [[nodiscard]] static std::optional<std::string> pendingName(StationRef const& ref) { return requestedName(ref); }
    static void setPendingName(StationRef const& ref, std::string const& name) { setItemName(ref, name); }

    /// The current anvil result for `player` (cached until the inputs change).
    [[nodiscard]] AnvilOutcome const& outcome(Player& player);

protected:
    void buildStation(Player& player) override;
    int  quickMoveStation(Player& player, ItemStack const& stack) override;

private:
    std::optional<AnvilOutcome> mCache;
    std::uint64_t               mCacheRevision = 0;
    bool                        mCacheCreative = false;
};

class AnvilUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const&        info() const override;
    void                                    openSettings(Player& player, UpgradeContext const& ctx) override;
    [[nodiscard]] std::vector<std::string>  describe(Player& player, UpgradeContext const& ctx) const override;
};

} // namespace abp::upgrade::anvil
