#pragma once

// Inventory columns taken by upgrade inventory parts (Sophisticated Core
// `IUpgradeItem.getInventoryColumnsTaken`): a Tank or Battery upgrade replaces two columns of the
// backpack's storage with its bar (the client draws it right of the slots). The inventory then has
// `base - rows x columnsTaken` slots (`core::capacity::slotsAfterColumns`), where rows are the
// base layout's rows; tank / battery sizes keep using the base rows.
//
// Contract (additive): an upgrade type that takes columns registers them with
// `ABP_REGISTER_INVENTORY_COLUMNS("tank", 2)`, checks `columns::canInstall` in its
// `canInstallItem` and calls `columns::sync` from `onInstall` / `onRemove`.

#include <string>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"
#include "mod/Registry.h"
#include "upgrade/IUpgrade.h"

namespace abp::upgrade::columns {

/// Upgrades with id `upgradeId` take `columns` storage columns. Static init only.
void registerColumns(std::string upgradeId, int columns);
/// Columns an upgrade item takes (0 for most upgrades).
[[nodiscard]] int columnsOf(core::ItemStackData const& upgradeItem);
/// Columns the installed upgrade items take together (enabled or not, like SB), skipping
/// upgrade slot `excludeSlot`.
[[nodiscard]] int expected(game::Backpack const& backpack, int excludeSlot = -1);

struct Check {
    bool             ok = true;
    int              slotsNeeded = 0; ///< SB: "the last %s slots"
    std::vector<int> errorSlots;      ///< storage slots whose items don't fit
};
/// Whether the backpack can give `targetColumns` columns in total (SB `needsSlotsThatAreOccupied`):
/// the items must fit into the smaller inventory (memory / no-sort slots don't move).
[[nodiscard]] Check check(game::Backpack& backpack, int targetColumns);
/// `canInstallItem` helper for an upgrade taking columns: installing `item` into `ctx.slot()`.
/// On failure `reason` gets SB's "There's an item in at least one of the last %s slots needed for
/// %s" (translated for `ctx.player()`).
bool canInstall(UpgradeContext const& ctx, core::ItemStackData const& item, std::string& reason);

/// Moves items and resizes the inventory so that `targetColumns` columns are taken. Returns false
/// (nothing changed) when the items don't fit.
bool apply(game::Backpack& backpack, int targetColumns);
/// `apply(expected(backpack, excludeSlot))`; true if the columns now match.
bool sync(game::Backpack& backpack, int excludeSlot = -1);

namespace detail {
struct ColumnsRegistrar {
    ColumnsRegistrar(std::string id, int columns) { registerColumns(std::move(id), columns); }
};
} // namespace detail

} // namespace abp::upgrade::columns

#define ABP_REGISTER_INVENTORY_COLUMNS(id, count)                                                                      \
    static ::abp::upgrade::columns::detail::ColumnsRegistrar ABP_UNIQUE(abp_inventory_columns_)(id, count)
