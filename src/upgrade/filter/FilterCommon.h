#pragma once

// Helpers shared by the filter-based upgrades of this unit (Pickup, Filter, Restock, Deposit):
// SB's per-upgrade filter configs, reading the filter from the upgrade settings with the tier's
// options applied, SC's contents filter matching, the lore lines and the (chest) editor menu used
// by simulated players and selftests. The native UI is the shared `FilterControl` tab.

#include <memory>
#include <string>
#include <vector>

#include "core/Filter.h"
#include "ui/menu/FilterMenu.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/filter/FilterTab.h"

class Player;

namespace abp::game {
class Backpack;
}

namespace abp::upgrade::filtering {

/// Default filter slot counts (SB: basic 9, advanced 16; configurable).
inline constexpr int kBasicSlots    = 9;
inline constexpr int kAdvancedSlots = 16;

/// Settings key of the filter.
inline constexpr std::string_view kFilterKey = "filter";

[[nodiscard]] constexpr int slotsFor(UpgradeTier tier) { return tier == UpgradeTier::Advanced ? kAdvancedSlots : kBasicSlots; }

/// SB's server config of one upgrade family (`config/<section>.json`):
/// {"version", "<name>Upgrade": {"filterSlots","slotsInRow"}, "advanced<Name>Upgrade": {...}}.
struct PickupConfig {
    int                   version               = 1;
    FilteredUpgradeConfig pickupUpgrade         = {9, 3};
    FilteredUpgradeConfig advancedPickupUpgrade = {16, 4};
};
struct FilterConfig {
    int                   version               = 1;
    FilteredUpgradeConfig filterUpgrade         = {9, 3};
    FilteredUpgradeConfig advancedFilterUpgrade = {16, 4};
};
struct RestockConfig {
    int                   version                = 1;
    FilteredUpgradeConfig restockUpgrade         = {9, 3};
    FilteredUpgradeConfig advancedRestockUpgrade = {16, 4};
};
struct DepositConfig {
    int                   version                = 1;
    FilteredUpgradeConfig depositUpgrade         = {9, 3};
    FilteredUpgradeConfig advancedDepositUpgrade = {16, 4};
};

/// The filter control of an upgrade of `group` ("pickup", "filter", "restock", "deposit") and
/// `tier`, from its config.
[[nodiscard]] FilterSpec specFor(std::string_view group, UpgradeTier tier);
/// `specFor` with the group of the upgrade installed at `ctx` (falls back to "pickup").
[[nodiscard]] FilterSpec specFor(UpgradeContext const& ctx, UpgradeTier tier);

/// Applies what a tier can configure: basic filters only have the type button and always match
/// by item, ignoring NBT and durability. Resizes the filter slots to `slots`.
void normalize(core::Filter& filter, UpgradeTier tier, int slots);
inline void normalize(core::Filter& filter, UpgradeTier tier) { normalize(filter, tier, slotsFor(tier)); }

/// Reads a stored filter as SC would: a missing filter is a new block list; phase-1 data (no
/// version) whose empty allow list meant "everything" becomes the equivalent empty block list;
/// the phase-1 Deposit/Restock "contents" setting becomes the INVENTORY / STORAGE type.
[[nodiscard]] core::Filter
read(nlohmann::json const& settings, int slots, core::FilterDefaults defaults = {}, std::string_view group = {});

/// The filter stored in `ctx.settings()["filter"]`, normalized for the upgrade.
[[nodiscard]] core::Filter load(UpgradeContext const& ctx, UpgradeTier tier);

/// Writes the filter back (normalized) and rebuilds the hooks.
void store(UpgradeContext const& ctx, UpgradeTier tier, core::Filter filter);

/// SC ContentsFilterLogic: `filter` against `item`, where STORAGE matches the items stored in
/// `backpack` (full and partial stacks) and its memorized slots.
[[nodiscard]] bool matchesContents(core::Filter const& filter, game::Backpack& backpack, core::ItemStackData const& item);

/// One-count copies of the distinct items stored in a backpack.
[[nodiscard]] std::vector<core::ItemStackData> storedItems(game::Backpack& backpack);

/// Editor options for `tier` (basic: allow/block only).
[[nodiscard]] ui::FilterMenu::Options menuOptions(UpgradeTier tier);

/// Lore lines describing a filter ("Allow: 3 items", match options).
[[nodiscard]] std::vector<std::string> describe(Player& player, core::Filter const& filter, UpgradeTier tier);

/// True while `ctx` still refers to an installed upgrade with item id `itemId` (menus can outlive
/// a removal).
[[nodiscard]] bool stillInstalled(UpgradeContext const& ctx, std::string const& itemId);

/// The SB tab title ("Pickup", "Adv. Pickup") and closed tab tooltip ("Pickup Settings") of an
/// upgrade, for the native tab data.
[[nodiscard]] nlohmann::json tabTexts(Player& player, std::string_view group, bool advanced);

/// Registers the native tab of an upgrade family whose filter is `ctx.settings()["filter"]`
/// (`registerSettingsFilterTab` with `specFor(group)`, `load`, `store`) plus its tab title and
/// tooltip (`tabTexts`). Call at static-init time.
void registerStandardFilterTab(std::string group);

/// The filter editor of an installed upgrade (chest menu: simulated players, selftests): ghost
/// slots + the shared control row, bound to `ctx.settings()["filter"]` (normalized for `tier`).
/// "Back" returns to the backpack's upgrades page. Subclasses add buttons to the free control
/// slots (5 and 6) in `addButtons()`.
class UpgradeFilterMenu : public ui::FilterMenu {
public:
    UpgradeFilterMenu(Player& player, UpgradeContext ctx, UpgradeTier tier);

    void build(Player& player) override;

    [[nodiscard]] UpgradeContext const& context() const { return mCtx; }
    /// Index of control-row slot `i` (0..8).
    [[nodiscard]] int controlSlot(int i) const { return size() - 9 + i; }

protected:
    virtual void addButtons(Player& /*player*/) {}

private:
    UpgradeFilterMenu(Player& player, UpgradeContext ctx, UpgradeTier tier, int tag);

    UpgradeContext mCtx;
};

} // namespace abp::upgrade::filtering
