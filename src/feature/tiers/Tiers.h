#pragma once

// U1: backpack tiers, recipes and presentation (lore, colours).
//
// - Recipes live in the behavior pack (assets/pack/bp/recipes/tiers/): Leather is crafted,
//   Copper..Diamond are "metal around the previous tier", Netherite is a smithing-table (or
//   crafting) upgrade of the Diamond backpack, and every tier can be dyed with one dye at a time
//   (beside the backpack = main colour, above/below = accent).
// - Crafting hooks (TierCrafting.cpp) carry the backpack identity (uuid + item user data) from the
//   consumed backpack into the crafted one, so tier upgrades and dyeing keep contents and upgrades.
//   `Storage::forItem` grows the storage when the item's tier is higher than the stored one.
// - TierLore.cpp removes the phase-1 lore (SB backpacks have none; uildLore stays for that
//   migration) and keeps the item tint (main colour, via the `minecraft:dyeable` item component) in sync with the storage.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Tier.h"

class ItemStack;
class ItemStackBase;
class Player;

namespace abp::game {
class Backpack;
}

namespace abp::tiers {

// ---- presentation ---------------------------------------------------------------------------

/// User-data key holding the colour (0xRRGGBB, -1 = none) we last tinted the item with. Lets the
/// refresher tell its own tint from a cauldron dye/wash done by the player.
inline constexpr std::string_view kTintKey = "abp_tint";

/// The lore lines for a backpack of `tier` (`backpack` may be null for a never-opened backpack),
/// in `player`'s language.
[[nodiscard]] std::vector<std::string> buildLore(Player const& player, core::Tier tier, game::Backpack const* backpack);

/// Translated colour label: the dye name if it is a dye colour, else "#RRGGBB".
[[nodiscard]] std::string colorLabel(Player const& player, std::uint32_t argb);

/// Brings `stack` (a backpack item) up to date: lore, and the tint (main colour). A cauldron
/// dye or wash on the item since our last tint is adopted into the storage first. Returns true if
/// the stack changed (the caller writes it back).
bool refreshItem(Player const& player, ItemStack& stack, std::shared_ptr<game::Backpack> const& backpack);

/// Refreshes every backpack the player carries (skipped while one of our menus is open).
/// Returns how many items were rewritten.
int refreshPlayer(Player& player);

// ---- crafting -------------------------------------------------------------------------------

/// One item consumed by a craft. `slot` is the PlayerUISlot index of the input slot when
/// `inGrid` (consumed from the crafting input grid); recipe-book crafts may consume straight from
/// the inventory (`inGrid` false: no position).
struct ConsumedInput {
    int                        slot = -1;
    std::shared_ptr<ItemStack> item;
    bool                       inGrid = true;
};

/// Row-major index of a crafting input slot in a 3-wide grid (2x2 slots map to their 3x3
/// position), or -1 for slots outside the crafting grid.
[[nodiscard]] int craftGridIndex(int uiSlot);

/// What a craft does to a backpack.
struct BackpackCraft {
    std::shared_ptr<ItemStack>   source;      ///< the consumed backpack
    std::optional<std::uint32_t> mainColor;   ///< from dyes beside the backpack
    std::optional<std::uint32_t> accentColor; ///< from dyes above / below the backpack
    bool                         createdStorage = false; ///< `carryOver` created the storage entry
    [[nodiscard]] bool           dyes() const { return mainColor || accentColor; }
};

/// Looks at the consumed inputs: exactly one backpack -> its plan. nullopt otherwise.
[[nodiscard]] std::optional<BackpackCraft> analyse(std::vector<ConsumedInput> const& inputs);

/// Makes `result` (a crafted backpack) the same backpack as `craft.source`: copies the source's
/// user data (uuid, name, upgrades travel with the uuid). If the craft changes colours and the
/// source was never opened, a storage entry is created first (its uuid is written to
/// `craft.source` so repeated calls reuse it). Returns the uuid bound (may be empty).
std::string carryOver(ItemStackBase& result, BackpackCraft& craft);

/// Applies a craft's colours to the stored backpack `uuid`. Returns false if it can't be loaded.
bool commitColors(std::string const& uuid, BackpackCraft const& craft);

} // namespace abp::tiers
