#pragma once

#include <optional>

#include "core/ItemStackData.h"

namespace abp::upgrade::compacting {

/// A compacting recipe found in the game's crafting recipes: `inputs` copies of an item fill an
/// n x n crafting grid and produce `result` (with `result.count` items).
struct Compaction {
    core::ItemStackData result;          ///< count = items produced by one craft
    int                 inputs     = 0;  ///< items consumed by one craft (4 or 9)
    bool                reversible = false; ///< one result crafts back into exactly `inputs / result.count` items
};

/// Looks up (and caches) the compacting recipe of `item` for an n x n grid (`gridSize` 2 or 3).
/// The cache is dropped whenever the game (re)loads its recipes or a new world starts.
/// Items with custom data (names, enchantments, ...) or damage never compact.
/// Server thread only.
[[nodiscard]] std::optional<Compaction> findCompaction(core::ItemStackData const& item, int gridSize);

/// Drops the recipe cache (tests, recipe reload).
void invalidateCache();

} // namespace abp::upgrade::compacting
