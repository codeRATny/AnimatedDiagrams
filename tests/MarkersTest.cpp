#include <gtest/gtest.h>

#include "Io/JsonCodec.hpp"
#include "Io/JsonIo.hpp"
#include "Model/Document.hpp"
#include "Model/Markers.hpp"
#include "Model/Sample.hpp"

using namespace ad;

namespace
{

Scenario WithMarkers(double duration, std::vector<Marker> markers)
{
    Scenario s;
    s.duration = duration;
    s.markers  = std::move(markers);
    return s;
}

} // namespace

TEST(MarkersTest, NoMarkersIsOneSegment)
{
    const auto segs = ScenarioSegments(WithMarkers(5000, {}));
    ASSERT_EQ(segs.size(), 1U);
    EXPECT_EQ(segs[0], (Segment{.index = 0, .start = 0, .end = 5000, .marker_id = {}, .label = {}}));
    EXPECT_DOUBLE_EQ(segs[0].Duration(), 5000);
}

TEST(MarkersTest, MarkersSplitTheScenario)
{
    const auto segs = ScenarioSegments(WithMarkers(9000, {{"m_a", 2000, "Request"}, {"m_b", 6000, "Retry"}}));
    ASSERT_EQ(segs.size(), 3U);
    EXPECT_EQ(segs[0], (Segment{.index = 0, .start = 0, .end = 2000, .marker_id = {}, .label = {}}));
    EXPECT_EQ(segs[1], (Segment{.index = 1, .start = 2000, .end = 6000, .marker_id = "m_a", .label = "Request"}));
    EXPECT_EQ(segs[2], (Segment{.index = 2, .start = 6000, .end = 9000, .marker_id = "m_b", .label = "Retry"}));
}

TEST(MarkersTest, MarkersAtTheEdgesDoNotSplit)
{
    // a marker at 0 names the first segment, one at the end (or after it) is ignored
    const auto segs = ScenarioSegments(WithMarkers(4000, {{"m_0", 0, "Intro"}, {"m_e", 4000, "End"}, {"m_x", 7000, "After"}}));
    ASSERT_EQ(segs.size(), 1U);
    EXPECT_EQ(segs[0].label, "Intro");
    EXPECT_EQ(segs[0].marker_id, "m_0");
    EXPECT_DOUBLE_EQ(segs[0].end, 4000);
}

TEST(MarkersTest, SegmentsAreSortedEvenForUnsortedMarkers)
{
    const auto segs = ScenarioSegments(WithMarkers(9000, {{"m_b", 6000, "B"}, {"m_a", 2000, "A"}}));
    ASSERT_EQ(segs.size(), 3U);
    EXPECT_EQ(segs[1].label, "A");
    EXPECT_EQ(segs[2].label, "B");
}

TEST(MarkersTest, SegmentIndexAt)
{
    const auto segs = ScenarioSegments(WithMarkers(9000, {{"m_a", 2000, ""}, {"m_b", 6000, ""}}));
    EXPECT_EQ(SegmentIndexAt(segs, -5), 0U);
    EXPECT_EQ(SegmentIndexAt(segs, 0), 0U);
    EXPECT_EQ(SegmentIndexAt(segs, 1999), 0U);
    EXPECT_EQ(SegmentIndexAt(segs, 2000), 1U);
    EXPECT_EQ(SegmentIndexAt(segs, 8999), 2U);
    EXPECT_EQ(SegmentIndexAt(segs, 9000), 2U);
    EXPECT_EQ(SegmentIndexAt(segs, 1e9), 2U);
}

TEST(MarkersTest, NextAndPreviousStops)
{
    const Scenario s = WithMarkers(9000, {{"m_a", 2000, ""}, {"m_b", 6000, ""}});
    EXPECT_EQ(NextStop(s, 0), 2000);
    EXPECT_EQ(NextStop(s, 2000), 6000); // standing on a marker: the next one
    EXPECT_EQ(NextStop(s, 3000), 6000);
    EXPECT_EQ(NextStop(s, 6500), 9000);
    EXPECT_FALSE(NextStop(s, 9000).has_value());

    EXPECT_DOUBLE_EQ(PreviousStop(s, 9000), 6000);
    EXPECT_DOUBLE_EQ(PreviousStop(s, 6000), 2000); // standing on a marker: the previous one
    EXPECT_DOUBLE_EQ(PreviousStop(s, 4000), 2000);
    EXPECT_DOUBLE_EQ(PreviousStop(s, 2000), 0);
    EXPECT_DOUBLE_EQ(PreviousStop(s, 0), 0);
}

TEST(MarkersTest, MarkerCrossed)
{
    const Scenario s = WithMarkers(9000, {{"m_a", 2000, ""}, {"m_b", 6000, ""}, {"m_0", 0, ""}});
    EXPECT_FALSE(MarkerCrossed(s, 0, 1999).has_value());
    EXPECT_EQ(MarkerCrossed(s, 1990, 2010), 2000);
    EXPECT_FALSE(MarkerCrossed(s, 2000, 2016).has_value()); // resuming from a marker does not stop again
    EXPECT_EQ(MarkerCrossed(s, 1000, 7000), 2000);          // a long frame stops at the first one
    EXPECT_FALSE(MarkerCrossed(s, -1, 10).has_value());     // a marker at 0 is not a stop
}

TEST(MarkersTest, NormalizeClampsSortsAndDropsDuplicates)
{
    Scenario s = WithMarkers(
        5000, {{"m_c", 9000, "late"}, {"m_a", 3000, "a"}, {"m_b", 3000.2, "dup time"}, {"m_a", 4000, "dup id"}, {"m_n", -50, "neg"}});
    NormalizeMarkers(s);
    ASSERT_EQ(s.markers.size(), 3U);
    EXPECT_EQ(s.markers[0], (Marker{"m_n", 0, "neg"}));
    EXPECT_EQ(s.markers[1], (Marker{"m_a", 3000, "a"}));
    EXPECT_EQ(s.markers[2], (Marker{"m_c", 5000, "late"}));
}

TEST(MarkersTest, JsonRoundTripAndOmittedWhenEmpty)
{
    Model m = SampleModel();
    EXPECT_FALSE(json::ToJson(m)["scenario"].contains("markers"));

    m.scenario.markers = {{"m_one", 1500, "First"}, {"m_two", 4000, ""}};
    const auto j       = json::ToJson(m);
    ASSERT_TRUE(j["scenario"].contains("markers"));
    EXPECT_EQ(j["scenario"]["markers"][0]["id"], "m_one");
    EXPECT_EQ(j["scenario"]["markers"][0]["time"], 1500);
    EXPECT_EQ(j["scenario"]["markers"][0]["label"], "First");

    const auto parsed = ParseModel(SerializeModel(m));
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    EXPECT_EQ(parsed->scenario.markers, m.scenario.markers);
    EXPECT_EQ(*parsed, m);
}

TEST(MarkersTest, ParseNormalizesMarkers)
{
    constexpr std::string_view kDoc   = R"({
      "nodes": [{"id": "n_a", "label": "A"}],
      "scenario": {"duration": 5000, "steps": [], "markers": [
        {"id": "m_late", "time": 8000, "label": "too late"},
        {"id": "m_x", "time": 1000, "label": "first"},
        {"id": "m_x", "time": 4000, "label": "same id"},
        {"time": 2000, "label": "no id"},
        {"id": "n_a", "time": 3000, "label": "id of a node"},
        {"id": "m_d", "time": 3000, "label": "same time"},
        {"id": "m_bad", "label": "no time"},
        "garbage"
      ]}
    })";
    const auto                 parsed = ParseModel(kDoc);
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    const auto &mk = parsed->scenario.markers;
    ASSERT_EQ(mk.size(), 4U);
    EXPECT_EQ(mk[0], (Marker{"m_x", 1000, "first"}));
    EXPECT_EQ(mk[1].label, "no id");
    EXPECT_TRUE(mk[1].id.starts_with("m_"));
    EXPECT_EQ(mk[2].label, "id of a node");
    EXPECT_NE(mk[2].id, "n_a");
    EXPECT_EQ(mk[3], (Marker{"m_late", 5000, "too late"}));
}

TEST(MarkersTest, DocumentAddMoveRenameRemoveAreUndoable)
{
    Document d(SampleModel());
    d.Mutable().scenario.markers.clear();
    const Model before = d.Get();

    Marker *m = d.AddMarker(2000, "Step 1");
    ASSERT_NE(m, nullptr);
    const std::string id = m->id;
    EXPECT_TRUE(id.starts_with("m_"));
    EXPECT_EQ(d.AddMarker(2000.2), nullptr); // same time
    ASSERT_NE(d.AddMarker(1e9), nullptr);    // clamped to the end
    EXPECT_DOUBLE_EQ(d.Get().scenario.markers.back().time, d.Get().scenario.duration);

    EXPECT_TRUE(d.MoveMarker(id, 3000));
    EXPECT_FALSE(d.MoveMarker(id, d.Get().scenario.duration)); // occupied
    EXPECT_FALSE(d.MoveMarker("m_missing", 10));
    EXPECT_TRUE(d.RenameMarker(id, "Renamed"));
    EXPECT_EQ(d.Get().FindMarker(id)->label, "Renamed");
    EXPECT_DOUBLE_EQ(d.Get().FindMarker(id)->time, 3000);
    EXPECT_TRUE(d.RemoveMarker(id));
    EXPECT_FALSE(d.RemoveMarker(id));
    EXPECT_EQ(d.Get().FindMarker(id), nullptr);

    int undone = 0;
    while (d.Undo())
    {
        ++undone;
    }
    EXPECT_EQ(undone, 5); // failed operations create no history entries
    EXPECT_EQ(d.Get(), before);
}

TEST(MarkersTest, DragMovesMergeIntoOneUndoStep)
{
    Document          d(SampleModel());
    const std::string id = d.AddMarker(1000)->id;
    EXPECT_TRUE(d.MoveMarker(id, 1100, "marker-drag"));
    EXPECT_TRUE(d.MoveMarker(id, 1200, "marker-drag"));
    EXPECT_TRUE(d.MoveMarker(id, 1300, "marker-drag"));
    d.BreakMerge();
    EXPECT_TRUE(d.Undo());
    EXPECT_DOUBLE_EQ(d.Get().FindMarker(id)->time, 1000);
}

TEST(MarkersTest, MarkersKeepTheSceneLongEnough)
{
    Document d;
    d.Mutable().scenario.duration = 20000;
    ASSERT_NE(d.AddMarker(15000), nullptr);
    d.UpdateDuration(); // automatic duration (no steps) would be 4 s
    EXPECT_DOUBLE_EQ(d.Get().scenario.duration, 15000);

    d.SetDuration(8000); // the user shortens the scene: the marker moves to the new end
    EXPECT_DOUBLE_EQ(d.Get().scenario.markers[0].time, 8000);
}
