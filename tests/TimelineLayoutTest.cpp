#include <gtest/gtest.h>

#include "Model/Sample.hpp"
#include "Timeline/TimelineLayout.hpp"

using namespace ad;

namespace
{

Step Bar(std::string id, double start, double dur)
{
    Step s;
    s.id       = std::move(id);
    s.start    = start;
    s.duration = dur;
    return s;
}

int LaneOf(const std::vector<LaneAssignment> &v, std::string_view id)
{
    for (const auto &l : v)
    {
        if (l.step_id == id)
        {
            return l.lane;
        }
    }
    return -1;
}

} // namespace

TEST(TimelineLayoutTest, PackLanes)
{
    const auto lanes = PackLanes({Bar("a", 0, 100), Bar("b", 100, 100), Bar("c", 50, 100), Bar("d", 260, 10)});
    EXPECT_EQ(LaneOf(lanes, "a"), 0);
    EXPECT_EQ(LaneOf(lanes, "c"), 1);
    EXPECT_EQ(LaneOf(lanes, "b"), 0);
    EXPECT_EQ(LaneCount(lanes), 2);
    EXPECT_EQ(LaneCount({}), 0);
}

TEST(TimelineLayoutTest, Ticks)
{
    EXPECT_EQ(NiceTicks(0, 10, 5), (std::vector<double>{0, 2, 4, 6, 8, 10}));
    const auto frac = NiceTicks(0, 1, 4);
    ASSERT_EQ(frac.size(), 6U);
    EXPECT_NEAR(frac[3], 0.6, 1e-12);
    EXPECT_DOUBLE_EQ(TickStep(0, 24, 4), 5);
    EXPECT_TRUE(NiceTicks(5, 5, 3).empty());
    EXPECT_EQ(FormatTick(4, 2), "4");
    EXPECT_EQ(FormatTick(0.25, 0.05), "0.25");
}

TEST(TimelineLayoutTest, TitlesColorsTime)
{
    Model m = SampleModel();
    EXPECT_EQ(StepTitle(m, *m.FindStep("s1")), "Service A → Service B · request");
    EXPECT_EQ(StepTitle(m, *m.FindStep("s3")), "⏱ Service A · 5s");
    EXPECT_EQ(StepTitle(m, *m.FindStep("s7")), "✷ Service A · Pulse");
    EXPECT_EQ(StepColor(*m.FindStep("s4")), Color::Rgb(0xfbbf24));
    EXPECT_DOUBLE_EQ(ContentEnd(m), 12000);
    m.scenario.steps.push_back(Bar("late", 15000, 1000));
    EXPECT_DOUBLE_EQ(ContentEnd(m), 16000);
    EXPECT_DOUBLE_EQ(SnapTime(1234), 1250);
    EXPECT_EQ(FormatTime(1500), "1.50s");
    EXPECT_EQ(FormatTime(12000), "12.0s");
}
