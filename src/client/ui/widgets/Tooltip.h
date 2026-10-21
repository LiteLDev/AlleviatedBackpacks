#pragma once

#include <functional>
#include <string>
#include <vector>

#include "client/ui/gfx/Painter.h"

class ItemStack;

namespace abp::client::ui {

/// One tooltip line (may contain `§` formatting codes).
struct TooltipLine {
    std::string   text;
    std::uint32_t rgb = 0xFFFFFF;
};

/// Optional custom part below the text lines (e.g. the backpack contents grid).
struct TooltipImage {
    float                                                  width  = 0;
    float                                                  height = 0;
    std::function<void(Painter&, float left, float top)> draw;
};

struct TooltipStyle {
    Color background = Color::argb(0xF0100010);
    Color borderTop  = Color::argb(0x505000FF);
    Color borderBottom = Color::argb(0x5028007F);
};

/// Draws a vanilla-style tooltip next to (x, y), kept on screen. Flushes the painter first so it
/// is on top of everything drawn before. A title gap follows the first line (when `titleGap`).
void drawTooltip(
    Painter&                        painter,
    std::vector<TooltipLine> const& lines,
    float                           x,
    float                           y,
    TooltipImage const*             image    = nullptr,
    TooltipStyle const&             style    = {},
    bool                            titleGap = true
);

/// Vanilla tooltip lines of an item stack (name, enchantments, lore, ...).
[[nodiscard]] std::vector<TooltipLine> itemTooltipLines(ItemStack const& stack);

} // namespace abp::client::ui
