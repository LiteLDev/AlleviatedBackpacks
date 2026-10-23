#include <gtest/gtest.h>

#include "core/screen/TooltipWrap.h"

using namespace abp::core::screen;

namespace {
/// 6 units per character (like the default font's average), formatting codes cost nothing.
int width(std::string_view s) {
    int w = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (s.compare(i, 2, "\xC2\xA7") == 0 && i + 2 < s.size()) {
            i += 3;
            continue;
        }
        auto c = static_cast<unsigned char>(s[i]);
        i += c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        w += 6;
    }
    return w;
}
} // namespace

TEST(TooltipWrap, NeoForgeWrapWidth) {
    // Fits right of the cursor: no wrap.
    EXPECT_FALSE(tooltipWrapWidth(100, 480, 200).has_value());
    // Too wide on the right but fits flipped to the left: no wrap.
    EXPECT_FALSE(tooltipWrapWidth(400, 480, 300).has_value());
    // Fits on neither side: the wider side's width.
    EXPECT_EQ(tooltipWrapWidth(300, 480, 400), 300 - 12 - 8); // cursor on the right half
    EXPECT_EQ(tooltipWrapWidth(100, 480, 400), 480 - 16 - 100); // left half
    EXPECT_EQ(tooltipWrapWidth(240, 480, 400), 480 - 16 - 240); // exactly half: the right side
}

TEST(TooltipWrap, SplitsAtWordsAndKeepsFormatting) {
    std::string line  = "\xC2\xA7" "8Automatically swaps item in player's hand";
    auto        lines = wrapTooltipLine(line, 6 * 20, width);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "\xC2\xA7" "8Automatically swaps");
    EXPECT_EQ(lines[1], "\xC2\xA7" "8item in player's"); // the colour goes on
    EXPECT_EQ(lines[2], "\xC2\xA7" "8hand");
    for (auto const& l : lines) EXPECT_LE(width(l), 6 * 20);
}

TEST(TooltipWrap, VanillaFontWidths) {
    EXPECT_EQ(vanillaTextWidth("Hello"), 6 + 6 + 3 + 3 + 6);
    EXPECT_EQ(vanillaTextWidth("\xC2\xA7" "8a b"), 6 + 4 + 6); // formatting codes cost nothing
    EXPECT_EQ(vanillaTextWidth("!.,i"), 2 + 2 + 2 + 2);
    EXPECT_EQ(vanillaTextWidth("\xE4\xB8\xAD\xE6\x96\x87"), 9 + 9); // CJK
    EXPECT_EQ(vanillaTextWidth(""), 0);
}

TEST(TooltipWrap, LongWordIsCutAndShortLinesStay) {
    EXPECT_EQ(wrapTooltipLine("short", 60, width), (std::vector<std::string>{"short"}));
    auto lines = wrapTooltipLine("abcdefghij", 6 * 4, width);
    EXPECT_EQ(lines, (std::vector<std::string>{"abcd", "efgh", "ij"}));
    // A reset in the middle: the next line starts plain.
    auto reset = wrapTooltipLine("\xC2\xA7" "aaa \xC2\xA7" "rbbb ccc", 6 * 7, width);
    EXPECT_EQ(reset, (std::vector<std::string>{"\xC2\xA7" "aaa \xC2\xA7" "rbbb", "ccc"}));
}
