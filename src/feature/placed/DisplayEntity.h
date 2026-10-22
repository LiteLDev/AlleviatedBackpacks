#pragma once

// P2-11: the entity that draws a placed backpack.
//
// Bedrock custom blocks have no block entity renderer and no per-position data, so a placed
// backpack block is invisible and an invisible, inert "display" entity at its position wears a
// copy of the backpack item. The game then draws the backpack attachable on it (the same tinted,
// part-composed model as worn / held, see client/render/ModelState.h, context "placed") and the
// item display as the entity's held item. The server keeps exactly one display entity per placed
// backpack (spawned / re-synced on the placed tick, removed with the block).
//
// The copies it wears / holds carry `kCopyKey` in their user data: they are never real items.
// The entity drops nothing (equipment drop chance 0, no damage), and any copy found in a player's
// inventory is deleted.

#include <optional>
#include <string_view>

#include "feature/placed/PlacedBackpacks.h"

class Actor;
class BlockSource;
class ItemStack;
class Player;

namespace abp::placed::display {

inline constexpr std::string_view kEntityId = "alleviatedbackpacks:backpack_display";
inline constexpr std::string_view kCopyKey  = "abp_display_copy";

/// Ensures the display entity of `entry` exists, faces `entry.facing` and shows `backpack` (the
/// placed item) and `displayItem` (nullopt = none). Chunk must be loaded.
void sync(BlockSource& region, PlacedEntry const& entry, ItemStack const& backpack, std::optional<ItemStack> const& displayItem);
/// Removes the display entities at `loc` (the backpack left the block).
void remove(BlockSource& region, BlockLocation const& loc);
/// The display entity at `loc`, or nullptr.
[[nodiscard]] Actor* find(BlockSource& region, BlockLocation const& loc);

/// True for an item that is a display copy (never a real item).
[[nodiscard]] bool isCopy(ItemStack const& stack);
/// Deletes display copies from the player's inventory (should never happen; a safety net).
int purgeCopies(Player& player);

} // namespace abp::placed::display
