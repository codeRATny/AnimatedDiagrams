#include <gtest/gtest.h>

#include "Io/JsonCodec.hpp"
#include "Io/JsonIo.hpp"
#include "Model/Sample.hpp"
#include "Utils/File.hpp"

using namespace ad;

TEST(JsonIoTest, SampleRoundTrip)
{
    const Model m      = SampleModel();
    const auto  parsed = ParseModel(SerializeModel(m));
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    EXPECT_EQ(*parsed, m);
}

TEST(JsonIoTest, BundledSampleFileMatches)
{
    const auto parsed = ParseModel(ReadFile(AD_SAMPLES_DIR "/fallback-retry.json"));
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    EXPECT_EQ(parsed->nodes, SampleModel().nodes);
    EXPECT_EQ(parsed->scenario.steps, SampleModel().scenario.steps);
}

TEST(JsonIoTest, ReadsWebVersionFormat)
{
    // v1 file: "color" accent, "shape", edge "style": "dashed", "bidirectional", "pulse" step
    constexpr std::string_view kLegacy = R"({
      "version": 1, "meta": {"name": "Legacy", "createdAt": 1700000000000}, "view": {"zoom": 1.5, "panX": 10, "panY": -20},
      "nodes": [
        {"id": "a", "label": "A", "kind": "db", "x": 0, "y": 0, "w": 150, "h": 66, "color": "#a855f7", "shape": "db",
         "ports": [{"id": "p1", "dx": 150, "dy": 33}]},
        {"id": "b", "label": "B", "kind": "service", "x": 300, "y": 0, "w": 150, "h": 66}],
      "edges": [{"id": "e1", "from": "a", "to": "b", "fromPort": "p1", "toPort": null, "style": "dashed", "bidirectional": true}],
      "scenario": {"duration": 8000, "steps": [
        {"id": "s1", "type": "pulse", "nodeId": "a", "start": 0, "duration": 700},
        {"id": "s2", "type": "timer", "nodeId": "a", "seconds": 3, "start": 100, "duration": 3000}]}
    })";
    const auto                 m       = ParseModel(kLegacy);
    ASSERT_TRUE(m.has_value()) << m.error();
    EXPECT_EQ(m->meta.created_at, 1700000000000);
    EXPECT_EQ(m->nodes[0].accent, "#a855f7");
    EXPECT_EQ(m->nodes[0].type, "db");
    EXPECT_EQ(m->edges[0].style.stroke_style, "dashed");
    EXPECT_EQ(m->edges[0].style.arrow_start, "triangle");
    EXPECT_EQ(m->edges[0].from_port, "p1");
    EXPECT_EQ(m->scenario.steps[0].type, StepType::Effect);
    EXPECT_EQ(m->scenario.steps[0].effect, "pulse");
    EXPECT_EQ(m->version, kModelVersion);
}

TEST(JsonIoTest, MissingMetaAndViewGetDefaults)
{
    const auto m = ParseModel(R"({"nodes": [], "scenario": {}})");
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->meta.name.empty());
    EXPECT_DOUBLE_EQ(m->view.zoom, 1);
    EXPECT_GE(m->scenario.duration, 4000);
}

TEST(JsonIoTest, RejectsGarbage)
{
    EXPECT_FALSE(ParseModel("not json").has_value());
    EXPECT_FALSE(ParseModel("[]").has_value());
    EXPECT_FALSE(ParseModel(R"({"nodes": []})").has_value());
}

TEST(JsonIoTest, DropsDanglingReferences)
{
    constexpr std::string_view kSrc = R"({
      "nodes": [{"id": "a"}, {"id": "b"}],
      "edges": [{"id": "e1", "from": "a", "to": "zzz"}, {"id": "e2", "from": "a", "to": "a"}, {"id": "e3", "from": "a", "to": "b", "fromPort": "x"}],
      "scenario": {"steps": [
        {"id": "s1", "type": "message", "from": "a", "to": "ghost"}, {"id": "s2", "type": "link", "edgeId": "e1"},
        {"id": "s3", "type": "state", "nodeId": "ghost"}, {"id": "s4", "type": "teleport"},
        {"id": "s5", "type": "message", "from": "a", "to": "b", "edgeId": "e1"}, {"id": "s6", "type": "note", "text": "ok"}]}
    })";
    const auto                 m    = ParseModel(kSrc);
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->edges.size(), 1U);
    EXPECT_TRUE(m->edges[0].from_port.empty());
    ASSERT_EQ(m->scenario.steps.size(), 2U);
    EXPECT_TRUE(m->scenario.steps[0].edge_id.empty());
}

TEST(JsonIoTest, ExtendedFieldsRoundTrip)
{
    Model m                      = SampleModel();
    m.nodes[0].style.shape       = "custom";
    m.nodes[0].style.custom_path = "M0 0 L1 0 L1 1 Z";
    m.nodes[0].style.opacity     = 0.5;
    m.nodes[0].style.shadow      = false;
    m.edges[0].style.routing     = "orthogonal";
    m.edges[0].style.arrow_end   = "diamond";
    m.edges[0].label_pos         = 0.3;
    m.edges[0].waypoints         = {{10, 20}};
    m.scene.background           = "#ffffff";
    m.scenario.user_duration     = true;
    Step &msg                    = *m.FindStep("s1");
    msg.packet                   = "envelope";
    msg.packet_count             = 3;
    msg.easing                   = Easing::BounceOut;
    msg.trail                    = false;
    Step &fx                     = *m.FindStep("s7");
    fx.intensity                 = 2;
    fx.repeat                    = 4;
    ElementType el;
    el.id          = "pod";
    el.label       = "Pod";
    el.style.shape = "hexagon";
    m.library.Upsert(el);
    m.library.Upsert(*BuiltinLibrary().Effect("shake"));
    m.library.Upsert(*BuiltinLibrary().Animation("pub-sub"));

    const auto back = ParseModel(SerializeModel(m));
    ASSERT_TRUE(back.has_value()) << back.error();
    EXPECT_EQ(*back, m);
}

TEST(JsonIoTest, LibraryReaderReportsProblems)
{
    json::Warnings   w;
    const json::Json j   = json::Json::parse(R"({"elements": [{"label": "no id"}],
      "effects": [{"id": "fx", "tracks": [{"property": "blur", "keys": []}, {"property": "glow", "keys": [{"t": 2, "value": 1}]}]}]})");
    const LibrarySet set = json::LibraryFromJson(j, &w);
    EXPECT_TRUE(set.elements.empty());
    ASSERT_EQ(set.effects.size(), 1U);
    ASSERT_EQ(set.effects[0].tracks.size(), 1U);
    EXPECT_DOUBLE_EQ(set.effects[0].tracks[0].keys[0].t, 1); // clamped
    EXPECT_EQ(w.size(), 2U);
}

TEST(JsonIoTest, ToleratesWrongTypes)
{
    const auto m =
        ParseModel(R"({"nodes": [{"id": "a", "x": "12", "w": null, "label": 5, "style": "oops"}], "scenario": {"duration": "x"}})");
    ASSERT_TRUE(m.has_value());
    EXPECT_DOUBLE_EQ(m->nodes[0].x, 0);
    EXPECT_DOUBLE_EQ(m->nodes[0].w, kDefaultNodeW);
    EXPECT_EQ(m->nodes[0].label, "5");
    EXPECT_TRUE(m->nodes[0].style.Empty());
}
