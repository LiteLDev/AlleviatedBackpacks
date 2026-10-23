#pragma once

// GX-3: how Sophisticated Backpacks' item tooltips wrap. SC splits the `.tooltip` text only at
// `\n` (TranslationHelper.getTranslatedLines); the wrapping of long lines is NeoForge's
// `ClientHooks.gatherTooltipComponents`, which every Java tooltip goes through:
//
//   tooltipTextWidth = widest line
//   tooltipX = mouseX + 12
//   if (tooltipX + tooltipTextWidth + 4 > screenWidth) {
//       tooltipX = mouseX - 16 - tooltipTextWidth          // flip to the left of the cursor
//       if (tooltipX < 4) {                               // fits on neither side: wrap
//           tooltipTextWidth = mouseX > screenWidth / 2 ? mouseX - 12 - 8 : screenWidth - 16 - mouseX
//           needsWrap = true
//       }
//   }
//
// and the lines are then split at word boundaries (Font.split / StringSplitter.splitLines),
// keeping the line's formatting on its continuation lines.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace abp::core::screen {

/// NeoForge's wrap width for a tooltip `textWidth` wide with the cursor at `mouseX` on a screen
/// `screenWidth` wide (GUI units); nullopt when the tooltip fits on one side and is not wrapped.
[[nodiscard]] std::optional<int> tooltipWrapWidth(int mouseX, int screenWidth, int textWidth);

/// Width of a text in GUI units (formatting codes `§x` count as 0).
using TextWidthFn = std::function<int(std::string_view)>;

/// The default Minecraft font's advance widths (ascii.png glyph widths + 1: most characters 6,
/// space 4, `i` / `!` / `.` 2, ...; other scripts 6, CJK 9), formatting codes skipped. What Java's
/// `Font.width` gives for SB's tooltip texts, without asking the game's font from outside a
/// render pass.
[[nodiscard]] int vanillaTextWidth(std::string_view text);

/// Splits `line` into lines at most `maxWidth` wide, at spaces (a single word wider than that is
/// cut), like StringSplitter.splitLines. The space at a break is dropped; the formatting codes in
/// effect at a break are repeated at the start of the next line. UTF-8.
[[nodiscard]] std::vector<std::string> wrapTooltipLine(std::string const& line, int maxWidth, TextWidthFn const& width);

} // namespace abp::core::screen
