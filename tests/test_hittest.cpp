#include <gtest/gtest.h>

#include "ad/hittest.hpp"
#include "ad/sample.hpp"

using namespace ad;

TEST(HitTest, NodeAndEmpty) {
    const Model m = sampleModel();
    const Hit h = hitTest(m, {100, 140}, {}, 0);
    EXPECT_EQ(h.kind, Hit::Kind::Node);
    EXPECT_EQ(h.id, "svcA");
    EXPECT_TRUE(hitTest(m, {-500, -500}, {}, 0).empty());
}

TEST(HitTest, TopmostNodeWins) {
    Model m = sampleModel();
    m.nodes[2].x = m.nodes[0].x;
    m.nodes[2].y = m.nodes[0].y;
    EXPECT_EQ(hitTest(m, {100, 140}, {}, 0).id, "svcC");
}

TEST(HitTest, PortBeforeNode) {
    Model m = sampleModel();
    m.nodes[0].ports.push_back({"p1", 10, 10});
    const Hit h = hitTest(m, {91, 131}, {}, 0);
    EXPECT_EQ(h.kind, Hit::Kind::Port);
    EXPECT_EQ(h.id, "svcA");
    EXPECT_EQ(h.portId, "p1");
}

TEST(HitTest, WaypointOnlyOnSelectedEdge) {
    Model m = sampleModel();
    m.edge("eAB")->waypoints = {{350, 0}};
    EXPECT_NE(hitTest(m, {350, 0}, {}, 0).kind, Hit::Kind::Waypoint);
    const Hit h = hitTest(m, {352, 1}, {Selection::Kind::Edge, "eAB"}, 0);
    EXPECT_EQ(h.kind, Hit::Kind::Waypoint);
    EXPECT_EQ(h.waypoint, 0u);
}

TEST(HitTest, EdgeWithinTolerance) {
    Model m;
    Node a, b;
    a.id = "a";
    b.id = "b";
    b.x = 400;
    m.nodes = {a, b};
    Edge e;
    e.id = "e";
    e.from = "a";
    e.to = "b";
    m.edges = {e};
    // горизонтальная связь на y = 32
    EXPECT_EQ(hitTest(m, {270, 38}, {}, 0).kind, Hit::Kind::Edge);
    EXPECT_TRUE(hitTest(m, {270, 60}, {}, 0).empty());
    EXPECT_EQ(hitTest(m, {270, 60}, {}, 25).kind, Hit::Kind::Edge);
}
