#pragma once

// Stonecutter upgrade (Sophisticated Core `upgrades/stonecutter` + `blockconverter`): an input slot
// and the selected recipe stored on the upgrade (they travel with the upgrade item), the game's
// stonecutter recipes as a 4x3 scrollable result list (recently taken results first), SB's
// expanded searchable output list, "Shift Click Result Into Backpack / Player's Inventory" and
// "Refill Input" (from the backpack). Shown in the upgrade's native tab
// (client/ui/tabs/stonecutter); not toggleable.
//
// Tab data: {"input":item|null, "output":item|null, "recipes":[result items], "selected":n,
//   "recent":[order per recipe, -1 = not recent], "shiftIntoStorage":bool, "refill":bool}
// Tab actions: {"input":true,"button":b,"shift":s} {"output":true,"button":b,"shift":s}
//   {"recipe":i} {"toggle":"shiftTarget"|"refill"} {"tabOpen":bool,"uuid":..}

#include <optional>
#include <string>
#include <vector>

#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/crafting/RecipeLookup.h"

class Player;

namespace abp::upgrade::stonecutter {

inline constexpr std::string_view kInputKey = "input";
/// SB RECIPE_ID: the last selected recipe (selected again when its input comes back).
inline constexpr std::string_view kSelectedKey = "selected";
/// SB SHIFT_CLICK_INTO_STORAGE (default true).
inline constexpr std::string_view kShiftIntoStorageKey = "shiftIntoBackpack";
/// SB REFILL_INPUT (default false).
inline constexpr std::string_view kRefillKey = "refillInput";
/// SC RecentCraftedResultStorage scope of the stonecutter.
inline constexpr std::string_view kRecentScope = "minecraft:stonecutting";

[[nodiscard]] core::ItemStackData inputOf(UpgradeContext const& ctx);
void                              setInput(UpgradeContext const& ctx, core::ItemStackData const& item);
[[nodiscard]] bool                shiftIntoStorage(UpgradeContext const& ctx);
void                              setShiftIntoStorage(UpgradeContext const& ctx, bool value);
[[nodiscard]] bool                refillInput(UpgradeContext const& ctx);
void                              setRefillInput(UpgradeContext const& ctx, bool value);

/// The recipes for the current input (cached until the input item kind changes).
[[nodiscard]] std::vector<crafting::StonecutterOption> const& options(UpgradeContext const& ctx);
[[nodiscard]] std::string                                     selectedId(UpgradeContext const& ctx);
/// Index of the selected recipe in `options` (-1 = none).
[[nodiscard]] int selectedIndex(UpgradeContext const& ctx);
/// SC selectRecipe: selects `options[index]` (ignored when out of range).
bool selectRecipe(UpgradeContext const& ctx, int index);
/// The selected recipe, if valid for the current input.
[[nodiscard]] std::optional<crafting::StonecutterOption> selectedOption(UpgradeContext const& ctx);

/// SC ResultSlot.onTake: records the recent result, consumes one input item, refills it from the
/// backpack (with "Refill Input") and returns the result. Empty when nothing valid is selected.
ItemStack craftOnce(UpgradeContext const& ctx, Player& player);
/// Shift-click on the output (SC quickMoveStack, repeated). Returns the number of crafts.
int craftShift(UpgradeContext const& ctx, Player& player);
/// Shift-click on the input: into the backpack storage, then the player inventory.
bool shiftInput(UpgradeContext const& ctx, Player& player);
/// Shift-click into the open stonecutter tab: into the input slot. Returns how many it took.
int quickMoveIntoInput(UpgradeContext const& ctx, core::ItemStackData const& item);

/// Order of each recipe in the player's recent results for the current input (-1 = not recent).
[[nodiscard]] std::vector<int> recentOrder(Player& player, UpgradeContext const& ctx);

} // namespace abp::upgrade::stonecutter
