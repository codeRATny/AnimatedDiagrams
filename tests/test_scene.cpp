#include <gtest/gtest.h>

#include <algorithm>

#include "ad/sample.hpp"
#include "ad/scene.hpp"

using namespace ad;

namespace {

std::vector<std::string> texts(const Frame& f) {
    std::vector<std::string> out;
    for (const auto& it : f.items)
        if (const auto* t = std::get_if<TextShape>(&it.shape)) out.push_back(t->text);
    return out;
}

bool hasText(const Frame& f, std::string_view s) { return std::ranges::contains(texts(f), std::string(s)); }

const ApproxTextMeasurer kTm;

}  // namespace

TEST(Scene, Utf8Length) {
    EXPECT_EQ(utf8Length("abc"), 3u);
    EXPECT_EQ(utf8Length("абв"), 3u);
    EXPECT_EQ(utf8Length("⏱x"), 2u);
}

TEST(Scene, StaticDiagramHasNodesAndEdges) {
    const Model m = sampleModel();
    const Frame f = buildFrame(m, 0, {}, kTm);
    EXPECT_TRUE(hasText(f, "Сервис A"));
    EXPECT_TRUE(hasText(f, "Сервис C"));
    EXPECT_TRUE(hasText(f, "REST"));
    EXPECT_TRUE(hasText(f, "fallback"));
    EXPECT_TRUE(hasText(f, "Норма"));
    EXPECT_FALSE(hasText(f, "запрос"));  // сообщений ещё нет
}

TEST(Scene, MessageAppearsWhileActive) {
    const Model m = sampleModel();
    EXPECT_TRUE(hasText(buildFrame(m, 800, {}, kTm), "запрос"));
    EXPECT_FALSE(hasText(buildFrame(m, 1500, {}, kTm), "запрос"));
}

TEST(Scene, StateChangesNodeSubtitle) {
    const Model m = sampleModel();
    EXPECT_TRUE(hasText(buildFrame(m, 2000, {}, kTm), "Недоступен"));
}

TEST(Scene, TimerCountsDown) {
    const Model m = sampleModel();  // таймер 5с на [1600, 6600]
    EXPECT_TRUE(hasText(buildFrame(m, 1600, {}, kTm), "5с"));
    EXPECT_TRUE(hasText(buildFrame(m, 4100, {}, kTm), "3с"));
    EXPECT_TRUE(hasText(buildFrame(m, 6600, {}, kTm), "0с"));
    EXPECT_TRUE(hasText(buildFrame(m, 4100, {}, kTm), "timeout"));
}

TEST(Scene, LastFrameKeepsStates) {
    const Model m = sampleModel();
    const Frame f = buildFrame(m, m.scenario.duration, {}, kTm);
    EXPECT_TRUE(hasText(f, "Недоступен"));
    EXPECT_TRUE(hasText(f, "Успех"));
    EXPECT_TRUE(hasText(f, "Активен"));
}

TEST(Scene, WaypointHandlesOnlyForSelectedEdgeWithChrome) {
    Model m = sampleModel();
    m.edge("eAB")->waypoints = {{300, 20}};
    auto countHandles = [&](const SceneOptions& o) {
        const Frame f = buildFrame(m, 0, o, kTm);
        return std::ranges::count_if(f.items, [](const Item& it) {
            const auto* e = std::get_if<EllipseShape>(&it.shape);
            return e && e->center == Vec2{300, 20};
        });
    };
    SceneOptions sel;
    sel.selection = {Selection::Kind::Edge, "eAB"};
    EXPECT_EQ(countHandles({}), 0);
    EXPECT_EQ(countHandles(sel), 1);
    sel.editorChrome = false;  // экспорт
    EXPECT_EQ(countHandles(sel), 0);
}

TEST(Scene, PulseScalesNode) {
    const Model m = sampleModel();  // пульс svcA на [6600, 7300]
    const Frame f = buildFrame(m, 6700, {}, kTm);
    const bool scaled = std::ranges::any_of(f.items, [](const Item& it) {
        return it.transform && it.transform->scale > 1 && it.transform->origin == Vec2{155, 153};
    });
    EXPECT_TRUE(scaled);
}

TEST(Scene, LinkStepDrawsBadge) {
    Model m = sampleModel();
    Step s;
    s.id = "l";
    s.type = StepType::Link;
    s.edgeId = "eAC";
    s.text = "TLS handshake";
    s.start = 0;
    s.duration = 1000;
    m.scenario.steps.push_back(s);
    EXPECT_TRUE(hasText(buildFrame(m, 500, {}, kTm), "TLS handshake"));
}

TEST(Scene, ContentBoundsIncludeNotesAndWaypoints) {
    Model m = sampleModel();
    m.edge("eAB")->waypoints = {{1000, -300}};
    const Rect r = contentBounds(m, kTm);
    EXPECT_LE(r.y, -300);
    EXPECT_GE(r.right(), 1000);
    EXPECT_LE(r.y, 20);  // заметка s2b на y=20
}

TEST(Scene, ContentBoundsOfEmptyModel) { EXPECT_EQ(contentBounds(Model{}, kTm), (Rect{0, 0, 640, 360})); }
