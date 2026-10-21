#include <gtest/gtest.h>

#include <vector>

#include "core/Appearance.h"

using namespace abp::core;

TEST(Appearance, DyeLookup) {
    EXPECT_EQ(dyeFromItemId("minecraft:red_dye"), DyeColor::Red);
    EXPECT_EQ(dyeFromItemId("minecraft:light_blue_dye"), DyeColor::LightBlue);
    EXPECT_EQ(dyeFromItemId("minecraft:light_gray_dye"), DyeColor::LightGray);
    EXPECT_EQ(dyeFromItemId("minecraft:dye"), std::nullopt);
    EXPECT_EQ(dyeFromItemId("minecraft:red_wool"), std::nullopt);
    EXPECT_EQ(dyeFromItemId("other:red_dye"), std::nullopt);
    for (auto const& d : kDyes) {
        EXPECT_EQ(dyeFromItemId("minecraft:" + std::string(d.id) + "_dye"), d.color);
        EXPECT_EQ(dyeFromColor(d.argb), d.color);
        EXPECT_EQ(dyeFromColor(d.argb & 0xFFFFFF), d.color) << "alpha ignored";
    }
    EXPECT_EQ(dyeFromColor(0xFF123456), std::nullopt);
    EXPECT_EQ(dyeInfo(DyeColor::Black).argb, 0xFF1D1D21u);
}

TEST(Appearance, MixColors) {
    EXPECT_EQ(mixColors({}), std::nullopt);
    std::vector<std::uint32_t> one{0x00B02E26};
    EXPECT_EQ(mixColors(one), 0xFFB02E26u) << "single colour keeps itself (with alpha)";
    std::vector<std::uint32_t> same{0xFF3C44AA, 0xFF3C44AA};
    EXPECT_EQ(mixColors(same), 0xFF3C44AAu);
    // Red + white: average (255,127,127) / max 255 -> stays (255,127,127).
    std::vector<std::uint32_t> rw{0xFFFF0000, 0xFFFFFFFF};
    EXPECT_EQ(mixColors(rw), 0xFFFF7F7Fu);
    // Pure red + pure blue: average (127,0,127), avg max 255 -> scaled to (255,0,255).
    std::vector<std::uint32_t> rb{0xFFFF0000, 0xFF0000FF};
    EXPECT_EQ(mixColors(rb), 0xFFFF00FFu);
    std::vector<std::uint32_t> black{0xFF000000, 0xFF000000};
    EXPECT_EQ(mixColors(black), 0xFF000000u);
}

TEST(Appearance, Hex) {
    EXPECT_EQ(colorHex(0xFFA0703C), "#A0703C");
    EXPECT_EQ(colorHex(0x00000001), "#000001");
}

TEST(Appearance, DyeTarget) {
    // 3x3 grid, backpack in the centre (4).
    EXPECT_EQ(dyeTargetFor(3, 4), DyeTarget::Main);   // left
    EXPECT_EQ(dyeTargetFor(5, 4), DyeTarget::Main);   // right
    EXPECT_EQ(dyeTargetFor(1, 4), DyeTarget::Accent); // above
    EXPECT_EQ(dyeTargetFor(7, 4), DyeTarget::Accent); // below
    EXPECT_EQ(dyeTargetFor(0, 8), DyeTarget::Accent); // diagonal, other row
    EXPECT_EQ(dyeTargetFor(2, 0), DyeTarget::Main);   // same row, far right
}

TEST(Appearance, FillStats) {
    BackpackInventory inv(4);
    auto              empty = fillStats(inv);
    EXPECT_EQ(empty.usedSlots, 0);
    EXPECT_EQ(empty.totalSlots, 4);
    EXPECT_EQ(empty.items, 0);
    EXPECT_EQ(empty.capacity, 4 * 64);
    EXPECT_EQ(empty.percent, 0);

    inv.set(0, ItemStackData::of("minecraft:stone", 64));
    inv.set(1, ItemStackData::of("minecraft:ender_pearl", 16, 16));
    auto s = fillStats(inv);
    EXPECT_EQ(s.usedSlots, 2);
    EXPECT_EQ(s.items, 80);
    EXPECT_EQ(s.capacity, 64 + 16 + 64 + 64);
    EXPECT_EQ(s.percent, 80 * 100 / 208);

    BackpackInventory big(100);
    big.set(0, ItemStackData::of("minecraft:stone", 1));
    EXPECT_EQ(fillStats(big).percent, 1) << "not empty -> at least 1%";

    // Stack upgrades raise the per-slot limit.
    BackpackInventory stacked(2);
    stacked.addLimitModifier("test", 0, [](int, ItemStackData const&, int limit) { return limit * 4; });
    stacked.set(0, ItemStackData::of("minecraft:stone", 256));
    auto st = fillStats(stacked);
    EXPECT_EQ(st.capacity, 256 * 2);
    EXPECT_EQ(st.percent, 50);
}
