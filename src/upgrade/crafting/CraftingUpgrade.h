#pragma once

#include <memory>
#include <optional>

#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/crafting/RecipeLookup.h"
#include "upgrade/crafting/TabMenu.h"

class Player;

namespace abp::upgrade::crafting {

/// Crafting upgrade: a 3x3 crafting grid stored in the upgrade's settings ("grid": 9 stacks),
/// a live result from the game's recipes, craft-one (click) / craft-all (shift-click) and an
/// optional refill of used-up ingredients from the backpack ("refill", default on).

inline constexpr std::string_view kGridKey   = "grid";
inline constexpr std::string_view kRefillKey = "refill";

/// Menu layout (double chest).
inline constexpr int kGridMenuSlots[9] = {1, 2, 3, 10, 11, 12, 19, 20, 21};
inline constexpr int kResultSlot       = 14;
inline constexpr int kInfoSlot         = 13;
inline constexpr int kRefillSlot       = 18;
inline constexpr int kClearSlot        = 7;

[[nodiscard]] core::ItemStackData gridAt(UpgradeContext const& ctx, int index);
void                              setGridAt(UpgradeContext const& ctx, int index, core::ItemStackData const& item);
[[nodiscard]] bool                refillEnabled(UpgradeContext const& ctx);

/// The recipe matching the current grid (cached until the kinds of items in the grid change).
[[nodiscard]] std::optional<CraftMatch> currentMatch(UpgradeContext const& ctx);

/// Crafts once: consumes one item from every occupied grid cell (container items such as
/// buckets stay behind), refills used-up cells from the backpack when enabled, and returns the
/// result for the caller to deliver. Empty (and nothing changes) when no recipe matches.
ItemStack craftOnce(UpgradeContext const& ctx, Player& player);

/// Shift-click: crafts as often as possible (while the result stays the same) and delivers the
/// results to the backpack or the player inventory. Returns the number of crafts.
int craftMax(UpgradeContext const& ctx, Player& player, bool intoBackpack);

/// Moves every grid item into the backpack / player inventory (what fits).
void clearGrid(UpgradeContext const& ctx, Player& player);

/// The crafting tab menu of an installed crafting upgrade (see `openTab`).
[[nodiscard]] std::shared_ptr<TabMenu> makeCraftingMenu(Player& player, UpgradeContext const& ctx);

} // namespace abp::upgrade::crafting
