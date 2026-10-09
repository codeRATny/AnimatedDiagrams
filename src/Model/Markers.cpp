#include "Markers.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace ad
{

namespace
{

/// The marker splits the scenario (it is neither at the start nor at / after the end).
bool IsBoundary(const Marker &m, double duration) { return m.time > kMarkerEpsilon && m.time < duration - kMarkerEpsilon; }

} // namespace

void NormalizeMarkers(Scenario &s)
{
    const double duration = std::max(0.0, s.duration);
    for (auto &m : s.markers)
    {
        m.time = std::clamp(m.time, 0.0, duration);
    }
    std::ranges::stable_sort(s.markers, {}, &Marker::time);
    std::set<std::string> ids;
    std::vector<Marker>   kept;
    kept.reserve(s.markers.size());
    for (auto &m : s.markers)
    {
        const bool same_time = !kept.empty() && m.time - kept.back().time < kMarkerEpsilon;
        const bool same_id   = !m.id.empty() && ids.contains(m.id);
        if (same_time || same_id)
        {
            continue;
        }
        ids.insert(m.id);
        kept.push_back(std::move(m));
    }
    s.markers = std::move(kept);
}

std::vector<Segment> ScenarioSegments(const Model &m) { return ScenarioSegments(m.scenario); }

std::vector<Segment> ScenarioSegments(const Scenario &s)
{
    const double         duration = std::max(0.0, s.duration);
    std::vector<Segment> out;
    Segment              cur;
    if (const Marker *first = MarkerAt(s, 0); first != nullptr)
    {
        cur.marker_id = first->id;
        cur.label     = first->label;
    }
    std::vector<const Marker *> bounds;
    for (const auto &m : s.markers)
    {
        if (IsBoundary(m, duration))
        {
            bounds.push_back(&m);
        }
    }
    std::ranges::stable_sort(bounds, {}, &Marker::time); // markers may be unsorted during a drag
    for (const Marker *m : bounds)
    {
        if (m->time - cur.start < kMarkerEpsilon)
        {
            continue; // duplicate time
        }
        cur.end = m->time;
        out.push_back(cur);
        cur = Segment{.index = out.size(), .start = m->time, .end = 0, .marker_id = m->id, .label = m->label};
    }
    cur.end = duration;
    out.push_back(cur);
    return out;
}

size_t SegmentIndexAt(const std::vector<Segment> &segments, double t)
{
    for (size_t i = 0; i < segments.size(); ++i)
    {
        if (t < segments[i].end)
        {
            return i;
        }
    }
    return segments.empty() ? 0 : segments.size() - 1;
}

std::optional<double> NextStop(const Scenario &s, double t)
{
    for (const auto &seg : ScenarioSegments(s))
    {
        if (seg.end > t + kMarkerEpsilon)
        {
            return seg.end;
        }
    }
    return std::nullopt;
}

double PreviousStop(const Scenario &s, double t)
{
    double best = 0;
    for (const auto &seg : ScenarioSegments(s))
    {
        if (seg.start < t - kMarkerEpsilon)
        {
            best = seg.start;
        }
    }
    return best;
}

std::optional<double> MarkerCrossed(const Scenario &s, double from, double to)
{
    std::optional<double> hit;
    for (const auto &m : s.markers)
    {
        if (IsBoundary(m, s.duration) && m.time > from && m.time <= to && (!hit.has_value() || m.time < *hit))
        {
            hit = m.time;
        }
    }
    return hit;
}

const Marker *MarkerAt(const Scenario &s, double time)
{
    const auto it = std::ranges::find_if(s.markers,
                                         [time](const Marker &m)
                                         {
                                             return std::abs(m.time - time) < kMarkerEpsilon;
                                         });
    return it != s.markers.end() ? &*it : nullptr;
}

} // namespace ad
