#include <gtest/gtest.h>

#include "core/BackpackInventory.h"
#include "core/Sorting.h"
#include "upgrade/mobcatcher/MobCatcherLogic.h"

using namespace abp::upgrade::mobcatcher;
using abp::core::AccessContext;
using abp::core::BackpackInventory;
using abp::core::InsertVerdict;
using abp::core::ItemStackData;
using abp::core::Source;

namespace {
ItemStackData item(std::string id, int count, int max = 64) {
    ItemStackData d;
    d.id       = std::move(id);
    d.count    = count;
    d.maxStack = max;
    return d;
}

CapturedMob mob(int slot, int width, int height, int cost = 1, bool hostile = false) {
    CapturedMob m;
    m.id         = "00000000-0000-0000-0000-000000000001";
    m.entityType = "minecraft:cow";
    m.slot       = slot;
    m.width      = width;
    m.height     = height;
    m.slotCost   = cost;
    m.hostile    = hostile;
    return m;
}
} // namespace

// ---- core: slot blockers -------------------------------------------------------------------------

TEST(SlotBlocker, BlockedSlotsRefuseInsertsFromEverySource) {
    BackpackInventory inv(4);
    inv.addSlotBlocker("t", [](int s) { return s == 0 || s == 1; });
    EXPECT_TRUE(inv.isSlotBlocked(0));
    EXPECT_FALSE(inv.isSlotBlocked(2));
    EXPECT_FALSE(inv.isSlotBlocked(-1));
    EXPECT_EQ(inv.checkInsert(0, item("a", 1), AccessContext{Source::Internal}), InsertVerdict::Deny);
    EXPECT_EQ(inv.checkInsert(-1, item("a", 1), AccessContext{Source::Player}), InsertVerdict::Allow);
    auto r = inv.insertInto(1, item("a", 5), AccessContext{Source::Player});
    EXPECT_EQ(r.inserted, 0);
    r = inv.insert(item("a", 100), AccessContext{Source::Pickup});
    EXPECT_EQ(r.inserted, 100);
    EXPECT_TRUE(inv.at(0).empty());
    EXPECT_TRUE(inv.at(1).empty());
    EXPECT_EQ(inv.at(2).count, 64);
    EXPECT_EQ(inv.at(3).count, 36);
}

TEST(SlotBlocker, RemovedWithItsOwner) {
    BackpackInventory inv(2);
    inv.addSlotBlocker("mob", [](int s) { return s == 0; });
    inv.removeHooks("other");
    EXPECT_TRUE(inv.isSlotBlocked(0));
    inv.removeHooks("mob");
    EXPECT_FALSE(inv.isSlotBlocked(0));
    inv.addSlotBlocker("mob", [](int) { return true; });
    inv.clearFilters();
    EXPECT_FALSE(inv.isSlotBlocked(1));
}

TEST(SlotBlocker, SortingSkipsBlockedSlots) {
    BackpackInventory inv(5);
    inv.set(3, item("b", 1));
    inv.set(4, item("a", 1));
    inv.addSlotBlocker("mob", [](int s) { return s == 0 || s == 1; });
    inv.sort([](ItemStackData const& x, ItemStackData const& y) { return x.id < y.id; });
    EXPECT_TRUE(inv.at(0).empty());
    EXPECT_TRUE(inv.at(1).empty());
    EXPECT_EQ(inv.at(2).id, "a");
    EXPECT_EQ(inv.at(3).id, "b");

    // Whatever a blocked slot holds stays there (both sort paths agree).
    BackpackInventory held(4);
    held.set(1, item("z", 2));
    held.set(3, item("a", 1));
    held.addSlotBlocker("mob", [](int s) { return s == 1; });
    held.sort([](ItemStackData const& x, ItemStackData const& y) { return x.id < y.id; });
    EXPECT_EQ(held.at(0).id, "a");
    EXPECT_EQ(held.at(1).id, "z");
    EXPECT_TRUE(held.at(2).empty());

    BackpackInventory inv2(5);
    inv2.set(3, item("b", 1));
    inv2.set(4, item("a", 1));
    inv2.addSlotBlocker("mob", [](int s) { return s == 1; });
    EXPECT_TRUE(abp::core::sortInventory(inv2, [](ItemStackData const& x, ItemStackData const& y) { return x.id < y.id; }));
    EXPECT_EQ(inv2.at(0).id, "a");
    EXPECT_TRUE(inv2.at(1).empty());
    EXPECT_EQ(inv2.at(2).id, "b");
}

// ---- slot cost and footprint (values from SB's MobCatcherStorageTest) ----------------------------

TEST(MobCatcher, SlotCostUsesHalfMaxPlusHalfCurrentTimesMultiplier) {
    EXPECT_EQ(slotCost(10, 10, 1.0), 10); // cow
    EXPECT_EQ(slotCost(20, 20, 2.0), 40); // zombie, hostile
    EXPECT_EQ(slotCost(20, 3, 2.0), 23);  // hurt zombie: (10 + 1.5) * 2
    EXPECT_EQ(slotCost(4, 1, 1.0), 3);    // chicken: ceil(2.5)
    EXPECT_EQ(slotCost(0, 0, 1.0), 1);    // at least 1
    EXPECT_EQ(slotCost(10, 15, 1.0), 15); // current above max raises the effective max
}

TEST(MobCatcher, FootprintMatchesSbReferenceShapes) {
    EXPECT_EQ(footprintFor(0.6, 1.95, 20), (Footprint{3, 7})); // villager
    EXPECT_EQ(footprintFor(0.9, 1.4, 20), (Footprint{4, 5}));  // cow-sized
    EXPECT_EQ(footprintFor(0.4, 0.7, 4), (Footprint{2, 2}));   // chicken
    EXPECT_EQ(footprintFor(0.6, 1.95, 40), (Footprint{4, 10})); // tall hostile
    EXPECT_GE(footprintFor(0.9, 1.3, 10).area(), 10);
}

TEST(MobCatcher, TargetSlotStaysInsideShrunkGrid) {
    auto m = mob(7, 2, 2);
    EXPECT_EQ(targetSlot(m, 9, 7, 18), 5);
}

TEST(MobCatcher, ColumnsFollowTheBackgroundWidth) {
    EXPECT_EQ(columnsFor(27), 9);
    EXPECT_EQ(columnsFor(81), 9);
    EXPECT_EQ(columnsFor(108), 12);
    EXPECT_EQ(columnsFor(120), 12);
}

// ---- placement --------------------------------------------------------------------------------------

TEST(MobCatcher, OccupiesSlotCoversTheRectangle) {
    auto m = mob(10, 2, 3); // x 1..2, y 1..3 in 9 columns
    EXPECT_TRUE(m.occupiesSlot(10, 9));
    EXPECT_TRUE(m.occupiesSlot(11, 9));
    EXPECT_TRUE(m.occupiesSlot(28, 9));
    EXPECT_FALSE(m.occupiesSlot(12, 9));
    EXPECT_FALSE(m.occupiesSlot(9, 9));
    EXPECT_FALSE(m.occupiesSlot(37, 9));
    EXPECT_EQ(coveredSlots({m}, 9, 27), (std::vector<int>{10, 11, 19, 20}));
}

TEST(MobCatcher, FindsFirstEmptyRectangleRowByRow) {
    BackpackInventory inv(27);
    EXPECT_EQ(findEmptyRectangle(inv, 9, {3, 2}, {}), 0);
    inv.set(1, item("a", 1));
    EXPECT_EQ(findEmptyRectangle(inv, 9, {3, 2}, {}), 2);
    std::vector<CapturedMob> mobs{mob(2, 3, 2)};
    EXPECT_EQ(findEmptyRectangle(inv, 9, {3, 2}, mobs), 5);
    EXPECT_EQ(findEmptyRectangle(inv, 9, {3, 4}, mobs), std::nullopt); // only 3 rows
    inv.addSlotBlocker("x", [](int s) { return s == 5; });
    EXPECT_EQ(findEmptyRectangle(inv, 9, {3, 2}, mobs), 6);
    EXPECT_EQ(findEmptyRectangle(inv, 9, {9, 1}, mobs), 18);
}

TEST(MobCatcher, PartialLastRowIsNotUsable) {
    BackpackInventory inv(30); // 3 full rows + 3 slots
    EXPECT_EQ(findEmptyRectangle(inv, 9, {4, 4}, {}), std::nullopt);
    EXPECT_EQ(findEmptyRectangle(inv, 9, {3, 4}, {}), 0);
}

TEST(MobCatcher, BasicTierFitsOnlyPassiveMobsWithinLimit) {
    EXPECT_TRUE(canFitBasicTier({}, 18));
    EXPECT_TRUE(canFitBasicTier({mob(0, 3, 4, 10)}, 18));
    EXPECT_FALSE(canFitBasicTier({mob(0, 3, 4, 10, true)}, 18));
    EXPECT_FALSE(canFitBasicTier({mob(0, 5, 4, 20)}, 18));
}

TEST(MobCatcher, RecordRoundTripsAndDropsInvalidEntries) {
    auto m          = mob(4, 3, 4, 10);
    m.nbt           = "AAAA";
    m.displayName   = "Bessie";
    m.currentHealth = 7;
    m.maxHealth     = 10;
    m.look["variant"] = 2;
    auto arr = mobsToJson({m, mob(1, 1, 1)});
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_EQ(arr[0]["slot"], 1); // sorted by slot
    auto back = parseMobs(arr);
    ASSERT_EQ(back.size(), 2u);
    EXPECT_EQ(back[1].nbt, "AAAA");
    EXPECT_EQ(back[1].displayName, "Bessie");
    EXPECT_EQ(back[1].look.value("variant", 0), 2);
    EXPECT_FALSE(mobsToJson({m}, false)[0].contains("nbt"));
    arr.push_back(nlohmann::json{{"id", "x"}}); // no type
    arr.push_back(5);
    EXPECT_EQ(parseMobs(arr).size(), 2u);
}

// ---- tooltip hearts ----------------------------------------------------------------------------------

TEST(MobCatcher, HeartRowInlineUpToTwentyHealth) {
    auto r = heartRow(7, 10);
    EXPECT_FALSE(r.compact);
    EXPECT_EQ(r.inlineHearts, 5);
    EXPECT_EQ(inlineHeart(r, 0), 2);
    EXPECT_EQ(inlineHeart(r, 2), 2);
    EXPECT_EQ(inlineHeart(r, 3), 1);
    EXPECT_EQ(inlineHeart(r, 4), 0);
    EXPECT_FALSE(heartRow(20, 20).compact);
    EXPECT_EQ(heartRow(5, 9).inlineHearts, 5);
}

TEST(MobCatcher, HeartRowCompactAboveTwenty) {
    auto r = heartRow(25, 40);
    EXPECT_TRUE(r.compact);
    EXPECT_EQ(r.full, 12);
    EXPECT_TRUE(r.half);
    EXPECT_EQ(r.empty, 7);
    r = heartRow(100, 30); // clamped
    EXPECT_EQ(r.full, 15);
    EXPECT_FALSE(r.half);
    EXPECT_EQ(r.empty, 0);
}

// ---- preview pose ------------------------------------------------------------------------------------

TEST(MobCatcher, JavaUuidHashMatchesJava) {
    // new UUID(0x0000000100000002L, 0x0000000300000004L).hashCode(): hilo = 0x0000000200000006 -> 2 ^ 6
    EXPECT_EQ(javaUuidHash("00000001-0000-0002-0000-000300000004"), 4);
    // UUID.fromString("123e4567-e89b-12d3-a456-426614174000").hashCode() == 0x4AE455D2
    EXPECT_EQ(javaUuidHash("123e4567-e89b-12d3-a456-426614174000"), 1256478162);
}

TEST(MobCatcher, PreviewScaleFitsTheFootprint) {
    EXPECT_EQ(previewScale(0.9, 1.4, 54, 72), 26); // min(48 / 0.9, 66 / 1.4) * 0.5625 = 26.5
    EXPECT_EQ(previewScale(2.0, 2.0, 18, 18), 8);  // 12 / 2 * 0.5625 = 3.4 -> at least 8
    EXPECT_EQ(previewScale(0.1, 0.1, 18, 18), 27); // boxes below 0.25 count as 0.25
    EXPECT_EQ(idlePoseOffset(4, 100.f, 0, 33.f), 0.f); // rests between moves
}
