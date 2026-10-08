#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "Model/Document.hpp"
#include "Model/Registry.hpp"
#include "Model/Sample.hpp"

using namespace ad;

namespace
{

const ElementType &Type(const char *id) { return Registry::Default().Element(id); }

Document TwoNodes()
{
    Document d;
    d.AddNode({100, 100}, Type("service"));
    d.AddNode({400, 100}, Type("db"));
    return d;
}

} // namespace

TEST(DocumentTest, AddNodeUsesTypeDefaults)
{
    Document    d;
    const Node &n = d.AddNode({100, 50}, Type("db"));
    EXPECT_EQ(n.type, "db");
    EXPECT_EQ(n.label, "БД 1");
    EXPECT_DOUBLE_EQ(n.w, Type("db").width);
    EXPECT_DOUBLE_EQ(n.x, 100 - n.w / 2);
    EXPECT_TRUE(n.id.starts_with("n_"));
    EXPECT_EQ(d.AddNode({0, 0}, Type("db")).label, "БД 2");
}

TEST(DocumentTest, AddEdgeRejectsSelfAndUnknown)
{
    Document          d = TwoNodes();
    const std::string a = d.Get().nodes[0].id;
    EXPECT_EQ(d.AddEdge(a, a), nullptr);
    EXPECT_EQ(d.AddEdge(a, "nope"), nullptr);
}

TEST(DocumentTest, ParallelEdgesGetAlternatingCurve)
{
    Document          d = TwoNodes();
    const std::string a = d.Get().nodes[0].id;
    const std::string b = d.Get().nodes[1].id;
    EXPECT_DOUBLE_EQ(d.AddEdge(a, b)->curve, 0);
    EXPECT_DOUBLE_EQ(d.AddEdge(b, a)->curve, 0.2);
    EXPECT_DOUBLE_EQ(d.AddEdge(a, b)->curve, -0.2);
    EXPECT_DOUBLE_EQ(d.AddEdge(a, b)->curve, 0.4);
}

TEST(DocumentTest, RemoveNodeCascades)
{
    Document d(SampleModel());
    d.RemoveNode("svcB");
    const Model &m = d.Get();
    EXPECT_EQ(m.FindNode("svcB"), nullptr);
    EXPECT_EQ(m.FindEdge("eAB"), nullptr);
    EXPECT_NE(m.FindEdge("eAC"), nullptr);
    for (const auto &s : m.scenario.steps)
    {
        EXPECT_NE(s.node_id, "svcB");
        EXPECT_NE(s.from, "svcB");
        EXPECT_NE(s.to, "svcB");
    }
}

TEST(DocumentTest, RemoveEdgeDropsLinkStepsAndClearsMessages)
{
    Document d(SampleModel());
    Step     link;
    link.type    = StepType::Link;
    link.edge_id = "eAB";
    d.AddStep(link);
    Step msg = *d.Get().FindStep("s1");
    msg.id.clear();
    msg.edge_id              = "eAB";
    const std::string msg_id = d.AddStep(msg).id;
    d.RemoveEdge("eAB");
    EXPECT_TRUE(std::ranges::none_of(d.Get().scenario.steps,
                                     [](const Step &s)
                                     {
                                         return s.type == StepType::Link;
                                     }));
    EXPECT_TRUE(d.Get().FindStep(msg_id)->edge_id.empty());
}

TEST(DocumentTest, PortsAndEdgeReferences)
{
    Document          d = TwoNodes();
    const std::string a = d.Get().nodes[0].id;
    const std::string b = d.Get().nodes[1].id;
    const std::string p = d.AddPort(a)->id;
    EXPECT_DOUBLE_EQ(d.Get().FindPort(a, p)->dx, d.Get().nodes[0].w);
    const std::string e = d.AddEdge(a, b, p)->id;
    d.RemovePort(a, p);
    EXPECT_TRUE(d.Get().FindEdge(e)->from_port.empty());
}

TEST(DocumentTest, WaypointsInsertAtIndex)
{
    Document d(SampleModel());
    d.AddWaypoint("eAB", {1, 1});
    d.AddWaypoint("eAB", {3, 3});
    d.AddWaypoint("eAB", {2.4, 2.4}, 1);
    const auto &w = d.Get().FindEdge("eAB")->waypoints;
    ASSERT_EQ(w.size(), 3U);
    EXPECT_EQ(w[1], (Vec2{2, 2}));
    d.RemoveWaypoint("eAB", 0);
    d.RemoveWaypoint("eAB", 99);
    EXPECT_EQ(d.Get().FindEdge("eAB")->waypoints.size(), 2U);
}

TEST(DocumentTest, ReverseEdgeSwapsArrowsToo)
{
    Document d(SampleModel());
    d.Mutable().FindEdge("eAB")->style.arrow_end = "open";
    d.ReverseEdge("eAB");
    const Edge &e = *d.Get().FindEdge("eAB");
    EXPECT_EQ(e.from, "svcB");
    EXPECT_DOUBLE_EQ(e.curve, -0.15);
    EXPECT_EQ(e.style.arrow_start, "open");
    EXPECT_FALSE(e.style.arrow_end.has_value());
}

TEST(DocumentTest, StepsAndDuration)
{
    Document d(SampleModel());
    Step     s;
    s.type     = StepType::Note;
    s.start    = 20000;
    s.duration = 3000;
    d.AddStep(s);
    EXPECT_DOUBLE_EQ(d.Get().scenario.duration, 24500);
    EXPECT_TRUE(std::ranges::is_sorted(d.Get().scenario.steps, {}, &Step::start));
    const Step *copy = d.DuplicateStep("s1");
    ASSERT_NE(copy, nullptr);
    EXPECT_DOUBLE_EQ(copy->start, 300 + 1100 + 200);
}

TEST(DocumentTest, UserDurationIsKept)
{
    Scenario sc;
    sc.duration = 60000;
    EXPECT_DOUBLE_EQ(AutoDuration(sc), 4000);
    sc.user_duration = true;
    EXPECT_DOUBLE_EQ(AutoDuration(sc), 60000);
    Document d(SampleModel());
    d.SetDuration(30000);
    d.RemoveStep("s1");
    EXPECT_DOUBLE_EQ(d.Get().scenario.duration, 30000);
}

TEST(DocumentTest, DefaultStepsAndNormalize)
{
    Document empty;
    EXPECT_FALSE(empty.MakeDefaultStep(StepType::Message, 0).has_value());
    EXPECT_FALSE(empty.MakeDefaultStep(StepType::Effect, 0).has_value());
    EXPECT_TRUE(empty.MakeDefaultStep(StepType::Note, 0).has_value());

    Document   d(SampleModel());
    const auto msg = d.MakeDefaultStep(StepType::Message, 1234);
    ASSERT_TRUE(msg.has_value());
    EXPECT_DOUBLE_EQ(msg->start, 1250);
    EXPECT_EQ(msg->edge_id, "eAB");
    EXPECT_EQ(d.MakeDefaultStep(StepType::Effect, 0)->effect, "pulse");

    Step s = *d.Get().FindStep("s1");
    s.type = StepType::State;
    d.NormalizeStep(s);
    EXPECT_EQ(s.node_id, "svcA");
    s.type    = StepType::Link;
    s.edge_id = "ghost";
    d.NormalizeStep(s);
    EXPECT_EQ(s.edge_id, "eAB");
}

TEST(DocumentTest, LibraryOperations)
{
    Document  d;
    EffectDef fx;
    fx.id = "my-fx";
    d.UpsertEffect(fx);
    EXPECT_NE(d.Get().library.Effect("my-fx"), nullptr);
    EXPECT_TRUE(d.RemoveLibraryItem("my-fx"));
    EXPECT_FALSE(d.RemoveLibraryItem("my-fx"));
    ASSERT_TRUE(d.Undo());
    EXPECT_NE(d.Get().library.Effect("my-fx"), nullptr);
}

TEST(DocumentTest, UndoRedo)
{
    Document d;
    d.AddNode({0, 0}, Type("service"));
    d.AddNode({100, 0}, Type("service"));
    ASSERT_TRUE(d.Undo());
    EXPECT_EQ(d.Get().nodes.size(), 1U);
    ASSERT_TRUE(d.Undo());
    EXPECT_FALSE(d.Undo());
    ASSERT_TRUE(d.Redo());
    d.AddNode({5, 5}, Type("client"));
    EXPECT_FALSE(d.CanRedo());
}

TEST(DocumentTest, MergeKeyCoalescesEdits)
{
    Document d(SampleModel());
    for (const char *label : {"A", "AB", "ABC"})
    {
        d.Checkpoint("node:svcA:label");
        d.Mutable().FindNode("svcA")->label = label;
    }
    ASSERT_TRUE(d.Undo());
    EXPECT_EQ(d.Get().FindNode("svcA")->label, "Сервис A");
    EXPECT_FALSE(d.CanUndo());
}

TEST(DocumentTest, HistoryIsBounded)
{
    Document d;
    for (size_t i = 0; i < Document::kMaxHistory + 20; ++i)
    {
        d.AddNode({0, 0}, Type("service"));
    }
    size_t undos = 0;
    while (d.Undo())
    {
        ++undos;
    }
    EXPECT_EQ(undos, Document::kMaxHistory);
}

TEST(DocumentTest, IdsAreUnique)
{
    IdGenerator           g(42);
    Model                 m;
    std::set<std::string> seen;
    for (int i = 0; i < 300; ++i)
    {
        Node n;
        n.id = g.Next("n", m);
        EXPECT_TRUE(seen.insert(n.id).second);
        m.nodes.push_back(n);
    }
}
