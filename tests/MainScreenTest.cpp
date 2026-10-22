#include <gtest/gtest.h>

#include "core/screen/MainScreen.h"
#include "core/screen/ScreenModel.h"
#include "core/screen/StorageLayout.h"

using namespace abp::core::screen;

// ---- counts ----------------------------------------------------------------------------------------

TEST(MainScreen, CountOverlayFollowsStorageScreenBase) {
    EXPECT_EQ(countOverlay(1).text, "");
    EXPECT_EQ(countOverlay(0).text, "");
    EXPECT_EQ(countOverlay(64).text, "64");
    EXPECT_FALSE(countOverlay(99).special);
    auto big = countOverlay(1024);
    EXPECT_TRUE(big.special);
    EXPECT_EQ(big.text, "1,024");
    EXPECT_EQ(countOverlay(123456).text, "123k");
    auto over = countOverlay(5, "x", true);
    EXPECT_EQ(over.text, "x");
    EXPECT_TRUE(over.yellow);
}

TEST(MainScreen, CountScaleFitsSixteenUnits) {
    EXPECT_FLOAT_EQ(countScale(12), 1.f);
    EXPECT_FLOAT_EQ(countScale(16), 1.f);
    EXPECT_FLOAT_EQ(countScale(32), 0.5f);
    EXPECT_FLOAT_EQ(countScale(0), 1.f);
}

TEST(MainScreen, QuickCraftPreviewCapsInYellow) {
    // 30 items over 3 slots: 10 each.
    EXPECT_EQ(quickCraftPlaceCount(QuickCraft::Split, 30, 64, 3, 64), 10);
    EXPECT_EQ(quickCraftPlaceCount(QuickCraft::One, 30, 64, 3, 64), 1);
    EXPECT_EQ(quickCraftPlaceCount(QuickCraft::Clone, 3, 64, 3, 64), 64);
    EXPECT_EQ(quickCraftPlaceCount(QuickCraft::Split, 200, 64, 1, 64), 64); // capped by the slot
    // A slot holding 60 of max 64 gets 10 more: drawn as 70 with a yellow "64".
    auto d = dragPreview(10, 60, false, 64);
    EXPECT_EQ(d.count, 70);
    EXPECT_TRUE(d.overlay.yellow);
    EXPECT_EQ(d.overlay.text, "64");
    // Empty slots use 64 as the limit (SC quirk), a 1024 slot is not capped.
    EXPECT_FALSE(dragPreview(10, 0, true, 1024).overlay.yellow);
    EXPECT_FALSE(dragPreview(100, 900, false, 1024).overlay.yellow);
    auto big = dragPreview(100, 1000, false, 1024);
    EXPECT_TRUE(big.overlay.yellow && big.overlay.special);
    EXPECT_EQ(big.overlay.text, "1,024");
}

TEST(MainScreen, QuickCraftRemainder) {
    EXPECT_EQ(quickCraftRemainder(QuickCraft::Split, 30, 64, {{0, 64}, {0, 64}, {0, 64}}), 0);
    EXPECT_EQ(quickCraftRemainder(QuickCraft::Split, 31, 64, {{0, 64}, {0, 64}, {0, 64}}), 1);
    EXPECT_EQ(quickCraftRemainder(QuickCraft::Split, 30, 64, {{60, 64}, {0, 64}}), 30 - 4 - 15);
    EXPECT_EQ(quickCraftRemainder(QuickCraft::One, 5, 64, {{0, 64}, {0, 64}}), 3);
}

TEST(MainScreen, CountLineOnlyWhenSlotLimitDiffers) {
    EXPECT_FALSE(showsCountLine(64, 64));
    EXPECT_TRUE(showsCountLine(64, 16));   // ender pearls in a plain backpack
    EXPECT_TRUE(showsCountLine(1024, 64)); // stack upgrade
}

// ---- error message -----------------------------------------------------------------------------------

TEST(MainScreen, WrapsAtSpacesLikeFontSplit) {
    auto w     = [](std::string_view s) { return static_cast<float>(s.size()) * 6.f; };
    auto lines = wrapText("aaaa bbbb cccc", 60, w);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "aaaa bbbb");
    EXPECT_EQ(lines[1], "cccc");
    auto cut = wrapText("abcdefghijklmnop", 30, w);
    ASSERT_EQ(cut.size(), 4u);
    EXPECT_EQ(cut[0], "abcde");
    EXPECT_EQ(wrapText("short", 260, w).size(), 1u);
    EXPECT_EQ(errorTextHeight(1), 8);
    EXPECT_EQ(errorTextHeight(3), 30);
}

// ---- layout ----------------------------------------------------------------------------------------------

TEST(MainScreen, ColumnsTakenKeepRowsAndAddPartArea) {
    // Iron (54 slots, 6 rows) with a tank (2 columns): 42 slots shown in 7 columns, one 36 wide part.
    LayoutInput in;
    in.storageSlots = 54 - 2 * 6;
    in.columnsTaken = 2;
    in.screenHeight = 400;
    auto l          = computeLayout(in);
    EXPECT_EQ(l.rows, 6);
    EXPECT_EQ(l.slotsOnLine, 9);
    EXPECT_EQ(l.storageColumns, 7);
    EXPECT_EQ(l.imageWidth, 176);
    EXPECT_EQ(l.inventoryParts, (Rect{7 + 7 * 18, 17, 36, 6 * 18}));
    // Netherite with a scroll bar puts the parts right of the bar.
    LayoutInput n;
    n.storageSlots = 120 - 2 * 10;
    n.columnsTaken = 2;
    n.screenHeight = 240;
    auto ln        = computeLayout(n);
    EXPECT_EQ(ln.rows, 10);
    EXPECT_EQ(ln.slotsOnLine, 12);
    EXPECT_TRUE(ln.scroll);
    EXPECT_EQ(ln.inventoryParts.x, 7 + 10 * 18 + 6);
}

TEST(MainScreen, SortButtonPositions) {
    LayoutInput in;
    in.storageSlots = 27;
    in.upgradeSlots = 1;
    auto title      = computeLayout(in);
    EXPECT_EQ(title.sortButton, (Rect{176 - 31, 4, 12, 12}));
    EXPECT_EQ(title.sortByButton, (Rect{176 - 19, 4, 12, 12}));
    EXPECT_EQ(title.searchBox.w, 176 - 31 - 1 - 7);
    in.sortButtons = SortButtonsPosition::BelowUpgrades;
    auto below     = computeLayout(in);
    EXPECT_EQ(below.sortButton, (Rect{-21 - 2, 6 + 16 + 6 + 2, 12, 12}));
    EXPECT_EQ(below.searchBox.w, 176 - 7 - 7);
    in.sortButtons = SortButtonsPosition::BelowUpgradeTabs;
    in.tabsHeight  = 2 * 25 - 1;
    auto tabs      = computeLayout(in);
    EXPECT_EQ(tabs.sortButton, (Rect{176 + 2, 4 + 49 + 2, 12, 12}));
    in.sortButtons = SortButtonsPosition::Hidden;
    auto hidden    = computeLayout(in);
    EXPECT_EQ(hidden.sortButton.w, 0);
    EXPECT_EQ(hidden.searchBox.w, 176 - 14);
}

TEST(MainScreen, ModelCarriesMainScreenFields) {
    BackpackScreenModel m;
    m.storageSlots = 2;
    m.slots.resize(2);
    m.slotLimits          = {0, 0};
    m.baseSlotLimit       = 1024;
    m.inaccessible        = {false, true};
    auto back = BackpackScreenModel::fromJson(m.toJson());
    EXPECT_EQ(back.baseSlotLimit, 1024);
    EXPECT_EQ(back.inaccessible, (std::vector<bool>{false, true}));
    auto changed          = back;
    changed.baseSlotLimit = 64;
    changed.inaccessible  = {};
    auto patch            = makePatch(back, changed);
    ASSERT_TRUE(applyPatch(back, patch));
    EXPECT_EQ(back.baseSlotLimit, 64);
    EXPECT_TRUE(back.inaccessible.empty());
}
