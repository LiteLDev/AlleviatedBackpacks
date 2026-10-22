#include <gtest/gtest.h>

#include "upgrade/void/VoidRules.h"

using namespace abp::core;
using namespace abp::upgrade::voiding;

namespace {
ItemStackData cobble(int n) { return ItemStackData::of("minecraft:cobblestone", n); }
ItemStackData dirt(int n) { return ItemStackData::of("minecraft:dirt", n); }
AccessContext const kPickup{Source::Pickup};
AccessContext const kPlayer{Source::Player};

VoidRules rules(VoidType t, bool gui = false) {
    VoidRules r;
    r.type      = t;
    r.workInGui = gui;
    r.matches   = [](ItemStackData const& i) { return i.id == "minecraft:cobblestone"; };
    return r;
}

void install(BackpackInventory& inv, VoidRules r) {
    inv.addInsertFilter("void", 1001, [r](InsertQuery const& q) { return verdict(r, q); });
}
} // namespace

TEST(VoidRules, TypeNamesAndCycle) {
    EXPECT_EQ(voidTypeFromName("slot_overflow"), VoidType::SlotOverflow);
    EXPECT_EQ(voidTypeFromName("storage_overflow"), VoidType::StorageOverflow);
    EXPECT_EQ(voidTypeFromName("x"), VoidType::Always);
    EXPECT_EQ(next(VoidType::Always, true), VoidType::SlotOverflow);
    EXPECT_EQ(next(VoidType::StorageOverflow, true), VoidType::Always);
    EXPECT_EQ(next(VoidType::StorageOverflow, false), VoidType::SlotOverflow);
    EXPECT_EQ(effective(VoidType::Always, false), VoidType::SlotOverflow);
}

TEST(VoidRules, AlwaysVoidsAutomaticInsertsOnly) {
    BackpackInventory inv(3);
    install(inv, rules(VoidType::Always));
    auto r = inv.insert(cobble(10), kPickup);
    EXPECT_EQ(r.voided, 10);
    r = inv.insert(dirt(10), kPickup);
    EXPECT_EQ(r.inserted, 10);
    r = inv.insert(cobble(10), kPlayer);
    EXPECT_EQ(r.inserted, 10); // GUI: voided on the next tick by the upgrade, not here
    // No room and none stored: SC rejects before the upgrades see it.
    BackpackInventory full(1);
    full.set(0, dirt(64));
    install(full, rules(VoidType::Always));
    r = full.insert(cobble(5), kPickup);
    EXPECT_EQ(r.voided, 0);
    EXPECT_EQ(r.remainder.count, 5);
}

TEST(VoidRules, SlotOverflowKeepsOneSlot) {
    BackpackInventory inv(4);
    install(inv, rules(VoidType::SlotOverflow));
    auto r = inv.insert(cobble(100), kPickup);
    EXPECT_EQ(r.inserted, 64);
    EXPECT_EQ(r.voided, 36);
    r = inv.insert(cobble(1), kPickup);
    EXPECT_EQ(r.voided, 1);
    // Direct insert into another empty slot is voided too.
    r = inv.insertInto(2, cobble(3), kPickup);
    EXPECT_EQ(r.voided, 3);
    EXPECT_TRUE(inv.at(2).empty());
}

TEST(VoidRules, SlotOverflowTopsUpPartialStack) {
    BackpackInventory inv(4);
    inv.set(1, cobble(30));
    install(inv, rules(VoidType::SlotOverflow));
    auto r = inv.insert(cobble(50), kPickup);
    EXPECT_EQ(r.inserted, 34);
    EXPECT_EQ(r.voided, 16);
    EXPECT_EQ(inv.at(1).count, 64);
}

TEST(VoidRules, StorageOverflow) {
    BackpackInventory inv(2);
    install(inv, rules(VoidType::StorageOverflow));
    auto r = inv.insert(cobble(100), kPickup);
    EXPECT_EQ(r.inserted, 100);
    r = inv.insert(cobble(100), kPickup);
    EXPECT_EQ(r.inserted, 28);
    EXPECT_EQ(r.voided, 72);
    r = inv.insert(dirt(1), kPickup);
    EXPECT_EQ(r.remainder.count, 1);
}

TEST(VoidRules, GuiSlotOverflowOnlyWithWorkInGui) {
    BackpackInventory inv(3);
    install(inv, rules(VoidType::SlotOverflow, true));
    auto r = inv.insertInto(0, cobble(60), kPlayer);
    EXPECT_EQ(r.inserted, 60);
    r = inv.insertInto(0, cobble(10), kPlayer);
    EXPECT_EQ(r.inserted, 4);
    EXPECT_EQ(r.voided, 6);
    r = inv.insertInto(1, cobble(5), kPlayer);
    EXPECT_EQ(r.voided, 5);
    // Shift-click (automatic placement) merges and voids the rest.
    r = inv.insert(cobble(5), kPlayer);
    EXPECT_EQ(r.voided, 5);

    BackpackInventory plain(3);
    install(plain, rules(VoidType::SlotOverflow, false));
    plain.insertInto(0, cobble(60), kPlayer);
    r = plain.insertInto(1, cobble(5), kPlayer);
    EXPECT_EQ(r.inserted, 5);
}

TEST(VoidRules, InternalNeverVoided) {
    BackpackInventory inv(2);
    install(inv, rules(VoidType::Always));
    auto r = inv.insert(cobble(5), AccessContext{Source::Internal});
    EXPECT_EQ(r.inserted, 5);
}
