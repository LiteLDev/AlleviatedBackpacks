#include <gtest/gtest.h>

#include <algorithm>
#include <map>

#include "core/Sorting.h"

using namespace abp::core;

namespace {
ItemStackData item(std::string id, int n, int maxStack = 64) { return ItemStackData::of(std::move(id), n, maxStack); }
AccessContext const kPlayer{Source::Player};

class FakeTags final : public ITagProvider {
public:
    std::map<std::string, std::vector<std::string>> map;
    bool hasTag(ItemStackData const& i, std::string_view tag) const override {
        auto it = map.find(i.id);
        return it != map.end() && std::find(it->second.begin(), it->second.end(), tag) != it->second.end();
    }
    std::vector<std::string> tagsOf(ItemStackData const& i) const override {
        auto it = map.find(i.id);
        return it == map.end() ? std::vector<std::string>{} : it->second;
    }
};

std::vector<std::string> ids(BackpackInventory const& inv) {
    std::vector<std::string> out;
    for (int i = 0; i < inv.size(); ++i) out.push_back(inv.at(i).empty() ? "-" : inv.at(i).id + "x" + std::to_string(inv.at(i).count));
    return out;
}
} // namespace

TEST(Sorting, SortByNames) {
    for (int i = 0; i < kSortByCount; ++i) {
        auto by = static_cast<SortBy>(i);
        EXPECT_EQ(sortByFromString(toString(by)), by);
    }
    EXPECT_FALSE(sortByFromString("bogus").has_value());
    EXPECT_EQ(nextSortBy(SortBy::Tag), SortBy::Name);
    EXPECT_EQ(nextSortBy(SortBy::Name), SortBy::Count);
}

TEST(Sorting, ByNameMergesAndOrders) {
    BackpackInventory inv(5);
    inv.set(0, item("minecraft:stone", 10));
    inv.set(1, item("minecraft:apple", 3));
    inv.set(3, item("minecraft:stone", 60));
    ASSERT_TRUE(sortInventory(inv, comparatorFor(SortBy::Name)));
    EXPECT_EQ(ids(inv), (std::vector<std::string>{"minecraft:applex3", "minecraft:stonex64", "minecraft:stonex6", "-", "-"}));
}

TEST(Sorting, ByCountMostFirst) {
    BackpackInventory inv(4);
    inv.set(0, item("minecraft:apple", 3));
    inv.set(1, item("minecraft:stone", 20));
    inv.set(2, item("minecraft:dirt", 20));
    ASSERT_TRUE(sortInventory(inv, comparatorFor(SortBy::Count)));
    EXPECT_EQ(ids(inv), (std::vector<std::string>{"minecraft:dirtx20", "minecraft:stonex20", "minecraft:applex3", "-"}));
}

TEST(Sorting, ByModGroupsNamespaces) {
    BackpackInventory inv(3);
    inv.set(0, item("minecraft:zinc", 1));
    inv.set(1, item("alleviatedbackpacks:zzz", 1));
    inv.set(2, item("minecraft:apple", 1));
    ASSERT_TRUE(sortInventory(inv, comparatorFor(SortBy::Mod)));
    EXPECT_EQ(ids(inv), (std::vector<std::string>{"alleviatedbackpacks:zzzx1", "minecraft:applex1", "minecraft:zincx1"}));
}

TEST(Sorting, ByTagUntaggedLast) {
    FakeTags tags;
    tags.map["minecraft:oak_log"]   = {"minecraft:logs"};
    tags.map["minecraft:iron_ore"]  = {"minecraft:ores", "minecraft:iron"};
    BackpackInventory inv(3);
    inv.set(0, item("minecraft:apple", 1));
    inv.set(1, item("minecraft:oak_log", 1));
    inv.set(2, item("minecraft:iron_ore", 1));
    ASSERT_TRUE(sortInventory(inv, comparatorFor(SortBy::Tag, &tags)));
    // "minecraft:iron" < "minecraft:logs"; apple has no tags.
    EXPECT_EQ(ids(inv), (std::vector<std::string>{"minecraft:iron_orex1", "minecraft:oak_logx1", "minecraft:applex1"}));
}

TEST(Sorting, LockedSlotsUntouched) {
    BackpackInventory inv(4);
    inv.set(0, item("minecraft:stone", 5));
    inv.set(1, item("minecraft:apple", 5));
    inv.set(3, item("minecraft:apple", 5));
    SortLayout layout;
    layout.locked = {true, false, false, false};
    ASSERT_TRUE(sortInventory(inv, comparatorFor(SortBy::Name), layout));
    EXPECT_EQ(ids(inv), (std::vector<std::string>{"minecraft:stonex5", "minecraft:applex10", "-", "-"}));
    // A locked empty slot stays empty.
    layout.locked = {false, true, false, false};
    inv.set(1, {});
    inv.set(3, item("minecraft:dirt", 1));
    ASSERT_TRUE(sortInventory(inv, comparatorFor(SortBy::Name), layout));
    EXPECT_EQ(ids(inv), (std::vector<std::string>{"minecraft:dirtx1", "-", "minecraft:stonex5", "-"}));
}

TEST(Sorting, MemorySlotsGetTheirItemAndStayReserved) {
    BackpackInventory inv(4);
    inv.set(0, item("minecraft:apple", 5));
    inv.set(1, item("minecraft:stone", 70));
    SortLayout layout;
    layout.memory.resize(4);
    layout.memory[3] = item("minecraft:stone", 1);
    layout.memory[0] = item("minecraft:diamond", 1); // nothing to put there: stays empty
    ASSERT_TRUE(sortInventory(inv, comparatorFor(SortBy::Name), layout));
    EXPECT_EQ(ids(inv), (std::vector<std::string>{"-", "minecraft:applex5", "minecraft:stonex6", "minecraft:stonex64"}));
}

TEST(Sorting, DoesNotFitKeepsLayout) {
    BackpackInventory inv(2);
    inv.set(0, item("minecraft:apple", 5));
    inv.set(1, item("minecraft:stone", 5));
    SortLayout layout;
    layout.memory = {item("minecraft:diamond", 1), {}};
    EXPECT_FALSE(sortInventory(inv, comparatorFor(SortBy::Name), layout));
    EXPECT_EQ(inv.at(0).id, "minecraft:apple");
    EXPECT_EQ(inv.at(1).id, "minecraft:stone");
}

TEST(Sorting, MemoryMatchIgnoresNbt) {
    auto a = item("minecraft:iron_sword", 1, 1);
    auto b = a;
    b.setNbt("enchanted");
    EXPECT_TRUE(memoryMatches(a, b));
    EXPECT_FALSE(memoryMatches(a, item("minecraft:stone", 1)));
    EXPECT_FALSE(memoryMatches({}, a));
}

TEST(BackpackInventory, SlotPreferenceFilledFirst) {
    BackpackInventory inv(4);
    inv.addSlotPreference("mem", 0, [](ItemStackData const& s) {
        return s.id == "minecraft:stone" ? std::vector<int>{3, 7, -1} : std::vector<int>{};
    });
    auto r = inv.insert(item("minecraft:stone", 70), kPlayer);
    EXPECT_EQ(r.inserted, 70);
    EXPECT_EQ(inv.at(3).count, 64);
    EXPECT_EQ(inv.at(0).count, 6);
    inv.insert(item("minecraft:apple", 1), kPlayer);
    EXPECT_EQ(inv.at(1).id, "minecraft:apple");
    inv.removeHooks("mem");
    inv.set(3, {});
    inv.set(0, {});
    inv.insert(item("minecraft:stone", 1), kPlayer);
    EXPECT_EQ(inv.at(0).count, 1);
}
