#include <gtest/gtest.h>

#include "Export/ExportPlan.hpp"

using namespace ad;

TEST(ExportPlanTest, FrameTimesCoverWholeScene)
{
    const auto t = ExportFrameTimes(12000, 15);
    ASSERT_EQ(t.size(), 181u); // 180 intervals + the last frame
    EXPECT_DOUBLE_EQ(t.front(), 0);
    EXPECT_DOUBLE_EQ(t.back(), 12000);
    EXPECT_NEAR(t[1], 1000.0 / 15, 1e-9);
}

TEST(ExportPlanTest, FrameTimesEdgeCases)
{
    EXPECT_TRUE(ExportFrameTimes(1000, 0).empty());
    const auto t = ExportFrameTimes(10, 15); // shorter than one frame
    ASSERT_EQ(t.size(), 2u);
    EXPECT_DOUBLE_EQ(t.back(), 10);
}

TEST(ExportPlanTest, GeometryIsEvenAndScaled)
{
    const auto g = MakeExportGeometry({0, 0, 301, 151}, 2);
    EXPECT_EQ(g.px_w, 602);
    EXPECT_EQ(g.px_h, 302);
    EXPECT_EQ(g.px_w % 2, 0);
    EXPECT_EQ(g.px_h % 2, 0);
}

TEST(ExportPlanTest, GeometryClampsLargestSide)
{
    const auto g = MakeExportGeometry({0, 0, 4000, 1000}, 10, 12000);
    EXPECT_LE(g.px_w, 12000);
    EXPECT_NEAR(static_cast<double>(g.px_w) / g.px_h, 4.0, 0.01);
}

TEST(ExportPlanTest, GeometryHasMinimumSize)
{
    const auto g = MakeExportGeometry({0, 0, 1, 1}, 1);
    EXPECT_EQ(g.world.w, 40);
    EXPECT_EQ(g.px_w, 40);
}
