// P2-12: Sophisticated Backpacks' colour mixing, dye columns and stack upgrade math.

#include <climits>
#include <vector>

#include <gtest/gtest.h>

#include "core/Appearance.h"
#include "core/BackpackData.h"
#include "core/BackpackInventory.h"
#include "core/StackMultiplier.h"

using namespace abp::core;

namespace {
std::uint32_t dye(DyeColor c) { return dyeInfo(c).argb; }
} // namespace

TEST(SbColors, NoDyesKeepsBase) {
    std::vector<std::uint32_t> none;
    EXPECT_EQ(calculateColor(0xFF123456u, kDefaultMainColor, none), 0xFF123456u);
}

TEST(SbColors, DefaultBaseIsNotMixedIn) {
    std::vector<std::uint32_t> red{dye(DyeColor::Red)};
    EXPECT_EQ(calculateColor(kDefaultMainColor, kDefaultMainColor, red), dye(DyeColor::Red));
}

TEST(SbColors, CreativeMixes) {
    // SB's creative-tab backpack: main = yellow + lime, accent = blue + black (from the defaults).
    std::vector<std::uint32_t> main{dye(DyeColor::Yellow), dye(DyeColor::Lime)};
    std::vector<std::uint32_t> accent{dye(DyeColor::Blue), dye(DyeColor::Black)};
    EXPECT_EQ(calculateColor(kDefaultMainColor, kDefaultMainColor, main), 0xFFD0E232u);
    EXPECT_EQ(calculateColor(kDefaultAccentColor, kDefaultAccentColor, accent), 0xFF2C3065u);
}

TEST(SbColors, NonDefaultBaseIsMixedIn) {
    // Red base + blue dye: avg (118, 57, 104), max components (176 + 170) / 2 = 173, max avg 118.
    std::vector<std::uint32_t> blue{dye(DyeColor::Blue)};
    auto c = calculateColor(dye(DyeColor::Red), kDefaultMainColor, blue);
    EXPECT_EQ(c, 0xFF000000u | (173u << 16) | (83u << 8) | 152u);
}

TEST(SbColors, DyeColumns) {
    EXPECT_TRUE(dyeColumnTarget(0, 1).main);
    EXPECT_FALSE(dyeColumnTarget(0, 1).accent);
    EXPECT_TRUE(dyeColumnTarget(2, 1).accent);
    EXPECT_FALSE(dyeColumnTarget(2, 1).main);
    EXPECT_TRUE(dyeColumnTarget(1, 1).main && dyeColumnTarget(1, 1).accent);
}

TEST(SbColors, LegacyDefaultsMigrate) {
    nlohmann::json j = BackpackData::create("u", Tier::Leather).toJson();
    j["main"]        = kLegacyDefaultMainColor;
    j["accent"]      = kLegacyDefaultAccentColor;
    auto d           = BackpackData::fromJson(j);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->mainColor, kDefaultMainColor);
    EXPECT_EQ(d->accentColor, kDefaultAccentColor);
    j["main"] = 0xFF112233u;
    d         = BackpackData::fromJson(j);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->mainColor, 0xFF112233u);
    EXPECT_EQ(BackpackData().mainColor, 0xFFCC613Au);
    EXPECT_EQ(BackpackData().accentColor, 0xFF622E1Au);
}

TEST(SbStack, SlotLimitOf) {
    EXPECT_EQ(stack::slotLimitOf(1.0), 64);
    EXPECT_EQ(stack::slotLimitOf(1.5), 96);
    EXPECT_EQ(stack::slotLimitOf(0.03125), 2);
    EXPECT_EQ(stack::slotLimitOf(0.001), 1);
    EXPECT_EQ(stack::slotLimitOf(2147483647.0), INT_MAX);
    std::vector<double> ms{2.0, 4.0, 0.125};
    EXPECT_EQ(stack::slotLimitOf(ms), 64);
    std::vector<double> omega{2147483647.0, 2.0};
    EXPECT_EQ(stack::slotLimitOf(omega), INT_MAX);
    std::vector<double> omegaDown{2147483647.0, 0.03125};
    EXPECT_EQ(stack::slotLimitOf(omegaDown), INT_MAX); // INT_MAX / 32 is still above INT_MAX / 64
}

TEST(SbStack, StackLimit) {
    EXPECT_EQ(stack::stackLimit(64, 64), 64);
    EXPECT_EQ(stack::stackLimit(16, 96), 24);
    EXPECT_EQ(stack::stackLimit(1, 96), 1);
    EXPECT_EQ(stack::stackLimit(1, 128), 2);
    EXPECT_EQ(stack::stackLimit(1, 1024), 16);
    EXPECT_EQ(stack::stackLimit(16, 8), 2);
    EXPECT_EQ(stack::stackLimit(1, 8), 1);
    EXPECT_EQ(stack::stackLimit(16, 3), 1);
    EXPECT_EQ(stack::stackLimit(64, INT_MAX), INT_MAX);
    EXPECT_EQ(stack::stackLimit(1, INT_MAX), INT_MAX / 64 + 63 / 64);
}

TEST(SbStack, FirstSlotNeedingMore) {
    BackpackInventory inv;
    inv.resize(3);
    inv.set(0, ItemStackData::of("minecraft:stone", 100));
    EXPECT_EQ(stack::firstSlotNeedingMore(inv, 2.0), -1);
    EXPECT_EQ(stack::firstSlotNeedingMore(inv, 1.5), 0); // 100 / 64 > 1.5
    inv.set(0, {});
    inv.set(2, ItemStackData::of("minecraft:iron_sword", 2, 1));
    EXPECT_EQ(stack::firstSlotNeedingMore(inv, 1.5), 2); // 2 / 1 > 1.5
    EXPECT_EQ(stack::firstSlotNeedingMore(inv, 2.0), -1);
    inv.set(2, ItemStackData::of("minecraft:iron_sword", 1, 1));
    EXPECT_EQ(stack::firstSlotNeedingMore(inv, 0.03125), -1); // single items always fit
}
