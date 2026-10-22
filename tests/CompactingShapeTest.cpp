#include <gtest/gtest.h>

#include "upgrade/compacting/CompactingRecipes.h"

using namespace abp::upgrade::compacting;

TEST(CompactingShape, ParsesSbShapes) {
    auto ring = Shape::parse("111/101/111");
    ASSERT_TRUE(ring.has_value());
    EXPECT_EQ(ring->width, 3);
    EXPECT_EQ(ring->height, 3);
    EXPECT_EQ(ring->count, 8);
    EXPECT_FALSE(ring->isFilled(1, 1));
    EXPECT_TRUE(ring->isFilled(0, 0));
    EXPECT_TRUE(ring->isFilled(2, 2));
    auto wide = Shape::parse("11/11");
    ASSERT_TRUE(wide.has_value());
    EXPECT_EQ(*wide, Shape::full(2));
    EXPECT_EQ(Shape::full(3).count, 9);
    EXPECT_TRUE(ring->fitsWithin(3, 3));
    EXPECT_FALSE(ring->fitsWithin(2, 2));
}

TEST(CompactingShape, RejectsInvalidShapes) {
    EXPECT_FALSE(Shape::parse("").has_value());
    EXPECT_FALSE(Shape::parse("1111").has_value());        // wider than 3
    EXPECT_FALSE(Shape::parse("1/1/1/1").has_value());     // taller than 3
    EXPECT_FALSE(Shape::parse("11/1").has_value());        // ragged
    EXPECT_FALSE(Shape::parse("12/11").has_value());       // bad character
    EXPECT_FALSE(Shape::parse("10/00").has_value());       // SC: at least two ingredients
    EXPECT_FALSE(Shape::parse("/11").has_value());
}

TEST(CompactingShape, ParsesOverrides) {
    auto o = ShapeOverride::parse("some_mod:tiny_coal=111/101/111");
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->item, "some_mod:tiny_coal");
    EXPECT_EQ(o->shape.count, 8);
    EXPECT_FALSE(ShapeOverride::parse("tiny_coal=11/11").has_value()); // needs a namespace
    EXPECT_FALSE(ShapeOverride::parse("Mod:Item=11/11").has_value()); // lowercase only
    EXPECT_FALSE(ShapeOverride::parse("a:b=1").has_value());
}
