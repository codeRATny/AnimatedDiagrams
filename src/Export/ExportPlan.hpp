#ifndef _EXPORT_EXPORT_PLAN_HPP_
#define _EXPORT_EXPORT_PLAN_HPP_

#include <vector>

#include "Geometry/Geometry.hpp"

/// @file ExportPlan.hpp
/// @brief Frame grid and output size of an export (renderer independent).

namespace ad
{

/// Frame times: 0, 1/fps, ... and exactly `duration` as the last frame.
std::vector<double> ExportFrameTimes(double duration_ms, double fps);

struct ExportGeometry
{
    Rect world; // part of the world visible in the frame
    int  px_w = 0;
    int  px_h = 0;
};

/// Frame size: world * scale, the larger side <= max_dim (keeping the aspect ratio),
/// even dimensions (video codecs require them).
ExportGeometry MakeExportGeometry(Rect world, double scale, int max_dim = 12000);

/// Padding around the content: room for timer rings and packet labels.
inline constexpr double kExportPadding = 74;

} // namespace ad

#endif // _EXPORT_EXPORT_PLAN_HPP_
