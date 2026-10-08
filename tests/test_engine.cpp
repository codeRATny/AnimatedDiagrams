#include <gtest/gtest.h>

#include "ad/engine.hpp"
#include "ad/sample.hpp"

using namespace ad;

namespace {

Step stateStep(std::string id, std::string node, std::string st, double start, double dur) {
    Step s;
    s.id = std::move(id);
    s.type = StepType::State;
    s.nodeId = std::move(node);
    s.state = std::move(st);
    s.start = start;
    s.duration = dur;
    return s;
}

}  // namespace

TEST(Engine, StateIsHalfOpenInterval) {
    Model m = sampleModel();
    m.scenario.steps = {stateStep("a", "svcA", "down", 1000, 1000)};
    EXPECT_EQ(stateAt(m, "svcA", 999), nullptr);
    EXPECT_NE(stateAt(m, "svcA", 1000), nullptr);
    EXPECT_NE(stateAt(m, "svcA", 1999), nullptr);
    EXPECT_EQ(stateAt(m, "svcA", 2000), nullptr);
    EXPECT_EQ(stateAt(m, "svcB", 1500), nullptr);
}

TEST(Engine, LatestStartedStateWins) {
    Model m = sampleModel();
    m.scenario.steps = {stateStep("a", "svcA", "down", 0, 5000), stateStep("b", "svcA", "busy", 1000, 1000)};
    EXPECT_EQ(stateAt(m, "svcA", 1500)->id, "b");
    EXPECT_EQ(stateAt(m, "svcA", 2500)->id, "a");
}

TEST(Engine, StateHoldsOnLastFrameOfScene) {
    // баг веб-версии: на последнем кадре (t == длительность) все состояния сбрасывались
    const Model m = sampleModel();
    const double end = m.scenario.duration;
    ASSERT_NE(stateAt(m, "svcB", end), nullptr);
    EXPECT_EQ(stateAt(m, "svcB", end)->state, "down");
    EXPECT_EQ(stateAt(m, "svcC", end)->state, "active");
    EXPECT_EQ(stateAt(m, "svcA", end)->state, "success");
}

TEST(Engine, ActiveStepsInclusive) {
    const Model m = sampleModel();
    const auto at = activeSteps(m, 300);
    ASSERT_EQ(at.size(), 1u);
    EXPECT_EQ(at[0]->id, "s1");
    EXPECT_EQ(activeSteps(m, 0).size(), 0u);
    EXPECT_EQ(activeSteps(m, 1400).size(), 2u);  // конец s1 и начало s2
}

TEST(Engine, ResolveNodeStateDefaultsAndOverrides) {
    const ResolvedState def = resolveNodeState(nullptr);
    EXPECT_EQ(def.id, "ok");
    EXPECT_EQ(def.label, "Норма");

    Step s = stateStep("x", "n", "down", 0, 1);
    ResolvedState r = resolveNodeState(&s);
    EXPECT_EQ(r.label, "Недоступен");
    EXPECT_EQ(r.ring, Color::rgb(0xef4444));
    EXPECT_FALSE(r.customLabel);

    s.label = "503";
    s.color = "#ef4444";
    s.labelSize = 14;
    r = resolveNodeState(&s);
    EXPECT_EQ(r.label, "503");
    EXPECT_TRUE(r.customLabel);
    EXPECT_EQ(r.fill, (Color{121, 35, 35}));
    EXPECT_EQ(r.labelSize, 14);
}

TEST(EdgeGeom, StraightEdgeEndsBeforeArrow) {
    Model m = sampleModel();
    m.nodes[1].y = m.nodes[0].y;  // A и B на одной высоте
    Edge& e = *m.edge("eAB");
    e.curve = 0;
    const auto g = edgeGeometry(m, e);
    ASSERT_TRUE(g);
    const Node& a = m.nodes[0];
    const Node& b = m.nodes[1];
    EXPECT_DOUBLE_EQ(g->rawStart.x, a.x + a.w + 4);
    EXPECT_DOUBLE_EQ(g->rawEnd.x, b.x - 4);
    EXPECT_DOUBLE_EQ(g->end.x, b.x - 4 - kArrowLen);
    EXPECT_EQ(g->start, g->rawStart);  // однонаправленная — без отступа в начале
    EXPECT_EQ(g->path.segments().back().kind, Path::Kind::Line);
}

TEST(EdgeGeom, BidirectionalPullsBackBothEnds) {
    Model m = sampleModel();
    Edge& e = *m.edge("eAB");
    e.bidirectional = true;
    const auto g = edgeGeometry(m, e);
    ASSERT_TRUE(g);
    EXPECT_NEAR(distance(g->start, g->rawStart), kArrowLen, 1e-9);
    EXPECT_NEAR(distance(g->end, g->rawEnd), kArrowLen, 1e-9);
}

TEST(EdgeGeom, UsesPortsAndWaypoints) {
    Model m = sampleModel();
    m.nodes[0].ports.push_back({"p", 150, 10});
    Edge& e = *m.edge("eAB");
    e.fromPort = "p";
    e.waypoints = {{300, 0}, {350, 50}};
    const auto g = edgeGeometry(m, e);
    ASSERT_TRUE(g);
    EXPECT_EQ(g->rawStart, (Vec2{80 + 150, 120 + 10}));
    ASSERT_EQ(g->points.size(), 4u);
    EXPECT_EQ(g->points[1], (Vec2{300, 0}));
    EXPECT_EQ(g->path.segments().back().kind, Path::Kind::Cubic);
}

TEST(EdgeGeom, MissingNodeYieldsNothing) {
    Model m = sampleModel();
    Edge e;
    e.from = "svcA";
    e.to = "ghost";
    EXPECT_FALSE(edgeGeometry(m, e));
}

TEST(EdgeGeom, NearestSegment) {
    Model m = sampleModel();
    Edge& e = *m.edge("eAB");
    e.waypoints = {{300, 300}, {600, 300}};
    EXPECT_EQ(nearestSegmentIndex(m, e, {450, 305}), 1u);
}
