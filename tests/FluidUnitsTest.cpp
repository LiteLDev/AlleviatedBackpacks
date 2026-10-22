// P2-6 (Tank, Battery, Pump, XP Pump): inventory columns taken by upgrade inventory parts, SB
// sizing helpers and the screen layout with columns taken.

#include <gtest/gtest.h>

#include "core/BackpackData.h"
#include "core/Capacity.h"
#include "core/ColumnsFit.h"
#include "core/screen/StorageLayout.h"

using namespace abp::core;

TEST(InventoryColumns, SlotsAfterColumnsUseBaseRows) {
    using capacity::slotsAfterColumns;
    EXPECT_EQ(slotsAfterColumns(27, 0), 27);
    EXPECT_EQ(slotsAfterColumns(27, 2), 21);  // leather: 3 rows
    EXPECT_EQ(slotsAfterColumns(45, 2), 35);  // copper: 5 rows
    EXPECT_EQ(slotsAfterColumns(54, 4), 30);  // iron: 6 rows, two tanks
    EXPECT_EQ(slotsAfterColumns(81, 2), 63);  // gold: 9 rows
    EXPECT_EQ(slotsAfterColumns(108, 6), 54); // diamond: 9 rows of 12
    EXPECT_EQ(slotsAfterColumns(120, 2), 100); // netherite: 10 rows of 12
}

TEST(InventoryColumns, BaseSlotsForIsTheInverse) {
    using capacity::baseSlotsFor;
    using capacity::slotsAfterColumns;
    for (int base : {27, 45, 54, 81, 108, 120}) {
        for (int c : {0, 2, 4, 6}) {
            EXPECT_EQ(baseSlotsFor(slotsAfterColumns(base, c), c), base) << base << " slots, " << c << " columns";
        }
    }
}

TEST(InventoryColumns, RequiredMultiplierFormat) {
    using capacity::formatRequiredMultiplier;
    EXPECT_EQ(formatRequiredMultiplier(1.0), "1");
    EXPECT_EQ(formatRequiredMultiplier(1.01), "1.1");
    EXPECT_EQ(formatRequiredMultiplier(1.5), "1.5");
    EXPECT_EQ(formatRequiredMultiplier(1.96), "2");
    EXPECT_EQ(formatRequiredMultiplier(4.0 / 3.0), "1.4");
}

TEST(InventoryColumns, PumpMaxInOutTruncatesTheMultiplier) {
    using capacity::pumpMaxInOut;
    EXPECT_EQ(pumpMaxInOut(3, 1), 1000);     // 60 -> at least a bucket
    EXPECT_EQ(pumpMaxInOut(10, 8), 1600);    // 20 * 10 * 8
    EXPECT_EQ(pumpMaxInOut(10, 8, 20, 0.5), 1000); // 1 + (int)3.5 = 4 -> 800 -> 1000
    EXPECT_EQ(pumpMaxInOut(10, 16, 20, 0.5), 1600); // 1 + (int)7.5 = 8
}

TEST(InventoryColumns, BackpackDataPersistsColumnsTaken) {
    auto d = BackpackData::create("u", Tier::Iron);
    EXPECT_EQ(d.baseStorageSlots(), 54);
    d.columnsTaken = 2;
    d.inventory.resize(capacity::slotsAfterColumns(54, 2));
    EXPECT_EQ(d.inventory.size(), 42);
    EXPECT_EQ(d.baseStorageSlots(), 54);
    auto back = BackpackData::fromJson(d.toJson());
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->columnsTaken, 2);
    EXPECT_EQ(back->inventory.size(), 42); // setTier on load keeps the taken columns
    // Without columns the key is not written (old data stays byte-identical).
    EXPECT_FALSE(BackpackData::create("v", Tier::Iron).toJson().contains("columnsTaken"));
    // A tier upgrade grows to the new tier's slots minus the taken columns.
    back->setTier(Tier::Gold);
    EXPECT_EQ(back->inventory.size(), 63);
    EXPECT_EQ(back->baseStorageSlots(), 81);
}

TEST(InventoryColumns, FitKeepsSlotsBelowTheTarget) {
    // 27 slots -> 21: items at 0, 5 stay; 22 and 26 go after the last kept item.
    std::vector<bool> occ(27, false);
    occ[0] = occ[5] = occ[22] = occ[26] = true;
    auto r = columns::fit(occ, {}, 21);
    ASSERT_TRUE(r.fits);
    EXPECT_EQ(r.target[0], 0);
    EXPECT_EQ(r.target[5], 5);
    EXPECT_EQ(r.target[22], 6);
    EXPECT_EQ(r.target[26], 7);
    EXPECT_EQ(r.target[1], -1);
}

TEST(InventoryColumns, FitCompactsWhenNeeded) {
    // Items in 0..19 and 26 with slot 20 the only free one after them, plus a hole at 3.
    std::vector<bool> occ(27, false);
    for (int i = 0; i < 21; ++i) occ[static_cast<std::size_t>(i)] = i != 3 && i != 20;
    occ[25] = occ[26] = true;
    auto r = columns::fit(occ, {}, 21);
    ASSERT_TRUE(r.fits); // 21 items, 21 slots: compacted
    EXPECT_EQ(r.target[0], 0);
    EXPECT_EQ(r.target[4], 3);
    std::vector<bool> taken(21, false);
    for (std::size_t i = 0; i < occ.size(); ++i) {
        if (!occ[i]) continue;
        ASSERT_GE(r.target[i], 0);
        EXPECT_FALSE(taken[static_cast<std::size_t>(r.target[i])]);
        taken[static_cast<std::size_t>(r.target[i])] = true;
    }
}

TEST(InventoryColumns, FitFailsWhenFullAndRespectsFixedSlots) {
    std::vector<bool> occ(27, true);
    auto              r = columns::fit(occ, {}, 21);
    EXPECT_FALSE(r.fits);
    EXPECT_EQ(r.errorSlots.size(), 6u);
    // A fixed (memory) item beyond the new size can't move.
    std::vector<bool> few(27, false);
    few[24] = true;
    std::vector<bool> fixed(27, false);
    fixed[24] = true;
    auto f    = columns::fit(few, fixed, 21);
    EXPECT_FALSE(f.fits);
    ASSERT_EQ(f.errorSlots.size(), 1u);
    EXPECT_EQ(f.errorSlots[0], 24);
}

TEST(InventoryColumns, FitSkipsBlockedEmptySlots) {
    // Slot 1 is an empty memory slot: the item from slot 25 goes to slot 2 instead.
    std::vector<bool> occ(27, false);
    occ[0] = occ[25] = true;
    std::vector<bool> blocked(27, false);
    blocked[1] = true;
    auto r     = columns::fit(occ, {}, 21, blocked);
    ASSERT_TRUE(r.fits);
    EXPECT_EQ(r.target[25], 2);
}

TEST(InventoryColumns, LayoutWithColumnsTaken) {
    screen::LayoutInput in;
    in.storageSlots = 21; // leather with a tank
    in.columnsTaken = 2;
    in.upgradeSlots = 1;
    auto l          = screen::computeLayout(in);
    EXPECT_EQ(l.rows, 3);
    EXPECT_EQ(l.slotsOnLine, 9);
    EXPECT_EQ(l.storageColumns, 7);
    EXPECT_EQ(l.imageWidth, 176);
    EXPECT_EQ(l.inventoryParts.x, 7 + 7 * 18);
    EXPECT_EQ(l.inventoryParts.y, 17);
    EXPECT_EQ(l.inventoryParts.w, 36);
    EXPECT_EQ(l.inventoryParts.h, 54);

    in.storageSlots = 54; // diamond with two tanks and a battery
    in.columnsTaken = 6;
    in.upgradeSlots = 5;
    in.screenHeight = 400;
    l               = screen::computeLayout(in);
    EXPECT_EQ(l.rows, 9);
    EXPECT_EQ(l.slotsOnLine, 12);
    EXPECT_EQ(l.storageColumns, 6);
    EXPECT_EQ(l.inventoryParts.w, 108);
    EXPECT_FALSE(l.scroll);

    in.screenHeight = 240; // not all rows fit: the parts move right of the scroll bar
    l               = screen::computeLayout(in);
    EXPECT_TRUE(l.scroll);
    EXPECT_EQ(l.inventoryParts.x, 7 + 6 * 18 + 6);
    EXPECT_EQ(l.inventoryParts.h, l.visibleRows * 18);
}
