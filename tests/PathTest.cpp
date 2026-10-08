#include <gtest/gtest.h>

#include <array>

#include "Common/Exceptions.hpp"
#include "Geometry/Path.hpp"

using namespace ad;

TEST(PathTest, LineFlattensToExactLength)
{
    const FlatPath fp(Path::Line({0, 0}, {30, 40}));
    EXPECT_DOUBLE_EQ(fp.Length(), 50);
    EXPECT_EQ(fp.PointAtFraction(0.5), (Vec2{15, 20}));
    EXPECT_EQ(fp.PointAtLength(-5), (Vec2{0, 0}));
    EXPECT_EQ(fp.PointAtLength(500), (Vec2{30, 40}));
}

TEST(PathTest, CatmullRomInterpolatesAllPoints)
{
    const std::array pts{Vec2{0, 0}, Vec2{50, 40}, Vec2{120, -10}, Vec2{200, 30}};
    const Path       p = Path::CatmullRom(pts);
    ASSERT_EQ(p.Segments().size(), 4U);
    for (size_t i = 1; i < pts.size(); ++i)
    {
        EXPECT_EQ(p.Segments()[i].kind, Path::Kind::Cubic);
        EXPECT_EQ(p.Segments()[i].to, pts[i]);
    }
    EXPECT_EQ(p.Segments()[1].c1, pts[0]); // first control point at the start (as d3)
    EXPECT_EQ(p.Segments()[3].c2, pts[3]);
}

TEST(PathTest, CatmullRomDegenerateCases)
{
    const std::array two{Vec2{0, 0}, Vec2{10, 0}};
    EXPECT_EQ(Path::CatmullRom(two).Segments()[1].kind, Path::Kind::Line);
    EXPECT_TRUE(Path::CatmullRom(std::span<const Vec2>{}).Segments().empty());
}

TEST(PathTest, Directions)
{
    const Path q = Path::Quad({0, 0}, {50, 50}, {100, 0});
    EXPECT_NEAR(q.EndDirection().x, std::sqrt(0.5), 1e-12);
    EXPECT_NEAR(q.EndDirection().y, -std::sqrt(0.5), 1e-12);
    EXPECT_NEAR(q.StartDirection().y, std::sqrt(0.5), 1e-12);
}

TEST(PathTest, BuildersAreClosed)
{
    const Path e = Path::Ellipse({0, 0}, 10, 5);
    EXPECT_EQ(e.Segments().back().kind, Path::Kind::Close);
    EXPECT_NEAR(FlatPath(e, 64).Length(), 48.44, 0.1); // ellipse perimeter
    const Path r = Path::RoundedRect(0, 0, 100, 50, 10);
    EXPECT_EQ(r.Segments().back().kind, Path::Kind::Close);
    EXPECT_NEAR(FlatPath(r, 64).Length(), 2 * (80 + 30) + 2 * 3.14159265 * 10, 0.1);
    const Path sq = Path::RoundedRect(0, 0, 10, 10, 0);
    EXPECT_DOUBLE_EQ(FlatPath(sq).Length(), 40);
}

TEST(PathTest, MappedTo)
{
    const Path unit = Path::Line({0, 0}, {1, 1});
    const Path m    = unit.MappedTo(10, 20, 100, 50);
    EXPECT_EQ(m.StartPoint(), (Vec2{10, 20}));
    EXPECT_EQ(m.EndPoint(), (Vec2{110, 70}));
}

TEST(PathTest, ParseSvgPathAbsoluteAndRelative)
{
    const Path  p = ParseSvgPath("M0,0 L1 0 l0,1 H0 V0.5 Q0.5 0.6 0.5 0.5 C0 0 0 0 0 0 z");
    const auto &s = p.Segments();
    ASSERT_EQ(s.size(), 8U);
    EXPECT_EQ(s[1].to, (Vec2{1, 0}));
    EXPECT_EQ(s[2].to, (Vec2{1, 1}));
    EXPECT_EQ(s[3].to, (Vec2{0, 1}));
    EXPECT_EQ(s[4].to, (Vec2{0, 0.5}));
    EXPECT_EQ(s[5].kind, Path::Kind::Quad);
    EXPECT_EQ(s[6].kind, Path::Kind::Cubic);
    EXPECT_EQ(s[7].kind, Path::Kind::Close);
    EXPECT_EQ(s[7].to, (Vec2{0, 0}));
}

TEST(PathTest, ParseSvgPathImplicitLineTo)
{
    const Path p = ParseSvgPath("m 0 0 1 0 0 1 -1e0 0 Z");
    ASSERT_EQ(p.Segments().size(), 5U);
    EXPECT_EQ(p.Segments()[3].to, (Vec2{0, 1}));
}

TEST(PathTest, ParseSvgPathErrors)
{
    EXPECT_THROW(ParseSvgPath("0 0 L 1 1"), ParseError);
    EXPECT_THROW(ParseSvgPath("M 0 0 A 1 1 0 0 0 1 1"), ParseError);
    EXPECT_THROW(ParseSvgPath("M 0"), ParseError);
    EXPECT_TRUE(ParseSvgPath("").Segments().empty());
}

TEST(PathTest, FlatPathQuadLength)
{
    const FlatPath fp(Path::Quad({0, 0}, {50, 0}, {100, 100}), 64);
    EXPECT_NEAR(fp.Length(), 147.89, 0.1);
}

TEST(PathTest, FlatPathDirectionAtEnds)
{
    const FlatPath fp(Path::Line({0, 0}, {10, 0}));
    EXPECT_EQ(fp.DirectionAt(10, true), (Vec2{1, 0}));
    EXPECT_EQ(fp.DirectionAt(0, false), (Vec2{-1, 0}));
}

TEST(PathTest, FlatPathPointAlongGoesUp)
{
    EXPECT_NEAR(FlatPath(Path::Line({0, 0}, {100, 0})).PointAlong(0.5, 10).y, -10, 1e-9);
    EXPECT_NEAR(FlatPath(Path::Line({100, 0}, {0, 0})).PointAlong(0.5, 10).y, -10, 1e-9);
    EXPECT_DOUBLE_EQ(FlatPath(Path::Line({0, 0}, {100, 0})).DistanceTo({50, 7}), 7);
}

TEST(PathTest, FlatPathUsesFirstSubpathOnly)
{
    Path p = Path::Line({0, 0}, {10, 0});
    p.MoveTo({100, 100});
    p.LineTo({200, 100});
    EXPECT_DOUBLE_EQ(FlatPath(p).Length(), 10);
}
