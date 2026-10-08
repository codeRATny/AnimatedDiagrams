#include <gtest/gtest.h>

#include "Engine/Shapes.hpp"
#include "Model/Catalog.hpp"

using namespace ad;

TEST(ShapesTest, EveryShapeHasClosedOutlineInsideItsBox)
{
    const Rect r{10, 20, 160, 80};
    for (const auto &s : Shapes())
    {
        const Path p = ShapeOutline(s.id, r, DefaultCornerRadius(s.id), "M0 0 L1 0 L1 1 Z");
        ASSERT_FALSE(p.Empty()) << s.id;
        Bounds         b;
        const FlatPath flat(p, 16);
        for (const Vec2 pt : flat.Points())
        {
            b.Add(pt);
        }
        const Rect box = b.ToRect();
        EXPECT_GE(box.x, r.x - 1e-6) << s.id;
        EXPECT_LE(box.Right(), r.Right() + 1e-6) << s.id;
        EXPECT_GE(box.y, r.y - 1e-6) << s.id;
        EXPECT_LE(box.Bottom(), r.Bottom() + 1e-6) << s.id;
    }
}

TEST(ShapesTest, CustomPathFallsBackOnError)
{
    const Rect r{0, 0, 100, 50};
    const Path ok = ShapeOutline("custom", r, 0, "M0 0 L1 0 L0.5 1 Z");
    EXPECT_EQ(ok.Segments()[1].to, (Vec2{100, 0}));
    const Path bad = ShapeOutline("custom", r, 0, "garbage");
    EXPECT_FALSE(bad.Empty());
}

TEST(ShapesTest, DecorationsAndBorders)
{
    const Rect r{0, 0, 100, 60};
    EXPECT_FALSE(ShapeDecoration("cylinder", r).Empty());
    EXPECT_FALSE(ShapeDecoration("queue", r).Empty());
    EXPECT_TRUE(ShapeDecoration("rect", r).Empty());

    const Vec2 d = ShapeBorderPoint("diamond", r, {50, -100}, 0);
    EXPECT_NEAR(d.y, 0, 1e-9); // top vertex
    const Vec2 e = ShapeBorderPoint("ellipse", r, {200, 30}, 0);
    EXPECT_NEAR(e.x, 100, 1e-9);
    const Vec2 b = ShapeBorderPoint("rect", r, {50, 500}, 4);
    EXPECT_NEAR(b.y, 64, 1e-9);
    EXPECT_TRUE(IsCenteredShape("diamond"));
    EXPECT_FALSE(IsCenteredShape("rounded"));
    EXPECT_DOUBLE_EQ(DefaultCornerRadius("rounded"), 14);
}
