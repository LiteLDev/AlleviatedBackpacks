#pragma once

// Helpers shared by the filter-based upgrades of this unit (Pickup, Filter): reading the filter
// from the upgrade settings with the tier's options applied, the lore lines and the editor menu.

#include <memory>
#include <string>
#include <vector>

#include "core/Filter.h"
#include "ui/menu/FilterMenu.h"
#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::filtering {

/// Ghost slot counts (as Sophisticated Backpacks: basic 9, advanced 16).
inline constexpr int kBasicSlots    = 9;
inline constexpr int kAdvancedSlots = 16;

/// Settings key of the filter.
inline constexpr std::string_view kFilterKey = "filter";

[[nodiscard]] constexpr int slotsFor(UpgradeTier tier) { return tier == UpgradeTier::Advanced ? kAdvancedSlots : kBasicSlots; }

/// Applies what a tier can configure: basic filters only have an allow/deny list of items and
/// always match by item, ignoring NBT and durability.
void normalize(core::Filter& filter, UpgradeTier tier);

/// The filter stored in `ctx.settings()["filter"]`, normalized for `tier`.
[[nodiscard]] core::Filter load(UpgradeContext const& ctx, UpgradeTier tier);

/// Writes the filter back (normalized) and rebuilds the hooks.
void store(UpgradeContext const& ctx, UpgradeTier tier, core::Filter filter);

/// Editor options for `tier` (basic: allow/deny only).
[[nodiscard]] ui::FilterMenu::Options menuOptions(UpgradeTier tier);

/// Lore lines describing a filter ("Allow list: 3 items", match options).
[[nodiscard]] std::vector<std::string> describe(Player& player, core::Filter const& filter, UpgradeTier tier);

/// True while `ctx` still refers to an installed upgrade with item id `itemId` (menus can outlive
/// a removal).
[[nodiscard]] bool stillInstalled(UpgradeContext const& ctx, std::string const& itemId);

/// The filter editor of an installed upgrade: ghost slots + the shared control row, bound to
/// `ctx.settings()["filter"]` (normalized for `tier`). "Back" returns to the backpack's upgrades
/// page. Subclasses add buttons to the free control slots (5 and 6) in `addButtons()`.
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
