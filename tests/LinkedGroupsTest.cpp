#include <gtest/gtest.h>

#include "core/LinkedGroups.h"

using namespace abp::core;

TEST(LinkedGroups, CreateWithPrimary) {
    LinkedGroups groups;
    auto& g = groups.create("g1", "p1", "e1", "s1", "Main", "alleviatedbackpacks:iron_backpack");
    EXPECT_TRUE(g.isPrimary("e1"));
    EXPECT_TRUE(g.hasEndpoint("e1"));
    EXPECT_FALSE(g.hasEndpoint("e2"));
    EXPECT_EQ(groups.size(), 1u);
    ASSERT_NE(groups.byStorage("s1"), nullptr);
    EXPECT_EQ(groups.byStorage("s1")->id, "g1");
    EXPECT_EQ(groups.byStorage("nope"), nullptr);
}

TEST(LinkedGroups, SecondaryEndpoints) {
    LinkedGroups groups;
    auto& g = groups.create("g1", "", "e1", "s1", "", "x");
    g.addEndpoint("e2");
    g.addEndpoint("e2"); // no duplicates
    EXPECT_EQ(g.endpoints.size(), 2u);
    EXPECT_FALSE(g.isPrimary("e2"));
    EXPECT_FALSE(g.removeEndpoint("e1")); // the primary stays
    EXPECT_TRUE(g.removeEndpoint("e2"));
    EXPECT_FALSE(g.hasEndpoint("e2"));
    EXPECT_TRUE(g.detachLostEndpoint("e1"));
    EXPECT_TRUE(g.endpoints.empty());
}

TEST(LinkedGroups, JsonRoundTrip) {
    LinkedGroups groups;
    auto& g = groups.create("g1", "p1", "e1", "s1", "Shared", "alleviatedbackpacks:gold_backpack");
    g.addEndpoint("e2");
    g.recordOpened("e2", "p2", 1234);
    groups.create("g2", "p1", "e3", "s2", "", "alleviatedbackpacks:leather_backpack");
    auto back = LinkedGroups::fromJson(groups.toJson());
    EXPECT_EQ(back.size(), 2u);
    auto* r = back.find("g1");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->name, "Shared");
    EXPECT_EQ(r->storage, "s1");
    EXPECT_EQ(r->carrier, "alleviatedbackpacks:gold_backpack");
    ASSERT_EQ(r->endpoints.size(), 2u);
    EXPECT_EQ(r->endpoints[1].lastOpenedBy, "p2");
    EXPECT_EQ(r->endpoints[1].lastOpenedAt, 1234);
    auto storages = back.storages();
    EXPECT_EQ(storages.size(), 2u);
}

TEST(LinkedGroups, BadJsonIsIgnored) {
    EXPECT_EQ(LinkedGroups::fromJson(nlohmann::json::array()).size(), 0u);
    auto j = nlohmann::json::parse(R"({"groups":[{"id":"g"},{"id":"g2","storage":"s","primary":"e"},5]})");
    auto groups = LinkedGroups::fromJson(j);
    EXPECT_EQ(groups.size(), 1u);
    EXPECT_NE(groups.find("g2"), nullptr);
}
