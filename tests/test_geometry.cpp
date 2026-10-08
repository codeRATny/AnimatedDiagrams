#include <gtest/gtest.h>

#include <array>

#include "ad/geometry.hpp"

using namespace ad;

TEST(Geometry, BorderPointHitsRectangleEdge) {
    const Vec2 c{0, 0};
    EXPECT_EQ(geom::borderPoint(c, 50, 20, {100, 0}), (Vec2{50, 0}));
    EXPECT_EQ(geom::borderPoint(c, 50, 20, {0, -100}), (Vec2{0, -20}));
    // диагональ упирается в ближнюю сторону (верх/низ при плоском прямоугольнике)
    const Vec2 d = geom::borderPoint(c, 50, 20, {100, 100});
    EXPECT_DOUBLE_EQ(d.x, 20);
    EXPECT_DOUBLE_EQ(d.y, 20);
    EXPECT_EQ(geom::borderPoint(c, 50, 20, c), c);
}

TEST(Geometry, PerpControlOffsetsToTheSide) {
    EXPECT_EQ(geom::perpControl({0, 0}, {100, 0}, 0), (Vec2{50, 0}));
    const Vec2 p = geom::perpControl({0, 0}, {100, 0}, 0.2);
    EXPECT_DOUBLE_EQ(p.x, 50);
    EXPECT_DOUBLE_EQ(p.y, 20);
    EXPECT_DOUBLE_EQ(geom::perpControl({0, 0}, {100, 0}, -0.2).y, -20);
}

TEST(Geometry, PullBackMovesTipTowardsSource) {
    const Vec2 r = geom::pullBack({0, 0}, {100, 0}, 12);
    EXPECT_DOUBLE_EQ(r.x, 88);
    EXPECT_DOUBLE_EQ(r.y, 0);
    // на коротком отрезке не «перепрыгивает» источник
    EXPECT_GT(geom::pullBack({0, 0}, {5, 0}, 12).x, 0);
}

TEST(Geometry, DistToSegment) {
    EXPECT_DOUBLE_EQ(geom::distToSegment({5, 5}, {0, 0}, {10, 0}), 5);
    EXPECT_DOUBLE_EQ(geom::distToSegment({-3, 4}, {0, 0}, {10, 0}), 5);
    EXPECT_DOUBLE_EQ(geom::distToSegment({1, 1}, {0, 0}, {0, 0}), std::sqrt(2.0));
}

TEST(Geometry, BoundsAccumulates) {
    Bounds b;
    EXPECT_TRUE(b.empty());
    b.add(Vec2{10, 20});
    b.add(Rect{0, 0, 5, 5});
    EXPECT_EQ(b.rect(), (Rect{0, 0, 10, 20}));
}

TEST(Path, LineFlattensToExactLength) {
    const FlatPath fp(Path::line({0, 0}, {30, 40}));
    EXPECT_DOUBLE_EQ(fp.length(), 50);
    const Vec2 mid = fp.pointAtFraction(0.5);
    EXPECT_DOUBLE_EQ(mid.x, 15);
    EXPECT_DOUBLE_EQ(mid.y, 20);
    EXPECT_EQ(fp.pointAtLength(-5), (Vec2{0, 0}));
    EXPECT_EQ(fp.pointAtLength(500), (Vec2{30, 40}));
}

TEST(Path, CatmullRomInterpolatesAllPoints) {
    const std::array pts{Vec2{0, 0}, Vec2{50, 40}, Vec2{120, -10}, Vec2{200, 30}};
    const Path p = Path::catmullRom(pts);
    ASSERT_EQ(p.segments().size(), 4u);  // move + 3 кубических сегмента
    EXPECT_EQ(p.startPoint(), pts.front());
    for (std::size_t i = 1; i < pts.size(); ++i) {
        EXPECT_EQ(p.segments()[i].kind, Path::Kind::Cubic);
        EXPECT_EQ(p.segments()[i].to, pts[i]);
    }
    // первый контрольный — в начальной точке (как в d3)
    EXPECT_EQ(p.segments()[1].c1, pts[0]);
    // последний второй контрольный — в конечной точке
    EXPECT_EQ(p.segments()[3].c2, pts[3]);
}

TEST(Path, CatmullRomDegenerateCases) {
    const std::array two{Vec2{0, 0}, Vec2{10, 0}};
    const Path p = Path::catmullRom(two);
    ASSERT_EQ(p.segments().size(), 2u);
    EXPECT_EQ(p.segments()[1].kind, Path::Kind::Line);
    EXPECT_TRUE(Path::catmullRom(std::span<const Vec2>{}).segments().empty());
}

TEST(Path, EndAndStartDirections) {
    const Path q = Path::quad({0, 0}, {50, 50}, {100, 0});
    const Vec2 e = q.endDirection();
    EXPECT_NEAR(e.x, std::sqrt(0.5), 1e-12);
    EXPECT_NEAR(e.y, -std::sqrt(0.5), 1e-12);
    const Vec2 s = q.startDirection();
    EXPECT_NEAR(s.x, std::sqrt(0.5), 1e-12);
    EXPECT_NEAR(s.y, std::sqrt(0.5), 1e-12);
}

TEST(FlatPath, QuadLengthIsCloseToAnalytic) {
    // парабола y = x^2/100 на [0,100]: длина ≈ 147.89
    const FlatPath fp(Path::quad({0, 0}, {50, 0}, {100, 100}), 64);
    EXPECT_NEAR(fp.length(), 147.89, 0.1);
}

TEST(FlatPath, DirectionIsDefinedAtEnds) {
    const FlatPath fp(Path::line({0, 0}, {10, 0}));
    EXPECT_EQ(fp.directionAt(10, true), (Vec2{1, 0}));
    EXPECT_EQ(fp.directionAt(0, false), (Vec2{-1, 0}));
}

TEST(FlatPath, PointAlongOffsetGoesUpOnScreen) {
    const FlatPath right(Path::line({0, 0}, {100, 0}));
    const FlatPath left(Path::line({100, 0}, {0, 0}));
    // независимо от направления пути положительный off — вверх (y меньше)
    EXPECT_NEAR(right.pointAlong(0.5, 10).y, -10, 1e-9);
    EXPECT_NEAR(left.pointAlong(0.5, 10).y, -10, 1e-9);
    EXPECT_NEAR(right.pointAlong(0.5, 10).x, 50, 1e-9);
}

TEST(FlatPath, DistanceTo) {
    const FlatPath fp(Path::line({0, 0}, {100, 0}));
    EXPECT_DOUBLE_EQ(fp.distanceTo({50, 7}), 7);
}
