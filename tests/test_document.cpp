#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "ad/document.hpp"
#include "ad/sample.hpp"

using namespace ad;

namespace {

Document twoNodes() {
    Document d;
    d.addNode({100, 100}, "service");
    d.addNode({400, 100}, "db");
    return d;
}

}  // namespace

TEST(Document, AddNodeUsesKindDefaults) {
    Document d;
    const Node& n = d.addNode({100, 50}, "db");
    EXPECT_EQ(n.kind, "db");
    EXPECT_EQ(n.shape, "db");
    EXPECT_EQ(n.color, "#a855f7");
    EXPECT_EQ(n.label, "БД 1");
    EXPECT_DOUBLE_EQ(n.x, 30);  // центр в точке клика
    EXPECT_DOUBLE_EQ(n.y, 18);
    EXPECT_TRUE(n.id.starts_with("n_"));
}

TEST(Document, AddEdgeRejectsSelfAndUnknown) {
    Document d = twoNodes();
    const std::string a = d.model().nodes[0].id;
    EXPECT_EQ(d.addEdge(a, a), nullptr);
    EXPECT_EQ(d.addEdge(a, "nope"), nullptr);
}

TEST(Document, ParallelEdgesGetAlternatingCurve) {
    Document d = twoNodes();
    const std::string a = d.model().nodes[0].id, b = d.model().nodes[1].id;
    EXPECT_DOUBLE_EQ(d.addEdge(a, b)->curve, 0);
    EXPECT_DOUBLE_EQ(d.addEdge(b, a)->curve, 0.2);
    EXPECT_DOUBLE_EQ(d.addEdge(a, b)->curve, -0.2);
    EXPECT_DOUBLE_EQ(d.addEdge(a, b)->curve, 0.4);
}

TEST(Document, RemoveNodeCascades) {
    Document d(sampleModel());
    d.removeNode("svcB");
    const Model& m = d.model();
    EXPECT_EQ(m.node("svcB"), nullptr);
    EXPECT_EQ(m.edge("eAB"), nullptr);
    EXPECT_NE(m.edge("eAC"), nullptr);
    for (const auto& s : m.scenario.steps) {
        EXPECT_NE(s.nodeId, "svcB");
        EXPECT_NE(s.from, "svcB");
        EXPECT_NE(s.to, "svcB");
    }
}

TEST(Document, RemoveEdgeDropsLinkStepsAndClearsMessages) {
    Document d(sampleModel());
    Step link;
    link.type = StepType::Link;
    link.edgeId = "eAB";
    d.addStep(link);
    Step msg = *d.model().step("s1");
    msg.id.clear();
    msg.edgeId = "eAB";
    const std::string msgId = d.addStep(msg).id;

    d.removeEdge("eAB");
    const Model& m = d.model();
    EXPECT_TRUE(std::ranges::none_of(m.scenario.steps, [](const Step& s) { return s.type == StepType::Link; }));
    ASSERT_NE(m.step(msgId), nullptr);
    EXPECT_TRUE(m.step(msgId)->edgeId.empty());
}

TEST(Document, PortsAndEdgeReferences) {
    Document d = twoNodes();
    const std::string a = d.model().nodes[0].id, b = d.model().nodes[1].id;
    const std::string p = d.addPort(a)->id;
    const Port* port = d.model().port(a, p);
    ASSERT_NE(port, nullptr);
    EXPECT_DOUBLE_EQ(port->dx, kDefaultNodeW);
    EXPECT_DOUBLE_EQ(port->dy, kDefaultNodeH / 2);
    const std::string e = d.addEdge(a, b, p)->id;
    d.removePort(a, p);
    EXPECT_TRUE(d.model().edge(e)->fromPort.empty());
}

TEST(Document, WaypointsInsertAtIndex) {
    Document d(sampleModel());
    d.addWaypoint("eAB", {1, 1});
    d.addWaypoint("eAB", {3, 3});
    d.addWaypoint("eAB", {2.4, 2.4}, 1);
    const auto& w = d.model().edge("eAB")->waypoints;
    ASSERT_EQ(w.size(), 3u);
    EXPECT_EQ(w[1], (Vec2{2, 2}));  // координаты округляются
    d.removeWaypoint("eAB", 0);
    EXPECT_EQ(d.model().edge("eAB")->waypoints.front(), (Vec2{2, 2}));
    d.removeWaypoint("eAB", 99);  // вне диапазона — без эффекта
    EXPECT_EQ(d.model().edge("eAB")->waypoints.size(), 2u);
}

TEST(Document, ReverseEdge) {
    Document d(sampleModel());
    d.addWaypoint("eAB", {1, 1});
    d.addWaypoint("eAB", {2, 2});
    d.reverseEdge("eAB");
    const Edge& e = *d.model().edge("eAB");
    EXPECT_EQ(e.from, "svcB");
    EXPECT_EQ(e.to, "svcA");
    EXPECT_DOUBLE_EQ(e.curve, -0.15);
    EXPECT_EQ(e.waypoints.front(), (Vec2{2, 2}));
}

TEST(Document, AddStepSortsAndExtendsDuration) {
    Document d(sampleModel());
    Step s;
    s.type = StepType::Note;
    s.start = 20000;
    s.duration = 3000;
    d.addStep(s);
    EXPECT_DOUBLE_EQ(d.model().scenario.duration, 24500);  // ceil((23000+1200)/500)*500
    const auto& steps = d.model().scenario.steps;
    EXPECT_TRUE(std::ranges::is_sorted(steps, {}, &Step::start));
}

TEST(Document, AutoDurationRespectsUserDuration) {
    Scenario sc;
    sc.duration = 60000;
    EXPECT_DOUBLE_EQ(autoDuration(sc), 4000);
    sc.userDuration = true;
    EXPECT_DOUBLE_EQ(autoDuration(sc), 60000);
}

TEST(Document, SetDurationMarksUserDuration) {
    Document d(sampleModel());
    d.setDuration(30000);
    EXPECT_TRUE(d.model().scenario.userDuration);
    d.removeStep("s1");
    EXPECT_DOUBLE_EQ(d.model().scenario.duration, 30000);  // не сжимается автоматически
}

TEST(Document, DuplicateStep) {
    Document d(sampleModel());
    const Step* copy = d.duplicateStep("s1");
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->id, "s1");
    EXPECT_DOUBLE_EQ(copy->start, 300 + 1100 + 200);
    EXPECT_EQ(copy->label, "запрос");
}

TEST(Document, DefaultStepsNeedNodes) {
    Document empty;
    EXPECT_FALSE(empty.makeDefaultStep(StepType::Message, 0));
    EXPECT_FALSE(empty.makeDefaultStep(StepType::Link, 0));
    EXPECT_TRUE(empty.makeDefaultStep(StepType::Note, 0));

    Document d(sampleModel());
    const auto msg = d.makeDefaultStep(StepType::Message, 1234);
    ASSERT_TRUE(msg);
    EXPECT_DOUBLE_EQ(msg->start, 1250);  // привязка к 50 мс
    EXPECT_EQ(msg->edgeId, "eAB");
    const auto link = d.makeDefaultStep(StepType::Link, 0);
    ASSERT_TRUE(link);
    EXPECT_EQ(link->edgeId, "eAB");
}

TEST(Document, NormalizeStepAfterTypeChange) {
    Document d(sampleModel());
    Step s = *d.model().step("s1");  // сообщение A → B
    s.type = StepType::State;
    d.normalizeStep(s);
    EXPECT_EQ(s.nodeId, "svcA");
    s.type = StepType::Link;
    s.edgeId = "ghost";
    d.normalizeStep(s);
    EXPECT_EQ(s.edgeId, "eAB");
    EXPECT_EQ(s.anim, "flow");
}

TEST(History, UndoRedo) {
    Document d;
    d.addNode({0, 0}, "service");
    d.addNode({100, 0}, "service");
    EXPECT_EQ(d.model().nodes.size(), 2u);
    ASSERT_TRUE(d.undo());
    EXPECT_EQ(d.model().nodes.size(), 1u);
    ASSERT_TRUE(d.undo());
    EXPECT_TRUE(d.model().nodes.empty());
    EXPECT_FALSE(d.undo());
    ASSERT_TRUE(d.redo());
    EXPECT_EQ(d.model().nodes.size(), 1u);
    d.addNode({5, 5}, "client");  // новая правка очищает redo
    EXPECT_FALSE(d.canRedo());
}

TEST(History, MergeKeyCoalescesEdits) {
    Document d(sampleModel());
    for (const char* label : {"A", "AB", "ABC"}) {
        d.checkpoint("node:svcA:label");
        d.mutableModel().node("svcA")->label = label;
    }
    EXPECT_EQ(d.model().node("svcA")->label, "ABC");
    ASSERT_TRUE(d.undo());
    EXPECT_EQ(d.model().node("svcA")->label, "Сервис A");
    EXPECT_FALSE(d.canUndo());
}

TEST(History, BreakMergeStartsNewStep) {
    Document d(sampleModel());
    d.checkpoint("k");
    d.mutableModel().meta.name = "1";
    d.breakMerge();
    d.checkpoint("k");
    d.mutableModel().meta.name = "2";
    d.undo();
    EXPECT_EQ(d.model().meta.name, "1");
}

TEST(History, IsBounded) {
    Document d;
    for (std::size_t i = 0; i < Document::kMaxHistory + 50; ++i) d.addNode({0, 0}, "service");
    std::size_t undos = 0;
    while (d.undo()) ++undos;
    EXPECT_EQ(undos, Document::kMaxHistory);
}

TEST(History, ResetClearsHistory) {
    Document d;
    d.addNode({0, 0}, "service");
    d.reset(sampleModel());
    EXPECT_FALSE(d.canUndo());
    EXPECT_FALSE(d.canRedo());
}

TEST(IdGenerator, UniqueAgainstModel) {
    IdGenerator g(42);
    Model m;
    std::set<std::string> seen;
    for (int i = 0; i < 500; ++i) {
        auto id = g.next("n", m);
        EXPECT_TRUE(seen.insert(id).second);
        EXPECT_EQ(id.size(), 9u);
        Node n;
        n.id = id;
        m.nodes.push_back(n);
    }
}
