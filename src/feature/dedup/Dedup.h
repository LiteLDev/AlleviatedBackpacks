#pragma once

// Sophisticated Backpacks' UUIDDeduplicator: backpack items that share a storage uuid (duplication
// glitches, mods that copy drops, `/sbp give` to several players) are sorted out so only one item
// keeps the storage. The copy with more storage slots keeps the uuid (the first one on a tie); the
// others lose it and get a fresh, empty storage when opened. Linked-storage endpoints share their
// group's uuid on purpose and are never touched.
//
// - When a backpack item entity appears: other backpack item entities within 10 blocks.
// - Every 40 ticks per dimension: the backpacks in every player's inventory, offhand and chest
//   slot across that dimension (unless `tickDedupeLogicDisabled`).
// - When a player opens a backpack: that player's other backpacks.

class ItemActor;
class ItemStack;
class Player;
class BlockSource;

#include "game/PlayerScan.h"

namespace abp::dedup {

/// SB's preference: true if `first` keeps the uuid over `second` (slots of first >= second).
[[nodiscard]] bool firstPreferred(ItemStack const& first, ItemStack const& second);
/// Removes the storage uuid from a backpack item (SB: removeContentsUUIDTag).
void removeUuid(ItemStack& stack);

/// Dedupes a new backpack item entity against item entities within 10 blocks. Returns how many
/// uuids were removed.
int dedupeItemEntityInArea(ItemActor& entity, BlockSource& region);
/// Dedupes the backpacks of the players in `dimension` (the periodic check). Returns how many
/// uuids were removed.
int dedupePlayers(int dimension);
/// Dedupes `player`'s backpacks against the one at `openedAt` (opening a backpack). Returns how
/// many uuids were removed.
int dedupeOnOpen(Player& player, game::ItemLocation openedAt);

} // namespace abp::dedup
