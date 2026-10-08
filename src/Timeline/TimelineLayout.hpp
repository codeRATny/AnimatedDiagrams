#ifndef _TIMELINE_TIMELINE_LAYOUT_HPP_
#define _TIMELINE_TIMELINE_LAYOUT_HPP_

#include <string>
#include <vector>

#include "Model/Color.hpp"
#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file TimelineLayout.hpp
/// @brief UI-independent timeline logic: lane packing, ruler ticks, bar titles.

namespace ad
{

struct LaneAssignment
{
    std::string step_id;
    int         lane = 0;
};

/// Steps that do not overlap in time share a lane; parallel ones go to separate lanes.
std::vector<LaneAssignment> PackLanes(const std::vector<Step> &steps);
int                         LaneCount(const std::vector<LaneAssignment> &lanes);

/// "Nice" ticks on [start, stop] (as d3.ticks).
std::vector<double> NiceTicks(double start, double stop, int count);
double              TickStep(double start, double stop, int count);
/// Tick label with the precision matching the step.
std::string FormatTick(double value, double step);

/// End of the visible scale: max(scene duration, end of the last step).
double ContentEnd(const Model &m);

std::string StepTitle(const Model &m, const Step &s, const Registry &reg = Registry::Default());
Color       StepColor(const Step &s);

/// Snap to a grid (ms), not less than 0.
double SnapTime(double ms, double grid = 50);

/// "1.25с" -- seconds with two decimals below 10 s.
std::string FormatTime(double ms);

} // namespace ad

#endif // _TIMELINE_TIMELINE_LAYOUT_HPP_
