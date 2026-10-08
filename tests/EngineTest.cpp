#include <gtest/gtest.h>

#include "Engine/Engine.hpp"
#include "Model/Sample.hpp"

using namespace ad;

namespace
{

Step StateStep(std::string id, std::string node, std::string st, double start, double dur)
{
    Step s;
    s.id       = std::move(id);
    s.type     = StepType::State;
    s.node_id  = std::move(node);
    s.state    = std::move(st);
    s.start    = start;
    s.duration = dur;
    return s;
}

} // namespace

TEST(EngineTest, StateIsHalfOpenInterval)
{
    Model m          = SampleModel();
    m.scenario.steps = {StateStep("a", "svcA", "down", 1000, 1000)};
    EXPECT_EQ(StateAt(m, "svcA", 999), nullptr);
    EXPECT_NE(StateAt(m, "svcA", 1000), nullptr);
    EXPECT_EQ(StateAt(m, "svcA", 2000), nullptr);
}

TEST(EngineTest, LatestStartedStateWins)
{
    Model m          = SampleModel();
    m.scenario.steps = {StateStep("a", "svcA", "down", 0, 5000), StateStep("b", "svcA", "busy", 1000, 1000)};
    EXPECT_EQ(StateAt(m, "svcA", 1500)->id, "b");
    EXPECT_EQ(StateAt(m, "svcA", 2500)->id, "a");
}

TEST(EngineTest, StateHoldsOnLastFrame)
{
    const Model  m   = SampleModel();
    const double end = m.scenario.duration;
    EXPECT_EQ(StateAt(m, "svcB", end)->state, "down");
    EXPECT_EQ(StateAt(m, "svcA", end)->state, "success");
}

TEST(EngineTest, ActiveSteps)
{
    const Model m = SampleModel();
    EXPECT_EQ(ActiveSteps(m, 300).size(), 1U);
    EXPECT_TRUE(ActiveSteps(m, 0).empty());
    EXPECT_EQ(ActiveSteps(m, 1400).size(), 2U);
}

TEST(EngineTest, ResolveNodeStateUsesStyleInDefaultState)
{
    NodeStyle style;
    style.fill     = "#123456";
    style.stroke   = "#abcdef";
    const auto def = ResolveNodeState(nullptr, style);
    EXPECT_EQ(def.fill, Color::Rgb(0x123456));
    EXPECT_EQ(def.ring, Color::Rgb(0xabcdef));

    Step s       = StateStep("x", "n", "down", 0, 1);
    s.label      = "503";
    s.color      = "#ef4444";
    const auto r = ResolveNodeState(&s, style);
    EXPECT_EQ(r.label, "503");
    EXPECT_TRUE(r.custom_label);
    EXPECT_EQ(r.fill, (Color{121, 35, 35}));
}

TEST(EngineTest, ResolveStyleAndShape)
{
    Model m         = SampleModel();
    m.nodes[0].type = "db";
    EXPECT_EQ(ResolveShape(ResolveNodeStyle(m, m.nodes[0])), "cylinder");
    m.nodes[0].style.shape = "diamond";
    EXPECT_EQ(ResolveShape(ResolveNodeStyle(m, m.nodes[0])), "diamond");
    m.nodes[0].style.shape = "star";
    EXPECT_EQ(ResolveShape(ResolveNodeStyle(m, m.nodes[0])), "rounded");
}

TEST(EngineTest, StraightEdgeEndsBeforeArrow)
{
    Model m      = SampleModel();
    m.nodes[1].y = m.nodes[0].y;
    Edge &e      = *m.FindEdge("eAB");
    e.curve      = 0;
    const auto g = ComputeEdgeGeometry(m, e);
    ASSERT_TRUE(g.has_value());
    EXPECT_DOUBLE_EQ(g->raw_start.x, m.nodes[0].x + m.nodes[0].w + 4);
    EXPECT_DOUBLE_EQ(g->raw_end.x, m.nodes[1].x - 4);
    EXPECT_DOUBLE_EQ(g->end.x, m.nodes[1].x - 4 - kArrowLen);
    EXPECT_EQ(g->start, g->raw_start);
}

TEST(EngineTest, ArrowsAndRouting)
{
    Model m             = SampleModel();
    Edge &e             = *m.FindEdge("eAB");
    e.style.arrow_start = "triangle";
    e.style.arrow_end   = "none";
    auto g              = ComputeEdgeGeometry(m, e);
    ASSERT_TRUE(g.has_value());
    EXPECT_TRUE(g->arrow_start);
    EXPECT_FALSE(g->arrow_end);
    EXPECT_NEAR(Distance(g->start, g->raw_start), kArrowLen, 1e-9);
    EXPECT_EQ(g->end, g->raw_end);

    e.style.routing = "orthogonal";
    g               = ComputeEdgeGeometry(m, e);
    ASSERT_EQ(g->points.size(), 4U); // start, two elbows, end
    EXPECT_DOUBLE_EQ(g->points[1].x, g->points[2].x);
    EXPECT_EQ(g->path.Segments().back().kind, Path::Kind::Line);

    e.style.routing = "straight";
    g               = ComputeEdgeGeometry(m, e);
    EXPECT_EQ(g->path.Segments().back().kind, Path::Kind::Line); // curve ignored
}

TEST(EngineTest, EllipseBorderForEllipseShapes)
{
    Model m                = SampleModel();
    m.nodes[1].style.shape = "ellipse";
    m.nodes[1].y           = m.nodes[0].y;
    Edge &e                = *m.FindEdge("eAB");
    e.curve                = 0;
    const auto g           = ComputeEdgeGeometry(m, e);
    EXPECT_NEAR(g->raw_end.x, m.nodes[1].x - 4, 1e-9);
    EXPECT_NEAR(g->raw_end.y, m.nodes[1].Center().y, 1e-9);
}

TEST(EngineTest, PortsWaypointsAndNearestSegment)
{
    Model m = SampleModel();
    m.nodes[0].ports.push_back({"p", 150, 10});
    Edge &e      = *m.FindEdge("eAB");
    e.from_port  = "p";
    e.waypoints  = {{300, 300}, {600, 300}};
    const auto g = ComputeEdgeGeometry(m, e);
    EXPECT_EQ(g->raw_start, (Vec2{230, 130}));
    ASSERT_EQ(g->points.size(), 4U);
    EXPECT_EQ(NearestSegmentIndex(m, e, {450, 305}), 1U);
}
