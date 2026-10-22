#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/BackpackInventory.h"

namespace abp::core {

/// The 16 vanilla dye colours, in vanilla order.
enum class DyeColor : int {
    White, Orange, Magenta, LightBlue, Yellow, Lime, Pink, Gray,
    LightGray, Cyan, Purple, Blue, Brown, Green, Red, Black,
};

struct DyeInfo {
    DyeColor         color;
    std::string_view id;  ///< "light_blue" (item id is "minecraft:<id>_dye")
    std::uint32_t    argb; ///< the dye's colour, 0xFFRRGGBB
};

inline constexpr std::array<DyeInfo, 16> kDyes{{
    {DyeColor::White,     "white",      0xFFF9FFFE},
    {DyeColor::Orange,    "orange",     0xFFF9801D},
    {DyeColor::Magenta,   "magenta",    0xFFC74EBD},
    {DyeColor::LightBlue, "light_blue", 0xFF3AB3DA},
    {DyeColor::Yellow,    "yellow",     0xFFFED83D},
    {DyeColor::Lime,      "lime",       0xFF80C71F},
    {DyeColor::Pink,      "pink",       0xFFF38BAA},
    {DyeColor::Gray,      "gray",       0xFF474F52},
    {DyeColor::LightGray, "light_gray", 0xFF9D9D97},
    {DyeColor::Cyan,      "cyan",       0xFF169C9C},
    {DyeColor::Purple,    "purple",     0xFF8932B8},
    {DyeColor::Blue,      "blue",       0xFF3C44AA},
    {DyeColor::Brown,     "brown",      0xFF835432},
    {DyeColor::Green,     "green",      0xFF5E7C16},
    {DyeColor::Red,       "red",        0xFFB02E26},
    {DyeColor::Black,     "black",      0xFF1D1D21},
}};

[[nodiscard]] constexpr DyeInfo const& dyeInfo(DyeColor c) { return kDyes[static_cast<std::size_t>(c)]; }

/// The dye colour of a dye item id ("minecraft:red_dye"), or nullopt.
[[nodiscard]] std::optional<DyeColor> dyeFromItemId(std::string_view itemId);
/// The dye whose colour is exactly `argb` (alpha ignored), or nullopt.
[[nodiscard]] std::optional<DyeColor> dyeFromColor(std::uint32_t argb);

/// Mixes colours like vanilla leather-armour dyeing: average of the channels, scaled so the
/// brightest channel keeps the average maximum. One colour returns itself. Empty -> nullopt.
[[nodiscard]] std::optional<std::uint32_t> mixColors(std::span<std::uint32_t const> argb);

/// Sophisticated Backpacks' colour mixing (SophisticatedCore `ColorHelper.calculateColor`,
/// leather-armour style): no dyes -> `base` unchanged; otherwise the channel average of the dyes
/// plus `base` when `base != defaultColor`, rescaled so the brightest channel equals the average
/// of every colour's brightest channel. Result is opaque (0xFF alpha).
[[nodiscard]] std::uint32_t calculateColor(std::uint32_t base, std::uint32_t defaultColor, std::span<std::uint32_t const> dyes);

/// Which colours a dye at `dyeColumn` of a crafting grid feeds when the backpack is at
/// `backpackColumn` (SB `StorageDyeRecipeBase`): columns at or left of the backpack feed the main
/// colour, columns at or right of it the accent (trim) colour; the backpack's own column feeds both.
struct DyeColumnTarget {
    bool main   = false;
    bool accent = false;
};
[[nodiscard]] constexpr DyeColumnTarget dyeColumnTarget(int dyeColumn, int backpackColumn) {
    return {dyeColumn <= backpackColumn, dyeColumn >= backpackColumn};
}

/// "#RRGGBB"
[[nodiscard]] std::string colorHex(std::uint32_t argb);

/// Which backpack colour a dye in the crafting grid paints.
enum class DyeTarget { Main, Accent };
/// Dyes in the same row as the backpack (left or right of it) colour the main fabric; dyes in
/// another row (above or below it) colour the accent trim. Grid indices are row-major positions
/// in a 3-wide grid.
[[nodiscard]] constexpr DyeTarget dyeTargetFor(int dyeIndex, int backpackIndex) {
    return dyeIndex / 3 == backpackIndex / 3 ? DyeTarget::Main : DyeTarget::Accent;
}

/// How full a backpack is.
struct FillStats {
    int       usedSlots  = 0;
    int       totalSlots = 0;
    long long items      = 0; ///< total item count
    long long capacity   = 0; ///< sum of the per-slot limits (empty slots count as 64-stacks)
    int       percent    = 0; ///< items / capacity, rounded down; at least 1 when not empty

    [[nodiscard]] bool operator==(FillStats const&) const = default;
};
[[nodiscard]] FillStats fillStats(BackpackInventory const& inventory);

} // namespace abp::core
