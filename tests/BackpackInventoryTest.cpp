#include <gtest/gtest.h>

#include "core/BackpackInventory.h"

using namespace abp::core;

namespace {
ItemStackData stone(int n) { return ItemStackData::of("minecraft:stone", n); }
ItemStackData sword() { return ItemStackData::of("minecraft:iron_sword", 1, 1); }
AccessContext const kPlayer{Source::Player};
AccessContext const kPickup{Source::Pickup};
} // namespace

TEST(BackpackInventory, InsertMergesFirst) {
    BackpackInventory inv(4);
    inv.set(2, stone(60));
    auto r = inv.insert(stone(10), kPlayer);
    EXPECT_TRUE(r.remainder.empty());
    EXPECT_EQ(r.inserted, 10);
    EXPECT_EQ(inv.at(2).count, 64);
    EXPECT_EQ(inv.at(0).count, 6);
    EXPECT_TRUE(inv.at(1).empty());
}

TEST(BackpackInventory, InsertOverflowRemainder) {
    BackpackInventory inv(2);
    auto              r = inv.insert(stone(200), kPlayer);
    EXPECT_EQ(r.inserted, 128);
    EXPECT_EQ(r.remainder.count, 72);
    EXPECT_EQ(inv.at(0).count, 64);
    EXPECT_EQ(inv.at(1).count, 64);
}

TEST(BackpackInventory, SimulateDoesNotChange) {
    BackpackInventory inv(2);
    int               notified = 0;
    inv.addChangeListener("t", [&](int) { ++notified; });
    auto r = inv.insert(stone(10), kPlayer, true);
    EXPECT_EQ(r.inserted, 10);
    EXPECT_TRUE(inv.isEmpty());
    EXPECT_EQ(notified, 0);
    inv.insert(stone(10), kPlayer);
    EXPECT_GT(notified, 0);
    auto e = inv.extract(0, 4, kPlayer, true);
    EXPECT_EQ(e.count, 4);
    EXPECT_EQ(inv.at(0).count, 10);
}

TEST(BackpackInventory, LimitModifierMultiplies) {
    BackpackInventory inv(1);
    inv.addLimitModifier("stack", 0, [](int, ItemStackData const& item, int limit) {
        return item.maxStack > 1 ? limit * 4 : limit;
    });
    EXPECT_EQ(inv.slotLimit(0, stone(1)), 256);
    EXPECT_EQ(inv.slotLimit(0, sword()), 1);
    auto r = inv.insert(stone(300), kPlayer);
    EXPECT_EQ(inv.at(0).count, 256);
    EXPECT_EQ(r.remainder.count, 44);
    inv.removeHooks("stack");
    EXPECT_EQ(inv.slotLimit(0, stone(1)), 64);
}

TEST(BackpackInventory, FiltersDenyAndVoid) {
    BackpackInventory inv(2);
    inv.addInsertFilter("deny-swords", 0, [](InsertQuery const& q) {
        return q.stack.id == "minecraft:iron_sword" ? InsertVerdict::Deny : InsertVerdict::Allow;
    });
    inv.addInsertFilter("void-pickup-dirt", 1, [](InsertQuery const& q) {
        return (q.stack.id == "minecraft:dirt" && q.ctx.source == Source::Pickup) ? InsertVerdict::Void
                                                                                  : InsertVerdict::Allow;
    });
    auto r = inv.insert(sword(), kPlayer);
    EXPECT_EQ(r.remainder.count, 1);
    EXPECT_TRUE(inv.isEmpty());

    auto dirt = ItemStackData::of("minecraft:dirt", 5);
    r         = inv.insert(dirt, kPickup);
    EXPECT_EQ(r.voided, 5);
    EXPECT_TRUE(r.remainder.empty());
    EXPECT_TRUE(inv.isEmpty());

    r = inv.insert(dirt, kPlayer);
    EXPECT_EQ(r.inserted, 5);
}

TEST(BackpackInventory, VoidOverflow) {
    BackpackInventory inv(1);
    inv.addInsertFilter("vo", 0, [](InsertQuery const& q) {
        return q.slot == -1 ? InsertVerdict::VoidOverflow : InsertVerdict::Allow;
    });
    auto r = inv.insert(stone(100), kPickup);
    EXPECT_EQ(r.inserted, 64);
    EXPECT_EQ(r.voided, 36);
    EXPECT_TRUE(r.remainder.empty());
}

TEST(BackpackInventory, PerSlotFilterSkipsSlot) {
    BackpackInventory inv(3);
    // Slot 0 only accepts swords (memory-like).
    inv.addInsertFilter("mem", 0, [](InsertQuery const& q) {
        if (q.slot != 0) return InsertVerdict::Allow;
        return q.stack.id == "minecraft:iron_sword" ? InsertVerdict::Allow : InsertVerdict::Deny;
    });
    inv.insert(stone(10), kPlayer);
    EXPECT_TRUE(inv.at(0).empty());
    EXPECT_EQ(inv.at(1).count, 10);
    auto r = inv.insertInto(0, stone(1), kPlayer);
    EXPECT_EQ(r.remainder.count, 1);
    r = inv.insertInto(0, sword(), kPlayer);
    EXPECT_EQ(r.inserted, 1);
}

TEST(BackpackInventory, InsertIntoOccupiedDifferentItem) {
    BackpackInventory inv(1);
    inv.set(0, sword());
    auto r = inv.insertInto(0, stone(3), kPlayer);
    EXPECT_EQ(r.inserted, 0);
    EXPECT_EQ(r.remainder.count, 3);
    EXPECT_EQ(inv.insertInto(5, stone(1), kPlayer).remainder.count, 1);
}

TEST(BackpackInventory, ExtractFilterAndMatching) {
    BackpackInventory inv(3);
    inv.set(0, stone(10));
    inv.set(1, sword());
    inv.set(2, stone(5));
    inv.addExtractFilter("lock1", 0, [](ExtractQuery const& q) { return q.slot != 1; });
    EXPECT_TRUE(inv.extract(1, 1, kPlayer).empty());
    auto got = inv.extractMatching([](ItemStackData const& s) { return s.id == "minecraft:stone"; }, 12, kPlayer);
    EXPECT_EQ(got.count, 12);
    EXPECT_TRUE(inv.at(2).empty()); // last slot first
    EXPECT_EQ(inv.at(0).count, 3);
    EXPECT_EQ(inv.count([](ItemStackData const& s) { return s.id == "minecraft:stone"; }), 3);
}

TEST(BackpackInventory, Sort) {
    BackpackInventory inv(4);
    inv.set(0, sword());
    inv.set(1, stone(40));
    inv.set(3, stone(40));
    inv.sort([](ItemStackData const& a, ItemStackData const& b) { return a.id > b.id; });
    EXPECT_EQ(inv.at(0).id, "minecraft:stone");
    EXPECT_EQ(inv.at(0).count, 64);
    EXPECT_EQ(inv.at(1).count, 16);
    EXPECT_EQ(inv.at(2).id, "minecraft:iron_sword");
    EXPECT_TRUE(inv.at(3).empty());
}

TEST(BackpackInventory, ResizeAndJson) {
    BackpackInventory inv(3);
    inv.set(2, stone(7));
    inv.addInsertFilter("x", 0, [](InsertQuery const&) { return InsertVerdict::Deny; });
    auto json = inv.toJson();
    auto back = BackpackInventory::fromJson(json);
    EXPECT_EQ(back.size(), 3);
    EXPECT_EQ(back.at(2).count, 7);
    EXPECT_EQ(back.insert(stone(1), kPlayer).inserted, 1); // hooks are not serialized

    BackpackInventory copy = inv; // copies slots only
    EXPECT_EQ(copy.insert(stone(1), kPlayer).inserted, 1);

    auto overflow = inv.resize(2);
    ASSERT_EQ(overflow.size(), 1u);
    EXPECT_EQ(overflow[0].count, 7);
    EXPECT_EQ(inv.size(), 2);
}

TEST(BackpackInventory, LargeCounts) {
    BackpackInventory inv(1);
    inv.addLimitModifier("big", 0, [](int, ItemStackData const&, int l) { return l * 64; });
    auto r = inv.insert(stone(4000), kPlayer);
    EXPECT_EQ(inv.at(0).count, 4000);
    EXPECT_TRUE(r.remainder.empty());
    auto j = inv.toJson();
    EXPECT_EQ(BackpackInventory::fromJson(j).at(0).count, 4000);
}
