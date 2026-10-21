#include <gtest/gtest.h>

#include "core/Codec.h"
#include "core/Format.h"
#include "core/ItemStackData.h"
#include "core/Tier.h"

using namespace abp::core;

TEST(Tier, TableMatchesSpec) {
    EXPECT_EQ(tierInfo(Tier::Leather).slots, 27);
    EXPECT_EQ(tierInfo(Tier::Leather).upgradeSlots, 1);
    EXPECT_EQ(tierInfo(Tier::Copper).slots, 45);
    EXPECT_EQ(tierInfo(Tier::Copper).upgradeSlots, 1);
    EXPECT_EQ(tierInfo(Tier::Iron).slots, 54);
    EXPECT_EQ(tierInfo(Tier::Iron).upgradeSlots, 2);
    EXPECT_EQ(tierInfo(Tier::Gold).slots, 81);
    EXPECT_EQ(tierInfo(Tier::Gold).upgradeSlots, 3);
    EXPECT_EQ(tierInfo(Tier::Diamond).slots, 108);
    EXPECT_EQ(tierInfo(Tier::Diamond).upgradeSlots, 5);
    EXPECT_EQ(tierInfo(Tier::Netherite).slots, 120);
    EXPECT_EQ(tierInfo(Tier::Netherite).upgradeSlots, 7);
}

TEST(Tier, Lookup) {
    EXPECT_EQ(tierFromId("gold"), Tier::Gold);
    EXPECT_EQ(tierFromId("nope"), std::nullopt);
    EXPECT_EQ(tierFromInt(5), Tier::Netherite);
    EXPECT_EQ(tierFromInt(6), std::nullopt);
    EXPECT_EQ(nextTier(Tier::Leather), Tier::Copper);
    EXPECT_EQ(nextTier(Tier::Netherite), std::nullopt);
}

TEST(Codec, Base64RoundTrip) {
    for (std::string const& s :
         std::vector<std::string>{"", "a", "ab", "abc", "abcd", std::string("\0\xff\x10zz", 5)}) {
        auto enc = base64Encode(s);
        auto dec = base64Decode(enc);
        ASSERT_TRUE(dec.has_value()) << enc;
        EXPECT_EQ(*dec, s);
    }
    EXPECT_EQ(base64Encode("Man"), "TWFu");
    EXPECT_EQ(base64Encode("Ma"), "TWE=");
    EXPECT_FALSE(base64Decode("TWE").has_value());
    EXPECT_FALSE(base64Decode("T=E=").has_value());
}

TEST(Codec, Uuid) {
    auto a = newUuid();
    auto b = newUuid();
    EXPECT_TRUE(isUuid(a));
    EXPECT_NE(a, b);
    EXPECT_EQ(a[14], '4');
    EXPECT_FALSE(isUuid("not-a-uuid"));
}

TEST(Codec, Glob) {
    EXPECT_TRUE(globMatch("*", ""));
    EXPECT_TRUE(globMatch("core.*", "core.inventory"));
    EXPECT_FALSE(globMatch("core.*", "menu.open"));
    EXPECT_TRUE(globMatch("*.open*", "menu.open_close"));
    EXPECT_TRUE(globMatch("m?nu.*", "menu.x"));
    EXPECT_FALSE(globMatch("menu", "menu.x"));
}

TEST(Format, Placeholders) {
    EXPECT_EQ(formatMessageArgs("a {} b {}", 1, "x"), "a 1 b x");
    EXPECT_EQ(formatMessageArgs("{1}-{0}", "a", "b"), "b-a");
    EXPECT_EQ(formatMessageArgs("{{}} {}", 5), "{} 5");
    EXPECT_EQ(formatMessageArgs("{} {} {}", 1), "1 {} {}");
    EXPECT_EQ(formatMessageArgs("{x}", 1), "{x}");
}

TEST(Format, CompactCount) {
    EXPECT_EQ(compactCount(999), "999");
    EXPECT_EQ(compactCount(1234), "1.2k");
    EXPECT_EQ(compactCount(64000), "64k");
    EXPECT_EQ(compactCount(1500000), "1.5M");
}

TEST(ItemStackData, IdentityAndJson) {
    auto a = ItemStackData::of("minecraft:stone", 10);
    auto b = ItemStackData::of("minecraft:stone", 3);
    EXPECT_TRUE(a.sameItem(b));
    b.setNbt("blob");
    EXPECT_FALSE(a.sameItem(b));
    EXPECT_EQ(b.itemNamespace(), "minecraft");
    EXPECT_EQ(ItemStackData::of("foo", 1).itemNamespace(), "minecraft");
    EXPECT_EQ(ItemStackData::of("mod:x", 1).itemNamespace(), "mod");

    auto j = b.toJson();
    auto c = ItemStackData::fromJson(j);
    EXPECT_EQ(c, b);
    EXPECT_EQ(c.nbtHash(), b.nbtHash());
    EXPECT_TRUE(ItemStackData{}.toJson().is_null());
    EXPECT_TRUE(ItemStackData::fromJson(nullptr).empty());
    EXPECT_TRUE(a.withCount(0).empty());
}
