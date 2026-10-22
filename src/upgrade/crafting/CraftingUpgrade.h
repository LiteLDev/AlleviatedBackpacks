#pragma once

// Crafting upgrade (Sophisticated Core `upgrades/crafting`): a 3x3 crafting grid stored on the
// upgrade (it travels with the upgrade item), the game's crafting recipes with SB's result cycler
// for grids that match several recipes, "Shift Click Result Into Backpack / Player's Inventory"
// and "Refill Crafting Grid" (from the backpack, then the player). Shown in the upgrade's native
// tab (client/ui/tabs/crafting); not toggleable.
//
// Tab data (`UpgradeView::tab`): {"grid":[9 items|null], "result":item|null, "results":[items],
//   "selected":n, "shiftIntoStorage":bool, "refill":bool}
// Tab actions: {"grid":i,"button":b,"shift":s} {"result":true,"button":b,"shift":s}
//   {"select":i} {"next":true} {"previous":true} {"toggle":"shiftTarget"|"refill"}
//   {"tabOpen":bool,"uuid":..}

#include <optional>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/crafting/RecipeLookup.h"
#include "upgrade/crafting/Station.h"

class Player;

namespace abp::upgrade::crafting {

inline constexpr std::string_view kGridKey = "grid";
/// SB SHIFT_CLICK_INTO_STORAGE (default true).
inline constexpr std::string_view kShiftIntoStorageKey = "shiftIntoBackpack";
/// SB REFILL_CRAFTING_GRID (default false).
inline constexpr std::string_view kRefillKey = "refill";

[[nodiscard]] core::ItemStackData gridAt(UpgradeContext const& ctx, int index);
void                              setGridAt(UpgradeContext const& ctx, int index, core::ItemStackData const& item);
[[nodiscard]] bool                shiftIntoStorage(UpgradeContext const& ctx);
void                              setShiftIntoStorage(UpgradeContext const& ctx, bool value);
[[nodiscard]] bool                refillEnabled(UpgradeContext const& ctx);
void                              setRefill(UpgradeContext const& ctx, bool value);

/// The recipes matching the current grid and the selected one (cached until the kinds of items in
/// the grid change; the last chosen recipe stays selected while it still matches).
struct CraftResults {
    std::vector<CraftMatch> matches;
    int                     selected = 0;
};
[[nodiscard]] CraftResults const& results(UpgradeContext const& ctx);
/// The selected match (nullopt = no recipe).
[[nodiscard]] std::optional<CraftMatch> currentMatch(UpgradeContext const& ctx);
/// SC selectCraftingResult / selectNext / selectPrevious.
void selectResult(UpgradeContext const& ctx, int index);
void selectNextResult(UpgradeContext const& ctx);
void selectPreviousResult(UpgradeContext const& ctx);

/// SC ResultSlot.onTake: consumes one item from every occupied cell (container items stay behind;
/// with "Refill Crafting Grid" the item comes from the backpack / player instead) and returns the
/// result for the caller to deliver. Empty (and nothing changes) when no recipe matches.
ItemStack craftOnce(UpgradeContext const& ctx, Player& player);

/// Shift-click on the result (SC quickMoveStack, repeated): crafts while the result stays the
/// same item (at most a stack's worth of crafts with refill on), each into the backpack or the
/// player's inventory as set. Returns the number of crafts.
int craftShift(UpgradeContext const& ctx, Player& player);

/// Shift-click on grid cell `index`: into the backpack storage, then the player inventory.
bool shiftGridCell(UpgradeContext const& ctx, Player& player, int index);

/// Selftests: replaces the cached matches of the current grid (e.g. to cycle results when no
/// vanilla grid matches several recipes).
void injectResultsForTest(UpgradeContext const& ctx, std::vector<CraftMatch> matches);

/// Shift-click into the open crafting tab (SC mergeItemStack over the grid slots): merges into
/// cells with the same item, then fills the first empty cell. Returns how many items it took.
int quickMoveIntoGrid(UpgradeContext const& ctx, core::ItemStackData const& item);

} // namespace abp::upgrade::crafting
