#include <gtest/gtest.h>

#include <map>

#include "core/BackpackData.h"
#include "core/Filter.h"

using namespace abp::core;

namespace {
class FakeTags : public ITagProvider {
public:
    std::map<std::string, std::vector<std::string>> tags{
        {"minecraft:oak_log",   {"minecraft:logs", "minecraft:wood"}},
        {"minecraft:birch_log", {"minecraft:logs"}                  },
        {"minecraft:stone",     {"minecraft:stone_like"}            },
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
} // namespace

TEST(Filter, AllowByItem) {
    Filter f(4);
    EXPECT_TRUE(f.passes(ItemStackData::of("minecraft:stone", 1))); // empty allow = all
    f.emptyAllowsAll = false;
    EXPECT_FALSE(f.passes(ItemStackData::of("minecraft:stone", 1)));
    f.setGhost(1, ItemStackData::of("minecraft:stone", 32));
    EXPECT_EQ(f.ghosts[1].count, 1);
    EXPECT_TRUE(f.passes(ItemStackData::of("minecraft:stone", 5)));
    EXPECT_FALSE(f.passes(ItemStackData::of("minecraft:dirt", 5)));
    EXPECT_TRUE(f.hasGhost(ItemStackData::of("minecraft:stone", 2)));
}

TEST(Filter, DenyMode) {
    Filter f(2);
    f.mode = FilterMode::Deny;
    f.setGhost(0, ItemStackData::of("minecraft:dirt", 1));
    EXPECT_FALSE(f.passes(ItemStackData::of("minecraft:dirt", 1)));
    EXPECT_TRUE(f.passes(ItemStackData::of("minecraft:stone", 1)));
}

TEST(Filter, NbtAndDurability) {
    Filter f(1);
    auto   ghost = ItemStackData::of("minecraft:iron_sword", 1, 1);
    ghost.setNbt("A");
    ghost.damage = 3;
    f.setGhost(0, ghost);
    auto other = ItemStackData::of("minecraft:iron_sword", 1, 1);
    other.setNbt("B");
    other.damage = 5;
    EXPECT_TRUE(f.matches(other));
    f.matchNbt = true;
    EXPECT_FALSE(f.matches(other));
    f.matchNbt        = false;
    f.matchDurability = true;
    EXPECT_FALSE(f.matches(other));
    other.damage = 3;
    EXPECT_TRUE(f.matches(other));
}

TEST(Filter, NamespaceAndTags) {
    FakeTags tags;
    Filter   f(2);
    f.matchBy = MatchBy::Namespace;
    f.setGhost(0, ItemStackData::of("othermod:thing", 1));
    EXPECT_TRUE(f.matches(ItemStackData::of("othermod:other", 1)));
    EXPECT_FALSE(f.matches(ItemStackData::of("minecraft:stone", 1)));

    // TAGS (SC): only the selected tag list counts, filter items are ignored.
    f.matchBy = MatchBy::Tag;
    f.setGhost(0, ItemStackData::of("minecraft:birch_log", 1));
    EXPECT_FALSE(f.matches(ItemStackData::of("minecraft:oak_log", 1), &tags));
    f.tags = {"minecraft:stone_like"};
    EXPECT_FALSE(f.matches(ItemStackData::of("minecraft:stone", 1))); // no provider
    EXPECT_TRUE(f.matches(ItemStackData::of("minecraft:stone", 1), &tags));
    EXPECT_FALSE(f.matches(ItemStackData::of("minecraft:oak_log", 1), &tags));
}

TEST(Filter, Json) {
    Filter f(3);
    f.mode     = FilterMode::Deny;
    f.matchBy  = MatchBy::Tag;
    f.matchNbt = true;
    f.tags     = {"a:b"};
    f.setGhost(2, ItemStackData::of("minecraft:stone", 1));
    auto g = Filter::fromJson(f.toJson());
    EXPECT_EQ(g.mode, FilterMode::Deny);
    EXPECT_EQ(g.matchBy, MatchBy::Tag);
    EXPECT_TRUE(g.matchNbt);
    ASSERT_EQ(g.ghosts.size(), 3u);
    EXPECT_EQ(g.ghosts[2].id, "minecraft:stone");
    EXPECT_EQ(g.tags, std::vector<std::string>{"a:b"});
    EXPECT_EQ(Filter::fromJson(f.toJson(), 9).ghosts.size(), 9u);
}

TEST(BackpackData, CreateAndRoundTrip) {
    auto d = BackpackData::create("11111111-2222-4333-8444-555555555555", Tier::Iron);
    EXPECT_EQ(d.inventory.size(), 54);
    EXPECT_EQ(d.upgrades.size(), 2u);
    d.inventory.set(53, ItemStackData::of("minecraft:stone", 300));
    d.upgrades[1].item               = ItemStackData::of("alleviatedbackpacks:pickup_upgrade", 1, 1);
    d.upgrades[1].enabled            = false;
    d.upgrades[1].settings["filter"] = Filter(9).toJson();
    d.settings["sort"]               = "name";
    d.mainColor                      = 0xFF112233;

    auto text = d.serialize();
    auto back = BackpackData::deserialize(text);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->uuid, d.uuid);
    EXPECT_EQ(back->tier, Tier::Iron);
    EXPECT_EQ(back->inventory.at(53).count, 300);
    EXPECT_EQ(back->upgrades[1].item.id, "alleviatedbackpacks:pickup_upgrade");
    EXPECT_FALSE(back->upgrades[1].enabled);
    EXPECT_TRUE(back->upgrades[1].settings.contains("filter"));
    EXPECT_EQ(back->settings["sort"], "name");
    EXPECT_EQ(back->mainColor, 0xFF112233u);
}

TEST(BackpackData, RejectsBadData) {
    EXPECT_FALSE(BackpackData::deserialize("not json").has_value());
    EXPECT_FALSE(BackpackData::deserialize("{\"v\":99,\"tier\":\"iron\"}").has_value());
    EXPECT_FALSE(BackpackData::deserialize("{\"v\":1,\"tier\":\"wood\"}").has_value());
    EXPECT_FALSE(BackpackData::deserialize("[]").has_value());
    // Wrong field types are "unreadable", never an exception.
    EXPECT_FALSE(BackpackData::deserialize(R"({"v":"1","tier":"iron"})").has_value());
    EXPECT_FALSE(BackpackData::deserialize(R"({"v":1,"tier":"iron","uuid":5})").has_value());
    auto ok = BackpackData::deserialize(R"({"v":1,"tier":"iron","inventory":{"size":"9"},"upgrades":[1,{"item":3}]})");
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->inventory.size(), 54);
    auto f = Filter::fromJson(nlohmann::json::parse(R"({"mode":"deny","ghosts":[1,2]})"), 3);
    EXPECT_EQ(f.mode, FilterMode::Allow);
    EXPECT_EQ(f.ghosts.size(), 3u);
}

TEST(BackpackData, SetTierGrows) {
    auto d = BackpackData::create("u", Tier::Leather);
    d.setTier(Tier::Netherite);
    EXPECT_EQ(d.inventory.size(), 120);
    EXPECT_EQ(d.upgrades.size(), 7u);
    // Data with fewer slots than the tier needs is padded on load.
    auto j                 = d.toJson();
    j["inventory"]["size"] = 3;
    auto back              = BackpackData::fromJson(j);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->inventory.size(), 120);
}
