#include <gtest/gtest.h>

#include "Geometry/Geometry.hpp"

using namespace ad;

TEST(GeometryTest, BorderPointHitsRectangleEdge)
{
    const Vec2 c{0, 0};
    EXPECT_EQ(geom::BorderPoint(c, 50, 20, {100, 0}), (Vec2{50, 0}));
    EXPECT_EQ(geom::BorderPoint(c, 50, 20, {0, -100}), (Vec2{0, -20}));
    const Vec2 d = geom::BorderPoint(c, 50, 20, {100, 100});
    EXPECT_DOUBLE_EQ(d.x, 20);
    EXPECT_DOUBLE_EQ(d.y, 20);
    EXPECT_EQ(geom::BorderPoint(c, 50, 20, c), c);
}

TEST(GeometryTest, EllipseBorderPoint)
{
    const Vec2 p = geom::EllipseBorderPoint({0, 0}, 50, 20, {100, 0});
    EXPECT_DOUBLE_EQ(p.x, 50);
    EXPECT_DOUBLE_EQ(p.y, 0);
    const Vec2 q = geom::EllipseBorderPoint({0, 0}, 50, 50, {10, 10});
    EXPECT_NEAR(Length(q), 50, 1e-9);
}

TEST(GeometryTest, PerpControlOffsetsToTheSide)
{
    EXPECT_EQ(geom::PerpControl({0, 0}, {100, 0}, 0), (Vec2{50, 0}));
    EXPECT_DOUBLE_EQ(geom::PerpControl({0, 0}, {100, 0}, 0.2).y, 20);
    EXPECT_DOUBLE_EQ(geom::PerpControl({0, 0}, {100, 0}, -0.2).y, -20);
}

TEST(GeometryTest, PullBackMovesTipTowardsSource)
{
    const Vec2 r = geom::PullBack({0, 0}, {100, 0}, 12);
    EXPECT_DOUBLE_EQ(r.x, 88);
    EXPECT_GT(geom::PullBack({0, 0}, {5, 0}, 12).x, 0); // never jumps over the source
}

TEST(GeometryTest, DistToSegment)
{
    EXPECT_DOUBLE_EQ(geom::DistToSegment({5, 5}, {0, 0}, {10, 0}), 5);
    EXPECT_DOUBLE_EQ(geom::DistToSegment({-3, 4}, {0, 0}, {10, 0}), 5);
    EXPECT_DOUBLE_EQ(geom::DistToSegment({1, 1}, {0, 0}, {0, 0}), std::sqrt(2.0));
}

TEST(GeometryTest, BoundsAndRect)
{
    Bounds b;
    EXPECT_TRUE(b.Empty());
    b.Add(Vec2{10, 20});
    b.Add(Rect{0, 0, 5, 5});
    EXPECT_EQ(b.ToRect(), (Rect{0, 0, 10, 20}));
    EXPECT_EQ((Rect{0, 0, 10, 10}).United({20, 20, 5, 5}), (Rect{0, 0, 25, 25}));
    EXPECT_TRUE((Rect{0, 0, 10, 10}).Contains({10, 10}));
    EXPECT_EQ((Rect{0, 0, 10, 10}).Adjusted(2), (Rect{-2, -2, 14, 14}));
    EXPECT_EQ(Normalized({0, 0}), (Vec2{1, 0}));
    EXPECT_EQ(Normalized({0, 5}), (Vec2{0, 1}));
}
