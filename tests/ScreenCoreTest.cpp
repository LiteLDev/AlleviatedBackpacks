#include <gtest/gtest.h>

#include "core/screen/CountFormat.h"
#include "core/screen/ScreenModel.h"
#include "core/screen/SearchFilter.h"
#include "core/screen/StorageLayout.h"

using namespace abp::core;
using namespace abp::core::screen;

// ---- count abbreviation (Sophisticated Core CountAbbreviator) ------------------------------------

TEST(CountFormat, SmallCountsAreGrouped) {
    EXPECT_EQ(abbreviateCount(0), "0");
    EXPECT_EQ(abbreviateCount(7), "7");
    EXPECT_EQ(abbreviateCount(999), "999");
    EXPECT_EQ(abbreviateCount(1234), "1,234");
    EXPECT_EQ(abbreviateCount(9999), "9,999");
}

TEST(CountFormat, LargeCountsGetSuffixes) {
    EXPECT_EQ(abbreviateCount(12345), "12.3k");
    EXPECT_EQ(abbreviateCount(99999), "99.9k"); // rounded down
    EXPECT_EQ(abbreviateCount(123456), "123k");
    EXPECT_EQ(abbreviateCount(1234567), "1.23m");
    EXPECT_EQ(abbreviateCount(12345678), "12.3m");
    EXPECT_EQ(abbreviateCount(123456789), "123m");
    EXPECT_EQ(abbreviateCount(1234567890), "1.23b");
    EXPECT_EQ(abbreviateCount(2147483647), "2.14b");
    EXPECT_EQ(abbreviateCount(10000), "10.0k");
}

TEST(CountFormat, GroupedAndMultiplier) {
    EXPECT_EQ(groupedCount(1234567), "1,234,567");
    EXPECT_EQ(groupedCount(-1000), "-1,000");
    EXPECT_EQ(formatMultiplier(2), "2");
    EXPECT_EQ(formatMultiplier(1.5), "1.5");
    EXPECT_EQ(formatMultiplier(0.125), "0.125");
    EXPECT_EQ(formatMultiplier(1.0 / 3.0), "0.333");
    EXPECT_EQ(formatMultiplier(0.0625), "0.062"); // half-even like DecimalFormat
}

// ---- layout (StorageScreenBase) -------------------------------------------------------------------

TEST(StorageLayout, LeatherBackpackIsChestSized) {
    LayoutInput in;
    in.storageSlots    = 27;
    in.upgradeSlots    = 1;
    in.screenHeight    = 300;
    in.upgradeSwitches = {true};
    auto l             = computeLayout(in);
    EXPECT_EQ(l.slotsOnLine, 9);
    EXPECT_EQ(l.rows, 3);
    EXPECT_FALSE(l.scroll);
    EXPECT_EQ(l.imageWidth, 176);
    EXPECT_EQ(l.imageHeight, 114 + 3 * 18);
    EXPECT_EQ(l.inventoryLabelX, 8);
    EXPECT_EQ(l.inventoryLabelY, l.imageHeight - 94);
    // Main inventory row 1 starts 12 below the label, hotbar 4 below the third row.
    EXPECT_EQ(l.playerSlots[9], (Point{8, l.inventoryLabelY + 12}));
    EXPECT_EQ(l.playerSlots[35], (Point{8 + 8 * 18, l.inventoryLabelY + 12 + 36}));
    EXPECT_EQ(l.playerSlots[0], (Point{8, l.inventoryLabelY + 12 + 54 + 4}));
    EXPECT_EQ(l.upgradeSlots.at(0), (Point{-15, 6}));
    EXPECT_EQ(l.upgradeSwitches.at(0), (Rect{-22, 8, 6, 12}));
    EXPECT_EQ(l.upgradeColumn, (Rect{-21, 0, 26, 6 + 16 + 6}));
    EXPECT_EQ(l.sortButton, (Rect{176 - 31, 4, 12, 12}));
    EXPECT_EQ(l.sortByButton, (Rect{176 - 19, 4, 12, 12}));
    EXPECT_EQ(l.searchBox, (Rect{7, 5, 176 - 32 - 7, 10}));
    EXPECT_EQ(l.transferToStorage, (Rect{8 + 137, l.inventoryLabelY - 2, 12, 12}));
    EXPECT_EQ(l.transferToInventory, (Rect{8 + 149, l.inventoryLabelY - 2, 12, 12}));
    EXPECT_EQ(l.tabsOrigin, (Point{176, 4}));
}

TEST(StorageLayout, NetheriteUsesTwelveColumnsAndScrollsWhenShort) {
    LayoutInput in;
    in.storageSlots = 120;
    in.upgradeSlots = 7;
    in.screenHeight = 400;
    auto l          = computeLayout(in);
    EXPECT_EQ(l.slotsOnLine, 12);
    EXPECT_EQ(l.rows, 10);
    EXPECT_FALSE(l.scroll);
    EXPECT_EQ(l.imageWidth, 230);
    EXPECT_EQ(l.imageHeight, 114 + 180);
    EXPECT_EQ(l.playerInventoryXOffset, 27);
    EXPECT_EQ(l.inventoryLabelX, 35);

    in.screenHeight = 240; // (240 - 114) / 18 = 7 rows fit
    l               = computeLayout(in);
    EXPECT_TRUE(l.scroll);
    EXPECT_EQ(l.visibleRows, 7);
    EXPECT_EQ(l.imageWidth, 236);
    EXPECT_EQ(l.playerInventoryXOffset, 30);
    EXPECT_EQ(l.imageHeight, 114 + 7 * 18);
    EXPECT_EQ(l.scrollPanel, (Rect{7, 17, 12 * 18 + 6, 7 * 18}));
    EXPECT_EQ(l.scrollBar(), (Rect{7 + 12 * 18, 17, 6, 7 * 18}));
}

TEST(StorageLayout, GoldIsNineByNine) {
    LayoutInput in;
    in.storageSlots = 81;
    in.screenHeight = 400;
    auto l          = computeLayout(in);
    EXPECT_EQ(l.slotsOnLine, 9);
    EXPECT_EQ(l.rows, 9);
    EXPECT_EQ(storageRows(108), 9);
    EXPECT_EQ(storageRows(54), 6);
    EXPECT_EQ(storageRows(45), 5);
}

TEST(StorageLayout, ScrollingMovesWholeRows) {
    LayoutInput in;
    in.storageSlots = 120;
    in.screenHeight = 240;
    auto l          = computeLayout(in);
    ScrollState s;
    EXPECT_EQ(scrollMax(l, 120), 10 * 18 - 7 * 18);
    scrollBy(s, l, 120, -1); // wheel down one notch
    EXPECT_EQ(s.rowOffset(), 1);
    scrollBy(s, l, 120, -10);
    EXPECT_EQ(s.rowOffset(), 3); // clamped at max (3 rows)
    scrollBy(s, l, 120, 10);
    EXPECT_EQ(s.distance, 0);

    int  visible = 0;
    auto placed  = placeStorageSlots(l, 120, {}, s, &visible);
    EXPECT_EQ(visible, 7 * 12);
    EXPECT_EQ(placed.front().index, 0);
    EXPECT_EQ(placed.front().pos, (Point{8, 18}));
    scrollBy(s, l, 120, -1);
    placed = placeStorageSlots(l, 120, {}, s, &visible);
    EXPECT_EQ(placed.front().index, 12);
    EXPECT_EQ(placed.front().pos, (Point{8, 18}));
    EXPECT_EQ(placed.back().index, 12 + 7 * 12 - 1);

    auto thumb = scrollThumb(l, 120, ScrollState{});
    EXPECT_EQ(thumb.y, 17);
    EXPECT_EQ(thumb.h, 7 * 18 * 7 * 18 / (10 * 18));
}

TEST(StorageLayout, SearchReflowsMatchingSlots) {
    LayoutInput in;
    in.storageSlots = 27;
    in.screenHeight = 300;
    auto              l = computeLayout(in);
    std::vector<bool> matches(27, false);
    matches[5]  = true;
    matches[20] = true;
    int  visible = 0;
    auto placed  = placeStorageSlots(l, 27, matches, ScrollState{}, &visible);
    ASSERT_EQ(placed.size(), 2u);
    EXPECT_EQ(visible, 2);
    EXPECT_EQ(placed[0].index, 5);
    EXPECT_EQ(placed[0].pos, (Point{8, 18}));
    EXPECT_EQ(placed[1].index, 20);
    EXPECT_EQ(placed[1].pos, (Point{26, 18}));
}

// ---- search filter ---------------------------------------------------------------------------------

TEST(SearchFilter, TermsModsAndTooltips) {
    SearchItem diamond{false, "Diamond Sword", "minecraft", {"Diamond Sword", "§9+7 Attack Damage"}};
    SearchItem pack{false, "§eNetherite Backpack", "alleviatedbackpacks", {"Netherite Backpack"}};
    SearchItem empty;

    EXPECT_TRUE(SearchFilter("").matches(empty));
    EXPECT_TRUE(SearchFilter("   ").matches(diamond));
    EXPECT_FALSE(SearchFilter("dia").matches(empty));
    EXPECT_TRUE(SearchFilter("dia").matches(diamond));
    EXPECT_TRUE(SearchFilter("SWORD").matches(diamond));
    EXPECT_FALSE(SearchFilter("sword pack").matches(diamond));
    EXPECT_TRUE(SearchFilter("@mine").matches(diamond));
    EXPECT_FALSE(SearchFilter("@mine").matches(pack));
    EXPECT_TRUE(SearchFilter("@alleviated net").matches(pack));
    EXPECT_TRUE(SearchFilter("@").matches(pack));
    EXPECT_TRUE(SearchFilter("#attack").matches(diamond));
    EXPECT_FALSE(SearchFilter("#attack").matches(pack));
    EXPECT_TRUE(SearchFilter("#attack").needsTooltip());
    EXPECT_FALSE(SearchFilter("sword").needsTooltip());
    EXPECT_TRUE(SearchFilter("netherite").matches(pack)); // formatting codes ignored
    EXPECT_TRUE(SearchFilter("diamond  sword").matches(diamond)); // empty term matches
}

// ---- models ------------------------------------------------------------------------------------------

namespace {
BackpackScreenModel sampleModel() {
    BackpackScreenModel m;
    m.session      = 7;
    m.containerId  = 3;
    m.uuid         = "u";
    m.title        = "Backpack";
    m.tier         = 5;
    m.storageSlots = 4;
    m.slots        = {ItemStackData::of("minecraft:stone", 300), {}, ItemStackData::of("minecraft:dirt", 5), {}};
    m.slotLimits   = {512, 0, 512, 0};
    UpgradeView u;
    u.item       = ItemStackData::of("alleviatedbackpacks:pickup_upgrade", 1, 1);
    u.id         = "pickup";
    u.name       = "Pickup Upgrade";
    u.toggleable = true;
    m.upgrades   = {u, UpgradeView{}};
    m.memory     = {{}, ItemStackData::of("minecraft:dirt", 1), {}, {}};
    m.noSort     = {false, false, true, false};
    m.sortBy     = "count";
    return m;
}
} // namespace

TEST(ScreenModel, RoundTripsThroughJson) {
    auto m    = sampleModel();
    auto back = BackpackScreenModel::fromJson(nlohmann::json::parse(m.toJson().dump()));
    EXPECT_EQ(back.session, 7u);
    EXPECT_EQ(back.containerId, 3);
    EXPECT_EQ(back.storageSlots, 4);
    EXPECT_EQ(back.slots[0].count, 300);
    EXPECT_TRUE(back.slots[1].empty());
    EXPECT_EQ(back.slotLimits[0], 512);
    ASSERT_EQ(back.upgrades.size(), 2u);
    EXPECT_EQ(back.upgrades[0].id, "pickup");
    EXPECT_TRUE(back.upgrades[0].toggleable);
    EXPECT_TRUE(back.upgrades[1].item.empty());
    EXPECT_EQ(back.memory[1].id, "minecraft:dirt");
    EXPECT_TRUE(back.noSort[2]);
    EXPECT_EQ(back.sortBy, "count");
}

TEST(ScreenModel, PatchCarriesOnlyChanges) {
    auto a = sampleModel();
    auto b = a;
    EXPECT_TRUE(makePatch(a, b).empty());

    b.slots[1] = ItemStackData::of("minecraft:gold_ingot", 64);
    b.slots[0] = {};
    b.upgrades[0].enabled = false;
    b.sortBy              = "mod";
    b.errorMessage        = "Can't remove";
    b.errorStorageSlots   = {2};
    auto patch            = makePatch(a, b);
    EXPECT_TRUE(patch.contains("slotsDiff"));
    EXPECT_EQ(patch["slotsDiff"].size(), 2u);
    EXPECT_FALSE(patch.contains("memory"));

    auto c = a;
    ASSERT_TRUE(applyPatch(c, nlohmann::json::parse(patch.dump())));
    EXPECT_TRUE(c.slots[0].empty());
    EXPECT_EQ(c.slots[1].id, "minecraft:gold_ingot");
    EXPECT_FALSE(c.upgrades[0].enabled);
    EXPECT_EQ(c.sortBy, "mod");
    EXPECT_EQ(c.errorMessage, "Can't remove");
    EXPECT_EQ(c.errorStorageSlots, std::vector<int>{2});
    EXPECT_TRUE(makePatch(c, b).empty());
}

TEST(ScreenModel, PatchHandlesResize) {
    auto a         = sampleModel();
    auto b         = a;
    b.storageSlots = 6;
    b.slots.resize(6);
    b.slotLimits.resize(6);
    b.slots[5] = ItemStackData::of("minecraft:apple", 2);
    auto c     = a;
    ASSERT_TRUE(applyPatch(c, makePatch(a, b)));
    EXPECT_EQ(c.storageSlots, 6);
    EXPECT_EQ(c.slots[5].id, "minecraft:apple");
    EXPECT_FALSE(applyPatch(c, nlohmann::json::parse(R"({"slotsDiff":{"99":null}})")));
}

TEST(ScreenModel, ContentsAndRenderInfoRoundTrip) {
    ContentsModel cm;
    cm.uuid  = "x";
    cm.known = true;
    cm.lines = {"§aStack Size Multiplier: §f2"};
    cm.upgrades.push_back({ItemStackData::of("alleviatedbackpacks:magnet_upgrade", 1, 1), true, false});
    cm.items  = {ItemStackData::of("minecraft:cobblestone", 1000)};
    auto back = ContentsModel::fromJson(cm.toJson());
    EXPECT_TRUE(back.known);
    EXPECT_EQ(back.lines, cm.lines);
    ASSERT_EQ(back.upgrades.size(), 1u);
    EXPECT_FALSE(back.upgrades[0].enabled);
    ASSERT_EQ(back.items.size(), 1u);
    EXPECT_EQ(back.items[0].count, 1000);

    RenderInfo ri;
    ri.uuid      = "x";
    ri.tier      = 3;
    ri.mainColor = 0xFF112233;
    ri.upgrades  = {"magnet"};
    EXPECT_EQ(RenderInfo::fromJson(ri.toJson()), ri);
}

TEST(ScreenModel, CompactionSumsAndSortsByCount) {
    BackpackInventory inv(6);
    inv.set(0, ItemStackData::of("minecraft:dirt", 10));
    inv.set(1, ItemStackData::of("minecraft:stone", 64));
    inv.set(2, ItemStackData::of("minecraft:dirt", 60));
    inv.set(4, ItemStackData::of("minecraft:apple", 64));
    auto out = compactSortedByCount(inv);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].id, "minecraft:dirt");
    EXPECT_EQ(out[0].count, 70);
    EXPECT_EQ(out[1].id, "minecraft:stone"); // ties keep inventory order
    EXPECT_EQ(out[2].id, "minecraft:apple");
}
