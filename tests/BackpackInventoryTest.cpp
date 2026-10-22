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

TEST(BackpackInventory, ClearHooksExcept) {
    BackpackInventory inv(1);
    int               own = 0, other = 0;
    inv.addChangeListener("keep", [&](int) { ++own; });
    inv.addChangeListener("upgrade:0", [&](int) { ++other; });
    inv.addInsertFilter("upgrade:0", 0, [](InsertQuery const&) { return InsertVerdict::Deny; });
    inv.clearHooksExcept("keep");
    EXPECT_EQ(inv.insert(stone(1), kPlayer).inserted, 1);
    EXPECT_EQ(own, 1);
    EXPECT_EQ(other, 0);
}

TEST(BackpackInventory, FromJsonWrongTypes) {
    auto j = nlohmann::json::parse(R"({"size":"x","items":[5,{"slot":"a"},{"slot":0,"id":3}]})");
    auto inv = BackpackInventory::fromJson(j);
    EXPECT_EQ(inv.size(), 0);
    auto j2   = nlohmann::json::parse(R"({"size":2,"items":[{"slot":0,"id":"minecraft:stone","count":"x"}]})");
    auto inv2 = BackpackInventory::fromJson(j2);
    EXPECT_EQ(inv2.size(), 2);
    EXPECT_TRUE(inv2.at(0).empty());
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

TEST(BackpackInventory, InsertListenersSeeTouchedSlotsAfterTheOperation) {
    BackpackInventory inv(3);
    inv.set(1, stone(60));
    std::vector<std::pair<int, Source>> seen;
    inv.addInsertListener("t", [&](int slot, AccessContext const& ctx) {
        // Runs after the whole insert: the merged slot is already full.
        EXPECT_EQ(inv.at(1).count, 64);
        seen.emplace_back(slot, ctx.source);
    });
    inv.insert(stone(10), kPickup, true); // simulated: no notification
    EXPECT_TRUE(seen.empty());
    inv.insert(stone(10), kPickup);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0].first, 1);
    EXPECT_EQ(seen[1].first, 0);
    EXPECT_EQ(seen[0].second, Source::Pickup);
    seen.clear();
    inv.insertInto(2, stone(5), kPlayer);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].first, 2);
    EXPECT_EQ(seen[0].second, Source::Player);
    seen.clear();
    inv.insertInto(1, stone(5), kPlayer); // full: nothing moved
    EXPECT_TRUE(seen.empty());
    inv.removeHooks("t");
    inv.insert(stone(1), kPlayer);
    EXPECT_TRUE(seen.empty());
}

TEST(BackpackInventory, ExtractListenersGetTheOriginalContent) {
    BackpackInventory inv(2);
    inv.set(0, stone(10));
    int calls = 0;
    inv.addExtractListener("t", [&](int slot, ItemStackData const& original, AccessContext const& ctx) {
        ++calls;
        EXPECT_EQ(slot, 0);
        EXPECT_EQ(original.count, 10);
        EXPECT_EQ(ctx.source, Source::Pickup);
    });
    inv.extract(0, 4, kPickup, true);
    EXPECT_EQ(calls, 0);
    inv.extract(0, 4, kPickup);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(inv.at(0).count, 6);
}

TEST(BackpackInventory, InsertListenerMayInsertAgain) {
    BackpackInventory inv(4);
    int               crafts = 0;
    inv.addInsertListener("t", [&](int slot, AccessContext const&) {
        // Turn 4 stone into 1 cobblestone, like a compacting upgrade (re-entrant).
        if (inv.at(slot).id != "minecraft:stone" || inv.at(slot).count < 4) return;
        ++crafts;
        inv.extract(slot, 4, AccessContext{Source::Upgrade});
        inv.insert(ItemStackData::of("minecraft:cobblestone", 1), AccessContext{Source::Upgrade});
    });
    inv.insert(stone(4), kPickup);
    EXPECT_EQ(crafts, 1);
    EXPECT_EQ(inv.count([](ItemStackData const& s) { return s.id == "minecraft:stone"; }), 0);
    EXPECT_EQ(inv.count([](ItemStackData const& s) { return s.id == "minecraft:cobblestone"; }), 1);
}

TEST(BackpackInventory, InfiniteSlotsNeverDeplete) {
    BackpackInventory inv(3);
    inv.set(0, stone(1));
    inv.set(1, stone(5));
    inv.addInfiniteSlots("inf", [&inv](int slot) { return !inv.at(slot).empty(); });
    EXPECT_TRUE(inv.isInfinite(0));
    EXPECT_FALSE(inv.isInfinite(2)); // empty slots are not infinite
    EXPECT_FALSE(inv.isInfinite(7)); // out of range

    auto got = inv.extract(0, 64, kPlayer);
    EXPECT_EQ(got.count, 64); // not limited by the stored count
    EXPECT_EQ(inv.at(0).count, 1);
    auto all = inv.extractMatching([](ItemStackData const& s) { return s.id == "minecraft:stone"; }, 100, kPlayer);
    EXPECT_EQ(all.count, 100);
    EXPECT_EQ(inv.at(1).count, 5);

    // Extract filters still apply.
    inv.addExtractFilter("deny", 0, [](ExtractQuery const&) { return false; });
    EXPECT_TRUE(inv.extract(0, 1, kPlayer).empty());

    inv.removeHooks("inf");
    inv.removeHooks("deny");
    EXPECT_FALSE(inv.isInfinite(0));
    EXPECT_EQ(inv.extract(1, 64, kPlayer).count, 5);
    EXPECT_TRUE(inv.at(1).empty());

    inv.addInfiniteSlots("inf", [](int) { return true; });
    inv.clearFilters();
    EXPECT_FALSE(inv.isInfinite(0));
}
