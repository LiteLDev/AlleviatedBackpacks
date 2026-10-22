// Sophisticated Core FilterLogic / ContentsFilterLogic / DepositFilterLogic semantics.

#include <gtest/gtest.h>

#include <map>

#include "core/Filter.h"

using namespace abp::core;

namespace {
class Tags : public ITagProvider {
public:
    std::map<std::string, std::vector<std::string>> tags{
        {"minecraft:oak_log",   {"minecraft:logs", "minecraft:wood", "minecraft:logs_that_burn"}},
        {"minecraft:birch_log", {"minecraft:logs", "minecraft:logs_that_burn"}                  },
        {"minecraft:crimson_stem", {"minecraft:logs"}                                           },
        {"minecraft:stone",     {"minecraft:stone_like"}                                        },
    };
    bool hasTag(ItemStackData const& item, std::string_view tag) const override {
        auto it = tags.find(item.id);
        if (it == tags.end()) return false;
        for (auto const& t : it->second)
            if (t == tag) return true;
        return false;
    }
    std::vector<std::string> tagsOf(ItemStackData const& item) const override {
        auto it = tags.find(item.id);
        return it == tags.end() ? std::vector<std::string>{} : it->second;
    }
};

ItemStackData item(std::string const& id) { return ItemStackData::of(id, 1); }
} // namespace

TEST(FilterLogic, NewFilterIsBlockList) {
    Filter f(9, FilterDefaults{});
    EXPECT_EQ(f.mode, FilterMode::Deny);
    EXPECT_EQ(f.contentsType(), ContentsFilterType::Block);
    EXPECT_TRUE(f.matchAnyTag);
    EXPECT_EQ(f.ghosts.size(), 9u);
    // Empty block list matches everything.
    EXPECT_TRUE(f.passes(item("minecraft:stone")));
    // allowByDefault
    Filter a(9, FilterDefaults{.allowList = true});
    EXPECT_EQ(a.mode, FilterMode::Allow);
}

TEST(FilterLogic, EmptyAllowList) {
    Filter f(4, FilterDefaults{.allowList = true});
    EXPECT_FALSE(f.passes(item("minecraft:stone"))); // matches nothing
    Filter g(4, FilterDefaults{.allowList = true, .emptyAllowListMatchesEverything = true});
    EXPECT_TRUE(g.passes(item("minecraft:stone")));
    g.setGhost(0, item("minecraft:dirt"));
    EXPECT_FALSE(g.passes(item("minecraft:stone")));
    EXPECT_TRUE(g.passes(item("minecraft:dirt")));
}

TEST(FilterLogic, EmptyItemNeverMatchesContents) {
    Filter f(4, FilterDefaults{});
    EXPECT_FALSE(f.matchesContents(ItemStackData{}, {}));
}

TEST(FilterLogic, ModMatchHonoursDurabilityAndNbt) {
    Filter f(2, FilterDefaults{.allowList = true});
    f.matchBy = MatchBy::Namespace;
    auto ghost = ItemStackData::of("mod:a", 1, 1);
    ghost.damage = 2;
    f.setGhost(0, ghost);
    auto other   = ItemStackData::of("mod:b", 1, 1);
    other.damage = 5;
    EXPECT_TRUE(f.passes(other));
    f.matchDurability = true;
    EXPECT_FALSE(f.passes(other));
    other.damage = 2;
    EXPECT_TRUE(f.passes(other));
    f.matchNbt = true;
    other.setNbt("x");
    EXPECT_FALSE(f.passes(other));
    EXPECT_FALSE(f.passes(item("minecraft:stone")));
}

TEST(FilterLogic, TagsMatchAnyAndAll) {
    Tags   t;
    Filter f(4, FilterDefaults{.allowList = true});
    f.matchBy = MatchBy::Tag;
    // Allow + any tag + no tags: nothing.
    EXPECT_FALSE(f.passes(item("minecraft:oak_log"), &t));
    // Allow + all tags + no tags: everything (vacuous, as SC).
    f.matchAnyTag = false;
    EXPECT_TRUE(f.passes(item("minecraft:oak_log"), &t));
    f.addTag("minecraft:logs");
    f.addTag("minecraft:logs_that_burn");
    EXPECT_TRUE(f.passes(item("minecraft:oak_log"), &t));
    EXPECT_TRUE(f.passes(item("minecraft:birch_log"), &t));
    EXPECT_FALSE(f.passes(item("minecraft:crimson_stem"), &t)); // lacks logs_that_burn
    f.matchAnyTag = true;
    EXPECT_TRUE(f.passes(item("minecraft:crimson_stem"), &t));
    EXPECT_FALSE(f.passes(item("minecraft:stone"), &t));
    // Block list in TAGS mode = no tag match; the emptyAllowList flag does not matter.
    f.mode = FilterMode::Deny;
    EXPECT_FALSE(f.passes(item("minecraft:crimson_stem"), &t));
    EXPECT_TRUE(f.passes(item("minecraft:stone"), &t));
    f.tags.clear();
    EXPECT_TRUE(f.passes(item("minecraft:stone"), &t)); // any of nothing = no match -> passes
}

TEST(FilterLogic, TagListSortedUnique) {
    Filter f(1);
    EXPECT_TRUE(f.addTag("minecraft:wood"));
    EXPECT_TRUE(f.addTag("minecraft:logs"));
    EXPECT_FALSE(f.addTag("minecraft:logs"));
    EXPECT_EQ(f.tags, (std::vector<std::string>{"minecraft:logs", "minecraft:wood"}));
    EXPECT_TRUE(f.removeTag("minecraft:logs"));
    EXPECT_FALSE(f.removeTag("minecraft:logs"));
    Tags t;
    auto add = f.tagsToAdd(t.tagsOf(item("minecraft:oak_log")));
    EXPECT_EQ(add, (std::vector<std::string>{"minecraft:logs", "minecraft:logs_that_burn"}));
    f.addTag("minecraft:logs");
    f.addTag("minecraft:logs_that_burn");
    EXPECT_TRUE(f.tagsToAdd(t.tagsOf(item("minecraft:oak_log"))).empty()); // "All Item's tags are already added"
}

// "logs" and "minecraft:logs" are one tag (SC ResourceLocation.parse defaults to minecraft).
TEST(FilterLogic, TagsWithoutNamespaceAreMinecraft) {
    EXPECT_EQ(abp::core::normalizeTag("logs"), "minecraft:logs");
    EXPECT_EQ(abp::core::normalizeTag("minecraft:logs"), "minecraft:logs");
    EXPECT_EQ(abp::core::normalizeTag("c:wood"), "c:wood");
    EXPECT_EQ(abp::core::normalizeTag(""), "");
    Filter f(1);
    EXPECT_TRUE(f.addTag("logs"));
    EXPECT_FALSE(f.addTag("minecraft:logs"));
    EXPECT_EQ(f.tags, (std::vector<std::string>{"minecraft:logs"}));
    EXPECT_TRUE(f.hasTag("logs") && f.hasTag("minecraft:logs"));
    EXPECT_TRUE(f.tagsToAdd({"logs", "minecraft:logs", "wood"}) == (std::vector<std::string>{"minecraft:wood"}));
    EXPECT_TRUE(f.removeTag("logs"));
    EXPECT_TRUE(f.tags.empty());
    std::vector<std::string> v{"logs", "minecraft:logs", "c:logs"};
    abp::core::sortTags(v);
    EXPECT_EQ(v, (std::vector<std::string>{"c:logs", "minecraft:logs"}));
    // Stored filters with a bare tag load normalized.
    auto j    = f.toJson();
    j["tags"] = {"planks", "minecraft:planks"};
    EXPECT_EQ(Filter::fromJson(j, 1).tags, (std::vector<std::string>{"minecraft:planks"}));
}

// SC TreeSet<ResourceLocation>: path first, then namespace ("x" = "minecraft:x").
TEST(FilterLogic, TagOrderIsResourceLocationOrder) {
    EXPECT_TRUE(abp::core::tagLess("zzz:apple", "aaa:banana"));
    EXPECT_TRUE(abp::core::tagLess("a:logs", "b:logs"));
    EXPECT_TRUE(abp::core::tagLess("logs", "zzz:logs"));      // minecraft < zzz
    EXPECT_TRUE(abp::core::tagLess("aaa:logs", "logs"));      // aaa < minecraft
    EXPECT_FALSE(abp::core::tagLess("minecraft:x", "minecraft:x"));
    Filter f(1);
    f.addTag("mymod:wood");
    f.addTag("minecraft:logs");
    f.addTag("c:wood");
    f.addTag("minecraft:axe");
    EXPECT_EQ(f.tags, (std::vector<std::string>{"minecraft:axe", "minecraft:logs", "c:wood", "mymod:wood"}));
    std::vector<std::string> v{"z:b", "a:c", "z:b", "m:a"};
    abp::core::sortTags(v);
    EXPECT_EQ(v, (std::vector<std::string>{"m:a", "z:b", "a:c"}));
}

TEST(FilterLogic, RejectsDuplicates) {
    Filter f(3, FilterDefaults{});
    f.setGhost(0, item("minecraft:stone"));
    EXPECT_FALSE(f.canSetGhost(item("minecraft:stone")));
    EXPECT_TRUE(f.canSetGhost(item("minecraft:dirt")));
    EXPECT_TRUE(f.canSetGhost(ItemStackData{}));
    auto named = item("minecraft:stone");
    named.setNbt("named");
    EXPECT_TRUE(f.canSetGhost(named)); // different data = different item (isSameItemSameComponents)
}

TEST(FilterLogic, ContentsTypeCycle) {
    Filter f(2, FilterDefaults{});
    EXPECT_EQ(f.contentsType(), ContentsFilterType::Block);
    f.setContentsType(f.nextContentsType());
    EXPECT_EQ(f.contentsType(), ContentsFilterType::Storage);
    EXPECT_EQ(f.mode, FilterMode::Deny); // STORAGE keeps the list mode underneath
    f.setContentsType(f.nextContentsType());
    EXPECT_EQ(f.contentsType(), ContentsFilterType::Allow);
    EXPECT_FALSE(f.filterByStorage);
    // TAGS skips STORAGE.
    f.matchBy = MatchBy::Tag;
    f.setContentsType(f.nextContentsType());
    EXPECT_EQ(f.contentsType(), ContentsFilterType::Block);
    f.setContentsType(f.nextContentsType());
    EXPECT_EQ(f.contentsType(), ContentsFilterType::Allow);
}

TEST(FilterLogic, PrimaryMatchCycleLeavesStorage) {
    Filter f(2, FilterDefaults{});
    f.setContentsType(ContentsFilterType::Storage);
    f.cyclePrimaryMatch(true); // ITEM -> MOD
    EXPECT_EQ(f.matchBy, MatchBy::Namespace);
    EXPECT_EQ(f.contentsType(), ContentsFilterType::Storage);
    f.cyclePrimaryMatch(true); // MOD -> TAGS: STORAGE -> ALLOW
    EXPECT_EQ(f.matchBy, MatchBy::Tag);
    EXPECT_EQ(f.contentsType(), ContentsFilterType::Allow);
    f.cyclePrimaryMatch(true);
    EXPECT_EQ(f.matchBy, MatchBy::Item);
    // Deposit filters don't have STORAGE; the flag is untouched.
    Filter d(2, FilterDefaults{});
    d.setDepositType(DepositFilterType::Inventory);
    d.cyclePrimaryMatch(false);
    d.cyclePrimaryMatch(false);
    EXPECT_EQ(d.depositType(), DepositFilterType::Inventory);
}

TEST(FilterLogic, StorageAndInventoryMatch) {
    Filter f(2, FilterDefaults{.allowList = true});
    f.setGhost(0, item("minecraft:dirt"));
    std::vector<ItemStackData> stored{item("minecraft:stone"), item("minecraft:cobblestone")};
    EXPECT_TRUE(f.matchesContents(item("minecraft:dirt"), stored));
    EXPECT_FALSE(f.matchesContents(item("minecraft:stone"), stored));
    f.setContentsType(ContentsFilterType::Storage);
    EXPECT_FALSE(f.matchesContents(item("minecraft:dirt"), stored)); // filter items ignored
    EXPECT_TRUE(f.matchesContents(item("minecraft:stone"), stored));
    // Memorized slots count as storage contents.
    auto memory = [](ItemStackData const& i) { return i.id == "minecraft:diamond"; };
    EXPECT_TRUE(f.matchesContents(item("minecraft:diamond"), stored, nullptr, memory));
    // MOD primary match applies to the storage comparison.
    f.matchBy = MatchBy::Namespace;
    EXPECT_TRUE(f.matchesContents(item("minecraft:gold_ingot"), stored));
    EXPECT_FALSE(f.matchesContents(item("othermod:gold"), stored));

    Filter d(2, FilterDefaults{});
    d.setDepositType(DepositFilterType::Inventory);
    EXPECT_EQ(d.depositType(), DepositFilterType::Inventory);
    EXPECT_FALSE(d.matchesContents(item("minecraft:stone"), {})); // empty container: nothing
    EXPECT_TRUE(d.matchesContents(item("minecraft:stone"), stored));
    d.setDepositType(DepositFilterType::Allow);
    EXPECT_FALSE(d.filterByInventory);
}

TEST(FilterLogic, LegacyGhostTagsKept) {
    Tags t;
    // Phase-1 JSON (no "v"): tag mode without tags matched the filter items' tags.
    Filter old(2);
    old.matchBy = MatchBy::Tag;
    old.setGhost(0, item("minecraft:oak_log"));
    auto j = old.toJson();
    j.erase("v");
    j.erase("legacyTags");
    auto f = Filter::fromJson(j, 2);
    EXPECT_TRUE(f.legacyGhostTags);
    EXPECT_TRUE(f.passes(item("minecraft:birch_log"), &t));
    EXPECT_FALSE(f.passes(item("minecraft:stone"), &t));
    EXPECT_FALSE(f.isEmpty());
    // Survives a save; selecting a tag switches to SC semantics.
    auto g = Filter::fromJson(f.toJson(), 2);
    EXPECT_TRUE(g.legacyGhostTags);
    g.addTag("minecraft:stone_like");
    EXPECT_TRUE(g.passes(item("minecraft:stone"), &t));
    EXPECT_FALSE(g.passes(item("minecraft:birch_log"), &t));
    // New filters never use it.
    Filter n(2, FilterDefaults{.allowList = true});
    n.matchBy = MatchBy::Tag;
    n.setGhost(0, item("minecraft:oak_log"));
    EXPECT_FALSE(Filter::fromJson(n.toJson(), 2).passes(item("minecraft:birch_log"), &t));
}

TEST(FilterLogic, JsonRoundTripAndDefaults) {
    Filter f(3, FilterDefaults{});
    f.matchAnyTag = false;
    f.setContentsType(ContentsFilterType::Storage);
    f.filterByInventory = true;
    f.addTag("b:c");
    f.addTag("a:b");
    auto g = Filter::fromJson(f.toJson(), 3, FilterDefaults{});
    EXPECT_FALSE(g.matchAnyTag);
    EXPECT_TRUE(g.filterByStorage);
    EXPECT_TRUE(g.filterByInventory);
    EXPECT_EQ(g.mode, FilterMode::Deny);
    EXPECT_EQ(g.tags, (std::vector<std::string>{"a:b", "b:c"}));
    // A missing filter is a new filter with the upgrade's defaults.
    auto n = Filter::fromJson(nlohmann::json(), 16, FilterDefaults{.allowList = true});
    EXPECT_EQ(n.mode, FilterMode::Allow);
    EXPECT_EQ(n.ghosts.size(), 16u);
    EXPECT_FALSE(n.emptyAllowsAll);
    // emptyAllowsAll is a property of the upgrade, not of the stored data.
    auto e = Filter::fromJson(nlohmann::json{{"mode", 0}, {"emptyAll", true}}, 2, FilterDefaults{});
    EXPECT_FALSE(e.emptyAllowsAll);
}
