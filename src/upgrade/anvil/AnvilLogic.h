#pragma once

#include <optional>
#include <string>

#include "mc/world/item/ItemStack.h"

class Player;

namespace abp::upgrade::anvil {

/// Longest name the anvil accepts.
inline constexpr std::size_t kMaxNameLength = 50;

/// What putting `left` and `right` into an anvil (and optionally typing `name`) produces.
struct AnvilOutcome {
    ItemStack result;              ///< empty: nothing to take
    int       cost         = 0;    ///< experience levels
    bool      tooExpensive = false;
    int       rightUsed    = 0;    ///< items consumed from the right input
    bool      renameOnly   = false;

    [[nodiscard]] bool hasResult() const { return !result.isNull(); }
};

/// Computes the anvil result like the vanilla anvil: material repair, combining two items
/// (durability + enchantments, merged by the game's own `EnchantUtils::combineEnchantedItems`,
/// so level caps and incompatibilities are vanilla), enchanted books, renaming and the prior
/// work penalty. `name`: nullopt = leave the name alone; blank = remove a custom name.
[[nodiscard]] AnvilOutcome compute(
    ItemStack const&                  left,
    ItemStack const&                  right,
    std::optional<std::string> const& name,
    bool                              creative
);

/// Whether shift-clicking `stack` should put it into the anvil's left slot (damageable, enchanted
/// or an enchanted book); everything else goes to the backpack storage.
[[nodiscard]] bool isAnvilSubject(ItemStack const& stack);

/// The player's experience level.
[[nodiscard]] int playerLevel(Player& player);

/// Whether `player` can pay for `outcome` right now (creative players always can).
[[nodiscard]] bool canAfford(Player& player, AnvilOutcome const& outcome);

} // namespace abp::upgrade::anvil
