#include "ExportPlan.hpp"

#include <algorithm>
#include <cmath>

namespace ad
{

std::vector<double> ExportFrameTimes(double duration_ms, double fps)
{
    std::vector<double> out;
    if (fps <= 0 || duration_ms < 0)
    {
        return out;
    }
    const int64_t n = std::max<int64_t>(1, std::llround(duration_ms / 1000 * fps));
    out.reserve(static_cast<size_t>(n) + 1);
    for (int64_t i = 0; i <= n; ++i)
    {
        out.push_back(std::min(duration_ms, static_cast<double>(i) / fps * 1000));
    }
    return out;
}

ExportGeometry MakeExportGeometry(Rect world, double scale, int max_dim)
{
    world.w        = std::max(40.0, world.w);
    world.h        = std::max(40.0, world.h);
    scale          = scale > 0 ? scale : 1;
    double       w = world.w * scale;
    double       h = world.h * scale;
    const double k = std::min({1.0, max_dim / w, max_dim / h});
    w *= k;
    h *= k;
    auto even = [](double v)
    {
        const int i = static_cast<int>(std::lround(v));
        return std::max(2, i + (i % 2));
    };
    return {world, even(w), even(h)};
}

} // namespace ad
