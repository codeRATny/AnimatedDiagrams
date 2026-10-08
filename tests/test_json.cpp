#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "ad/json_io.hpp"
#include "ad/sample.hpp"

using namespace ad;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

TEST(Json, SampleRoundTrip) {
    const Model m = sampleModel();
    const auto parsed = parseModel(serializeModel(m));
    ASSERT_TRUE(parsed) << parsed.error();
    EXPECT_EQ(*parsed, m);
}

TEST(Json, BundledSampleFileMatchesBuiltInSample) {
    const auto parsed = parseModel(readFile(AD_SAMPLES_DIR "/fallback-retry.json"));
    ASSERT_TRUE(parsed) << parsed.error();
    EXPECT_EQ(parsed->nodes, sampleModel().nodes);
    EXPECT_EQ(parsed->scenario.steps, sampleModel().scenario.steps);
}

TEST(Json, ReadsLegacyWebFormat) {
    // формат веб-версии (version 1): null-порты, нет waypoints/bidirectional у связей
    constexpr std::string_view legacy = R"({
      "version": 1,
      "meta": {"name": "Legacy", "createdAt": 1700000000000},
      "view": {"zoom": 1.5, "panX": 10, "panY": -20},
      "nodes": [
        {"id": "a", "label": "A", "kind": "db", "x": 0, "y": 0, "w": 150, "h": 66, "color": "#a855f7", "shape": "db",
         "ports": [{"id": "p1", "dx": 150, "dy": 33}]},
        {"id": "b", "label": "B", "kind": "service", "x": 300, "y": 0, "w": 150, "h": 66}
      ],
      "edges": [{"id": "e1", "from": "a", "to": "b", "fromPort": "p1", "toPort": null, "label": "x", "curve": 0.1}],
      "scenario": {"duration": 8000, "steps": [
        {"id": "s1", "type": "message", "from": "a", "to": "b", "variant": "retry", "label": "r", "start": 100, "duration": 900},
        {"id": "s2", "type": "timer", "nodeId": "a", "seconds": 3, "label": "t", "start": 0, "duration": 3000}
      ]}
    })";
    const auto m = parseModel(legacy);
    ASSERT_TRUE(m) << m.error();
    EXPECT_EQ(m->meta.name, "Legacy");
    EXPECT_EQ(m->meta.createdAt, 1700000000000);
    EXPECT_DOUBLE_EQ(m->view.zoom, 1.5);
    ASSERT_EQ(m->nodes.size(), 2u);
    EXPECT_EQ(m->nodes[1].color, "#4f8cff");  // цвет по типу
    EXPECT_EQ(m->nodes[1].shape, "round");
    ASSERT_EQ(m->edges.size(), 1u);
    EXPECT_EQ(m->edges[0].fromPort, "p1");
    EXPECT_TRUE(m->edges[0].toPort.empty());
    EXPECT_TRUE(m->edges[0].waypoints.empty());
    ASSERT_EQ(m->scenario.steps.size(), 2u);
    EXPECT_EQ(m->scenario.steps[0].id, "s2");  // отсортировано по start
    EXPECT_EQ(m->scenario.steps[0].type, StepType::Timer);
    EXPECT_DOUBLE_EQ(m->scenario.steps[0].seconds, 3);
    EXPECT_DOUBLE_EQ(m->scenario.duration, 8000);
}

TEST(Json, MissingMetaAndViewGetDefaults) {
    // в веб-версии такой файл ломал сохранённое состояние приложения
    const auto m = parseModel(R"({"nodes": [], "scenario": {}})");
    ASSERT_TRUE(m) << m.error();
    EXPECT_FALSE(m->meta.name.empty());
    EXPECT_DOUBLE_EQ(m->view.zoom, 1);
    EXPECT_GE(m->scenario.duration, 4000);
}

TEST(Json, RejectsGarbage) {
    EXPECT_FALSE(parseModel("not json"));
    EXPECT_FALSE(parseModel("[]"));
    EXPECT_FALSE(parseModel(R"({"nodes": []})"));
    EXPECT_FALSE(parseModel(""));
}

TEST(Json, DropsDanglingReferences) {
    constexpr std::string_view src = R"({
      "nodes": [{"id": "a", "label": "A"}, {"id": "b", "label": "B"}],
      "edges": [
        {"id": "e1", "from": "a", "to": "zzz"},
        {"id": "e2", "from": "a", "to": "a"},
        {"id": "e3", "from": "a", "to": "b", "fromPort": "nope"}
      ],
      "scenario": {"steps": [
        {"id": "s1", "type": "message", "from": "a", "to": "ghost"},
        {"id": "s2", "type": "link", "edgeId": "e1"},
        {"id": "s3", "type": "state", "nodeId": "ghost"},
        {"id": "s4", "type": "teleport"},
        {"id": "s5", "type": "message", "from": "a", "to": "b", "edgeId": "e1"},
        {"id": "s6", "type": "note", "text": "ok"}
      ]}
    })";
    const auto m = parseModel(src);
    ASSERT_TRUE(m) << m.error();
    ASSERT_EQ(m->edges.size(), 1u);
    EXPECT_EQ(m->edges[0].id, "e3");
    EXPECT_TRUE(m->edges[0].fromPort.empty());
    ASSERT_EQ(m->scenario.steps.size(), 2u);
    EXPECT_EQ(m->scenario.steps[0].id, "s5");
    EXPECT_TRUE(m->scenario.steps[0].edgeId.empty());  // связи e1 больше нет
    EXPECT_EQ(m->scenario.steps[1].id, "s6");
}

TEST(Json, GeneratesMissingAndDuplicateIds) {
    const auto m = parseModel(R"({"nodes": [{"label": "A"}, {"id": "x"}, {"id": "x"}], "scenario": {}})");
    ASSERT_TRUE(m);
    ASSERT_EQ(m->nodes.size(), 3u);
    EXPECT_FALSE(m->nodes[0].id.empty());
    EXPECT_EQ(m->nodes[1].id, "x");
    EXPECT_NE(m->nodes[2].id, "x");
}

TEST(Json, ToleratesWrongFieldTypes) {
    const auto m = parseModel(R"({"nodes": [{"id": "a", "x": "12", "w": null, "label": 5}], "scenario": {"duration": "x"}})");
    ASSERT_TRUE(m);
    EXPECT_DOUBLE_EQ(m->nodes[0].x, 0);
    EXPECT_DOUBLE_EQ(m->nodes[0].w, kDefaultNodeW);
    EXPECT_EQ(m->nodes[0].label, "5");
}

TEST(Json, PersistsUserDuration) {
    Model m = sampleModel();
    m.scenario.userDuration = true;
    m.scenario.duration = 30000;
    const auto back = parseModel(serializeModel(m));
    ASSERT_TRUE(back);
    EXPECT_TRUE(back->scenario.userDuration);
    EXPECT_DOUBLE_EQ(back->scenario.duration, 30000);
}

TEST(Json, OptionalFieldsRoundTrip) {
    Model m = sampleModel();
    m.edges[0].labelPos = 0.3;
    m.edges[0].labelSize = 14;
    m.edges[0].waypoints = {{10, 20}, {30, 40}};
    m.edges[0].bidirectional = true;
    Step link;
    link.id = "lnk";
    link.type = StepType::Link;
    link.edgeId = "eAB";
    link.text = "TLS";
    link.anim = "pulse";
    link.labelOff = -5;
    link.start = 20000;
    m.scenario.steps.push_back(link);
    const auto back = parseModel(serializeModel(m));
    ASSERT_TRUE(back) << back.error();
    EXPECT_EQ(back->edges[0], m.edges[0]);
    const Step* s = back->step("lnk");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->anim, "pulse");
    EXPECT_EQ(s->labelOff, -5);
    EXPECT_FALSE(s->labelPos.has_value());
}
