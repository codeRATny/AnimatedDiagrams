#include <gtest/gtest.h>

#include "Engine/Layout.hpp"
#include "Model/Sample.hpp"

using namespace ad;

TEST(LayoutTest, LayersFollowEdges)
{
    Model m              = SampleModel();
    m.edges[0].waypoints = {{1, 1}};
    AutoLayout(m);
    const Node &a = *m.FindNode("svcA");
    const Node &b = *m.FindNode("svcB");
    const Node &c = *m.FindNode("svcC");
    EXPECT_LT(a.x, b.x);
    EXPECT_DOUBLE_EQ(b.x, c.x); // same layer
    EXPECT_NE(b.y, c.y);
    EXPECT_TRUE(m.edges[0].waypoints.empty());
}

TEST(LayoutTest, TopToBottomAndCycles)
{
    Model m = SampleModel();
    Edge  back;
    back.id   = "back";
    back.from = "svcB";
    back.to   = "svcA"; // cycle A -> B -> A
    m.edges.push_back(back);
    LayoutOptions opt;
    opt.direction = LayoutDirection::TopToBottom;
    AutoLayout(m, opt);
    EXPECT_LT(m.FindNode("svcA")->y, m.FindNode("svcC")->y);
    Model empty;
    AutoLayout(empty); // no crash
}
