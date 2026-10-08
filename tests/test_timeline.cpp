#include <gtest/gtest.h>

#include "ad/sample.hpp"
#include "ad/timeline.hpp"

using namespace ad;

namespace {

Step bar(std::string id, double start, double dur) {
    Step s;
    s.id = std::move(id);
    s.start = start;
    s.duration = dur;
    return s;
}

int laneOf(const std::vector<LaneAssignment>& v, std::string_view id) {
    for (const auto& l : v)
        if (l.stepId == id) return l.lane;
    return -1;
}

}  // namespace

TEST(Timeline, PackLanesSharesLaneWhenNoOverlap) {
    const std::vector<Step> steps{bar("a", 0, 100), bar("b", 100, 100), bar("c", 50, 100), bar("d", 260, 10)};
    const auto lanes = packLanes(steps);
    EXPECT_EQ(laneOf(lanes, "a"), 0);
    EXPECT_EQ(laneOf(lanes, "c"), 1);
    EXPECT_EQ(laneOf(lanes, "b"), 0);
    EXPECT_EQ(laneOf(lanes, "d"), 0);
    EXPECT_EQ(laneCount(lanes), 2);
    EXPECT_EQ(laneCount({}), 0);
}

TEST(Timeline, NiceTicks) {
    EXPECT_EQ(niceTicks(0, 10, 5), (std::vector<double>{0, 2, 4, 6, 8, 10}));
    const auto frac = niceTicks(0, 1, 4);
    ASSERT_EQ(frac.size(), 6u);
    for (std::size_t i = 0; i < frac.size(); ++i) EXPECT_NEAR(frac[i], 0.2 * static_cast<double>(i), 1e-12);
    EXPECT_DOUBLE_EQ(tickStep(0, 100, 10), 10);
    EXPECT_DOUBLE_EQ(tickStep(0, 13, 10), 1);
    EXPECT_DOUBLE_EQ(tickStep(0, 24, 4), 5);
    EXPECT_TRUE(niceTicks(5, 5, 3).empty());
}

TEST(Timeline, FormatTick) {
    EXPECT_EQ(formatTick(4, 2), "4");
    EXPECT_EQ(formatTick(0.5, 0.5), "0.5");
    EXPECT_EQ(formatTick(0.25, 0.05), "0.25");
}

TEST(Timeline, ContentEndCoversStepsBeyondScene) {
    Model m = sampleModel();
    EXPECT_DOUBLE_EQ(contentEnd(m), 12000);
    m.scenario.steps.push_back(bar("late", 15000, 1000));
    EXPECT_DOUBLE_EQ(contentEnd(m), 16000);
}

TEST(Timeline, StepTitles) {
    const Model m = sampleModel();
    EXPECT_EQ(stepTitle(m, *m.step("s1")), "Сервис A → Сервис B · запрос");
    EXPECT_EQ(stepTitle(m, *m.step("s3")), "⏱ Сервис A · 5с");
    EXPECT_EQ(stepTitle(m, *m.step("s2")), "⇄ Сервис B → Недоступен");
    EXPECT_EQ(stepTitle(m, *m.step("s7")), "✷ Сервис A");
    EXPECT_EQ(stepTitle(m, *m.step("s2b")), "✎ Сервис B недоступен");
}

TEST(Timeline, StepColors) {
    const Model m = sampleModel();
    EXPECT_EQ(stepColor(*m.step("s4")), Color::rgb(0xfbbf24));  // retry
    EXPECT_EQ(stepColor(*m.step("s2")), Color::rgb(0xef4444));  // down
}

TEST(Timeline, SnapAndFormatTime) {
    EXPECT_DOUBLE_EQ(snapTime(1234), 1250);
    EXPECT_DOUBLE_EQ(snapTime(-80), 0);
    EXPECT_EQ(formatTime(1500), "1.50с");
    EXPECT_EQ(formatTime(12000), "12.0с");
}
