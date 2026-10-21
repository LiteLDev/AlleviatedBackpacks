#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/crafting/RecipeLookup.h"
#include "upgrade/crafting/TabMenu.h"

class Player;

namespace abp::upgrade::stonecutter {

/// Stonecutter upgrade: an input slot ("input") and the selected recipe ("selected", a recipe id)
/// in the upgrade's settings; the choices come from the game's "stonecutter" recipes.

inline constexpr std::string_view kInputKey    = "input";
inline constexpr std::string_view kSelectedKey = "selected";

/// Menu layout (double chest).
inline constexpr int kInfoSlot        = 1;
inline constexpr int kInputSlot       = 10;
inline constexpr int kResultSlot      = 19;
inline constexpr int kRecipePageSlot  = 26;
inline constexpr int kRecipeSlots[15] = {3, 4, 5, 6, 7, 12, 13, 14, 15, 16, 21, 22, 23, 24, 25};

[[nodiscard]] core::ItemStackData inputOf(UpgradeContext const& ctx);
void                              setInput(UpgradeContext const& ctx, core::ItemStackData const& item);

/// The choices for the current input (cached until the input item kind changes).
[[nodiscard]] std::vector<crafting::StonecutterOption> const& options(UpgradeContext const& ctx);
[[nodiscard]] std::string                                     selectedId(UpgradeContext const& ctx);
void select(UpgradeContext const& ctx, std::string const& recipeId);
/// The selected choice, if it is valid for the current input.
[[nodiscard]] std::optional<crafting::StonecutterOption> selectedOption(UpgradeContext const& ctx);

/// Cuts once: consumes one input item, returns the result (empty if nothing valid is selected).
ItemStack craftOnce(UpgradeContext const& ctx);
/// Shift-click: cuts as often as possible, delivering to the backpack or the player inventory.
int craftMax(UpgradeContext const& ctx, Player& player, bool intoBackpack);

[[nodiscard]] std::shared_ptr<crafting::TabMenu> makeStonecutterMenu(Player& player, UpgradeContext const& ctx);

} // namespace abp::upgrade::stonecutter
