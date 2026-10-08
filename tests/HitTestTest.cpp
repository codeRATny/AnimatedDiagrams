#include <gtest/gtest.h>

#include "Interaction/HitTest.hpp"
#include "Model/Sample.hpp"

using namespace ad;

TEST(HitTestTest, NodeTopmostAndEmpty)
{
    Model m = SampleModel();
    EXPECT_EQ(HitTest(m, {100, 140}, {}, 0).id, "svcA");
    EXPECT_TRUE(HitTest(m, {-500, -500}, {}, 0).Empty());
    m.nodes[2].x = m.nodes[0].x;
    m.nodes[2].y = m.nodes[0].y;
    EXPECT_EQ(HitTest(m, {100, 140}, {}, 0).id, "svcC");
}

TEST(HitTestTest, PortAndWaypointPriority)
{
    Model m = SampleModel();
    m.nodes[0].ports.push_back({"p1", 10, 10});
    const Hit port = HitTest(m, {91, 131}, {}, 0);
    EXPECT_EQ(port.kind, Hit::Kind::Port);
    EXPECT_EQ(port.port_id, "p1");
    m.FindEdge("eAB")->waypoints = {{350, 0}};
    EXPECT_NE(HitTest(m, {350, 0}, {}, 0).kind, Hit::Kind::Waypoint);
    EXPECT_EQ(HitTest(m, {352, 1}, {Selection::Kind::Edge, "eAB"}, 0).kind, Hit::Kind::Waypoint);
}

TEST(HitTestTest, EdgeTolerance)
{
    Model m;
    Node  a;
    a.id = "a";
    Node b;
    b.id    = "b";
    b.x     = 400;
    m.nodes = {a, b};
    Edge e;
    e.id    = "e";
    e.from  = "a";
    e.to    = "b";
    m.edges = {e};
    EXPECT_EQ(HitTest(m, {270, 38}, {}, 0).kind, Hit::Kind::Edge);
    EXPECT_TRUE(HitTest(m, {270, 60}, {}, 0).Empty());
    EXPECT_EQ(HitTest(m, {270, 60}, {}, 25).kind, Hit::Kind::Edge);
}
