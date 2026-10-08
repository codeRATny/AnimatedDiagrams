#include <gtest/gtest.h>

#include "ad/export_plan.hpp"

using namespace ad;

TEST(ExportPlan, FrameTimesCoverWholeScene) {
    const auto t = exportFrameTimes(12000, 15);
    ASSERT_EQ(t.size(), 181u);  // 180 интервалов + последний кадр
    EXPECT_DOUBLE_EQ(t.front(), 0);
    EXPECT_DOUBLE_EQ(t.back(), 12000);
    EXPECT_NEAR(t[1], 1000.0 / 15, 1e-9);
}

TEST(ExportPlan, FrameTimesEdgeCases) {
    EXPECT_TRUE(exportFrameTimes(1000, 0).empty());
    const auto t = exportFrameTimes(10, 15);  // короче одного кадра
    ASSERT_EQ(t.size(), 2u);
    EXPECT_DOUBLE_EQ(t.back(), 10);
}

TEST(ExportPlan, GeometryIsEvenAndScaled) {
    const auto g = exportGeometry({0, 0, 301, 151}, 2);
    EXPECT_EQ(g.pxW, 602);
    EXPECT_EQ(g.pxH, 302);
    EXPECT_EQ(g.pxW % 2, 0);
    EXPECT_EQ(g.pxH % 2, 0);
}

TEST(ExportPlan, GeometryClampsLargestSide) {
    const auto g = exportGeometry({0, 0, 4000, 1000}, 10, 12000);
    EXPECT_LE(g.pxW, 12000);
    EXPECT_NEAR(static_cast<double>(g.pxW) / g.pxH, 4.0, 0.01);
}

TEST(ExportPlan, GeometryHasMinimumSize) {
    const auto g = exportGeometry({0, 0, 1, 1}, 1);
    EXPECT_EQ(g.world.w, 40);
    EXPECT_EQ(g.pxW, 40);
}
