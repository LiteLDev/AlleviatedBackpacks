#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/Backpack.h"
#include "game/PlayerScan.h"
#include "ui/menu/BackpackMenu.h"

class Player;

namespace abp::upgrade::inception {

inline constexpr std::string_view kId = "inception";

/// `config/inception.json` (SB `InceptionUpgradeConfig`).
struct InceptionConfig {
    int version = 2;
    /// Allows / disallows backpack upgrades to work with inventories of backpacks in the backpack
    /// with Inception upgrade (also hides the Inception tab when off).
    bool upgradesUseInventoriesOfBackpacksInBackpack = true;
    /// Allows / disallows upgrades to be functional even when they are in backpacks in the
    /// inventory of a backpack with Inception upgrade (their tanks / batteries join the outer's).
    bool upgradesInContainedBackpacksAreFunctional = true;
};

/// SB `InventoryOrder` (per Inception upgrade, setting "inventoryOrder"; default main first).
enum class InventoryOrder { MainFirst, InceptedFirst };

[[nodiscard]] std::string_view toString(InventoryOrder order);         ///< "main_first" / "incepted_first"
[[nodiscard]] InventoryOrder   inventoryOrderFromString(std::string_view s);
/// The order of the Inception upgrade installed in `backpack`, nullopt if none is installed.
[[nodiscard]] std::optional<InventoryOrder> inventoryOrderOf(game::Backpack const& backpack);
/// True if `backpack` has an Inception upgrade in any upgrade slot (enabled or not).
[[nodiscard]] bool hasInception(game::Backpack const& backpack);

/// SC `getInventoryForUpgradeProcessing`: the inventories the upgrades of `backpack` work with, in
/// order. Just `backpack` itself, unless it has an Inception upgrade and
/// `upgradesUseInventoriesOfBackpacksInBackpack` is on: then also the backpacks stored inside it
/// (slot order), after (main first) or before (incepted first) its own inventory.
[[nodiscard]] std::vector<std::shared_ptr<game::Backpack>> upgradeInventories(std::shared_ptr<game::Backpack> const& backpack);
/// SB `InceptionInventoryHandler.insertItem`: offers `item` to `upgradeInventories(backpack)` in
/// order (each inventory's own hooks apply). Returns how many items were taken (inserted or voided).
int insertForUpgrades(
    std::shared_ptr<game::Backpack> const& backpack,
    core::ItemStackData const&             item,
    core::AccessContext const&             access,
    bool                                   simulate = false
);
/// The backpacks stored inside `backpack` (slot order; resolved, uuids assigned when missing).
[[nodiscard]] std::vector<std::shared_ptr<game::Backpack>> subBackpacks(std::shared_ptr<game::Backpack> const& backpack);

/// Storage slots of `backpack` that hold backpack items.
[[nodiscard]] std::vector<int> backpackSlots(game::Backpack& backpack);
/// The backpack stored in slot `index` of `parent` (assigns a uuid to the stored item if it has
/// none yet). nullptr if that slot doesn't hold a usable backpack.
[[nodiscard]] std::shared_ptr<game::Backpack> resolveNested(std::shared_ptr<game::Backpack> const& parent, int index);

/// Appends the backpacks nested in the directly carried ones (installed as the PlayerScan provider).
void appendNested(Player& player, std::vector<std::pair<game::CarriedBackpack, std::shared_ptr<game::Backpack>>>& list);

/// A backpack opened from inside another backpack's GUI. Shows a "Back" button (slot 46) that
/// returns to the containing backpack, and closes itself when the chain of containers changes.
class NestedBackpackMenu : public ui::BackpackMenu {
public:
    /// `ancestors`: uuids from the outermost backpack down to the direct parent (non-empty).
    /// `rootSource`: where the outermost backpack item is on the player (nullopt if not carried).
    NestedBackpackMenu(
        std::shared_ptr<game::Backpack>   backpack,
        std::optional<game::ItemLocation> rootSource,
        std::vector<std::string>          ancestors,
        std::string                       title
    );

    void build(Player& player) override;
    void onTick(Player& player) override;

    [[nodiscard]] std::optional<game::ItemLocation> const& rootSource() const { return mRootSource; }
    [[nodiscard]] std::vector<std::string> const&          ancestors() const { return mAncestors; }

    /// Returns to the containing backpack.
    void goBack(Player& player);
    /// True while the chain root -> ... -> this backpack is intact.
    [[nodiscard]] bool chainValid(Player& player) const;

private:
    std::optional<game::ItemLocation> mRootSource;
    std::vector<std::string>          mAncestors;
};

/// Opens the backpack in slot `index` of `parent`, as seen from `menu` (the menu showing
/// `parent`, used to know the chain back to the carried backpack). Returns false if that slot
/// holds no backpack.
bool openNested(Player& player, ui::Menu const* menu, std::shared_ptr<game::Backpack> const& parent, int index);

} // namespace abp::upgrade::inception
