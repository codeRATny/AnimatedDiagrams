#ifndef _ENGINE_ENGINE_HPP_
#define _ENGINE_ENGINE_HPP_

#include <optional>
#include <string>
#include <vector>

#include "Geometry/Path.hpp"
#include "Model/Color.hpp"
#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file Engine.hpp
/// @brief Deterministic time model: everything visible at moment t is a pure
///        function of (model, t). Node states, active steps, resolved styles, edge geometry.

namespace ad
{

/// Length of an arrow head in world units.
inline constexpr double kArrowLen = 12;

/// State step active for the node at t (nullptr -- default state).
/// A step is active on [start, end); among several the latest started wins.
/// A step that lasts until the end of the scene stays active at t == scene end
/// (otherwise the last exported frame would reset all states).
const Step *StateAt(const Model &m, std::string_view node_id, double t);

/// Steps active at t (start <= t <= end) in scenario order.
std::vector<const Step *> ActiveSteps(const Model &m, double t);

struct ResolvedState
{
    std::string           id = "ok";
    std::string           label;
    Color                 fill;
    Color                 ring;
    std::optional<double> label_size;
    bool                  custom_label = false;
};

/// State preset + step overrides (label, accent color, font size). The default state
/// uses the node style fill / stroke when they are set.
ResolvedState ResolveNodeState(const Step *step, const NodeStyle &style = {});

/// Effective style of a node: element type style overridden by the node style.
NodeStyle ResolveNodeStyle(const Model &m, const Node &n, const Registry &reg = Registry::Default());

/// Shape id of a node after style resolution ("rounded" by default).
std::string ResolveShape(const NodeStyle &style);

struct EdgeGeometry
{
    Vec2              start;     // where the line starts (after the start arrow)
    Vec2              end;       // where the line ends (arrow base)
    Vec2              raw_start; // tip of the start arrow
    Vec2              raw_end;   // tip of the end arrow
    std::vector<Vec2> points;    // start, waypoints..., end
    Path              path;
    bool              arrow_start = false;
    bool              arrow_end   = true;
};

/// Edge geometry with ports, waypoints, routing and arrows; nullopt when a node is missing.
std::optional<EdgeGeometry> ComputeEdgeGeometry(const Model &m, const Edge &e, const Registry &reg = Registry::Default());

/// Index for inserting a waypoint: the polyline segment closest to p.
size_t NearestSegmentIndex(const Model &m, const Edge &e, Vec2 p, const Registry &reg = Registry::Default());

} // namespace ad

#endif // _ENGINE_ENGINE_HPP_
