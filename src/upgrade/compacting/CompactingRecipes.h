#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"

namespace abp::upgrade::compacting {

/// SC `RecipeHelper.CompactingRecipeShape`: a crafting grid pattern where every filled cell holds
/// one of the item being compacted ("111/101/111" = a hollow 3x3 ring of 8).
struct Shape {
    int           width  = 0;
    int           height = 0;
    std::uint32_t filled = 0; ///< bit (y * width + x) set = cell holds the item
    int           count  = 0; ///< filled cells

    [[nodiscard]] static Shape full(int size) {
        Shape s;
        s.width = s.height = size;
        s.count            = size * size;
        s.filled           = (1u << s.count) - 1u;
        return s;
    }
    /// Parses "rows of 1/0 separated by /" (all rows equally long, 1..3 x 1..3, at least two 1s).
    [[nodiscard]] static std::optional<Shape> parse(std::string_view text) {
        Shape s;
        int   x = 0;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '/') {
                if (x == 0 || (s.width != 0 && x != s.width)) return std::nullopt;
                s.width = x;
                ++s.height;
                x = 0;
                if (s.height > 3) return std::nullopt;
                continue;
            }
            char c = text[i];
            if ((c != '0' && c != '1') || x >= 3) return std::nullopt;
            if (c == '1') {
                s.filled |= 1u << (s.height * 3 + x); // packed with stride 3, repacked below
                ++s.count;
            }
            ++x;
        }
        if (s.count <= 1) return std::nullopt; // SC: at least two ingredients
        std::uint32_t packed = 0;
        for (int yy = 0; yy < s.height; ++yy) {
            for (int xx = 0; xx < s.width; ++xx) {
                if ((s.filled >> (yy * 3 + xx)) & 1u) packed |= 1u << (yy * s.width + xx);
            }
        }
        s.filled = packed;
        return s;
    }
    [[nodiscard]] bool isFilled(int x, int y) const { return (filled >> (y * width + x)) & 1u; }
    [[nodiscard]] bool fitsWithin(int maxWidth, int maxHeight) const { return width <= maxWidth && height <= maxHeight; }
    [[nodiscard]] bool operator==(Shape const&) const = default;
};

/// SC `CompactingUpgradeConfig` override entry "mod:item=shape".
struct ShapeOverride {
    std::string item;
    Shape       shape;
    [[nodiscard]] static std::optional<ShapeOverride> parse(std::string_view text) {
        auto eq = text.find('=');
        if (eq == std::string_view::npos || eq == 0) return std::nullopt;
        auto id    = text.substr(0, eq);
        auto colon = id.find(':');
        if (colon == std::string_view::npos || colon == 0 || colon + 1 == id.size()) return std::nullopt;
        for (char c : id) {
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-' || c == ':' || c == '/';
            if (!ok) return std::nullopt;
        }
        auto shape = Shape::parse(text.substr(eq + 1));
        if (!shape) return std::nullopt;
        return ShapeOverride{std::string(id), *shape};
    }
};

/// A compacting recipe found in the game's crafting recipes: `inputs` copies of an item laid out
/// as a shape produce `result` (with `result.count` items).
struct Compaction {
    core::ItemStackData result;          ///< count = items produced by one craft
    int                 inputs     = 0;  ///< items consumed by one craft
    bool                reversible = false; ///< one result crafts back into exactly `inputs` items (SC "uncraftable")
    /// SC `CompactingResult.getRemainingItems` (Java `Recipe.getRemainingItems`): what one craft
    /// leaves behind, e.g. 4 glass bottles for a honey block. Bedrock lists them as the recipe's
    /// extra results; without those, each consumed item's `craftingRemainingItem`.
    std::vector<core::ItemStackData> remaining;
};

/// SC `getCompactingResult(stack, shape)`: the crafting result of `item` laid out as `shape`
/// (the recipe whose result crafts back into the item is preferred). Cached; the cache is dropped
/// whenever the game (re)loads its recipes or a new world starts. Items with custom data (names,
/// enchantments, ...) or damage never compact. Server thread only.
[[nodiscard]] std::optional<Compaction> findCompaction(core::ItemStackData const& item, Shape const& shape);
/// Shorthand for a full `gridSize` x `gridSize` grid (2 or 3).
[[nodiscard]] std::optional<Compaction> findCompaction(core::ItemStackData const& item, int gridSize);

/// SC `uncompactMatchesItem`: one `result` crafts (1x1) back into exactly `count` of `item`.
[[nodiscard]] bool uncompactMatches(core::ItemStackData const& result, core::ItemStackData const& item, int count);

/// SC `RecipeHelper.CompactingShape` classification of an item for full 2x2 / 3x3 grids.
struct CompactingShapes {
    bool twoByTwo            = false; ///< any 2x2 compaction (not the same item, not a loop)
    bool twoByTwoUncraftable = false; ///< ... that crafts back into the 4 items
    bool threeByThree            = false;
    bool threeByThreeUncraftable = false;
};
/// SC `getItemCompactingShapes` (compactions that are part of a compacting loop are left out).
[[nodiscard]] CompactingShapes compactingShapes(core::ItemStackData const& item);

/// Drops the recipe cache (tests, recipe reload).
void invalidateCache();

} // namespace abp::upgrade::compacting
