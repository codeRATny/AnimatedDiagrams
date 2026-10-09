#ifndef _MODEL_MARKERS_HPP_
#define _MODEL_MARKERS_HPP_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Model/Model.hpp"

/// @file Markers.hpp
/// @brief Scenario markers (chapters): normalization, the segments between them and
///        navigation helpers. Used by the timeline, the presenter mode, playback
///        ("stop at markers") and exporters (one clip / slide per segment).
///
///        Segments are half-open intervals [start, end) of scenario time that cover
///        [0, duration] without gaps: markers strictly inside (0, duration) are the
///        boundaries; a scenario without such markers is one segment.

namespace ad
{

/// Two markers closer than this (ms) are duplicates.
inline constexpr double kMarkerEpsilon = 0.5;

/// One part of the scenario between two consecutive boundaries (0, markers, duration).
struct Segment
{
    size_t      index = 0; // 0-based position in the list
    double      start = 0; // ms, inclusive
    double      end   = 0; // ms, exclusive (== duration for the last segment)
    std::string marker_id; // marker that opens the segment (empty for the first one without a marker at 0)
    std::string label;     // label of that marker

    [[nodiscard]] double Duration() const { return end - start; }
    friend bool          operator==(const Segment &, const Segment &) = default;
};

/// Clamp marker times to [0, duration], sort by time and drop duplicates
/// (same id or the same time within kMarkerEpsilon: the first one wins). Ids are not generated.
void NormalizeMarkers(Scenario &s);

/// Segments of the scenario split by its markers (at least one; ordered by time).
[[nodiscard]] std::vector<Segment> ScenarioSegments(const Model &m);
[[nodiscard]] std::vector<Segment> ScenarioSegments(const Scenario &s);

/// Index of the segment containing `t` (the last one for t >= duration, the first one for t < 0).
[[nodiscard]] size_t SegmentIndexAt(const std::vector<Segment> &segments, double t);

/// Next stop strictly after `t`: the end of the segment containing `t` (the next marker or the
/// end of the scene); nullopt when `t` is already at the end.
[[nodiscard]] std::optional<double> NextStop(const Scenario &s, double t);

/// Previous segment start strictly before `t` (0 when there is none).
[[nodiscard]] double PreviousStop(const Scenario &s, double t);

/// Time of the first marker in (from, to] that splits the scenario (stop-at-markers playback);
/// nullopt when playback from `from` to `to` crosses no marker.
[[nodiscard]] std::optional<double> MarkerCrossed(const Scenario &s, double from, double to);

/// Marker at `time` (within kMarkerEpsilon), or nullptr.
[[nodiscard]] const Marker *MarkerAt(const Scenario &s, double time);

} // namespace ad

#endif // _MODEL_MARKERS_HPP_
