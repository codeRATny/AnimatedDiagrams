#include "ad/export_plan.hpp"

#include <algorithm>
#include <cmath>

namespace ad {

std::vector<double> exportFrameTimes(double durationMs, double fps) {
    std::vector<double> out;
    if (!(fps > 0) || !(durationMs >= 0)) return out;
    const auto n = std::max(1L, std::lround(durationMs / 1000 * fps));
    out.reserve(static_cast<std::size_t>(n) + 1);
    for (long i = 0; i <= n; ++i) out.push_back(std::min(durationMs, static_cast<double>(i) / fps * 1000));
    return out;
}

ExportGeometry exportGeometry(Rect world, double scale, int maxDim) {
    world.w = std::max(40.0, world.w);
    world.h = std::max(40.0, world.h);
    scale = scale > 0 ? scale : 1;
    double w = world.w * scale;
    double h = world.h * scale;
    const double k = std::min({1.0, maxDim / w, maxDim / h});
    w *= k;
    h *= k;
    auto even = [](double v) {
        auto i = static_cast<int>(std::lround(v));
        return std::max(2, i + (i % 2));
    };
    return {world, even(w), even(h)};
}

}  // namespace ad
