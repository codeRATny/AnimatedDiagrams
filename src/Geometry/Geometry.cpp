#include "Geometry.hpp"

#include <algorithm>
#include <limits>

namespace ad
{

Vec2 Normalized(Vec2 v)
{
    const double len = Length(v);
    if (len <= 0)
    {
        return {1, 0};
    }
    return v / len;
}

Rect Rect::United(const Rect &o) const
{
    Bounds b;
    b.Add(*this);
    b.Add(o);
    return b.ToRect();
}

void Bounds::Add(Vec2 p)
{
    if (_empty)
    {
        _min_x = _max_x = p.x;
        _min_y = _max_y = p.y;
        _empty          = false;
        return;
    }
    _min_x = std::min(_min_x, p.x);
    _min_y = std::min(_min_y, p.y);
    _max_x = std::max(_max_x, p.x);
    _max_y = std::max(_max_y, p.y);
}

void Bounds::Add(const Rect &r)
{
    Add(Vec2{r.x, r.y});
    Add(Vec2{r.Right(), r.Bottom()});
}

Rect Bounds::ToRect() const
{
    if (_empty)
    {
        return {};
    }
    return {_min_x, _min_y, _max_x - _min_x, _max_y - _min_y};
}

namespace geom
{

Vec2 BorderPoint(Vec2 center, double hw, double hh, Vec2 toward)
{
    const double dx = toward.x - center.x;
    const double dy = toward.y - center.y;
    if (dx == 0 && dy == 0)
    {
        return center;
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const double     sx   = dx != 0 ? hw / std::abs(dx) : kInf;
    const double     sy   = dy != 0 ? hh / std::abs(dy) : kInf;
    const double     s    = std::min(sx, sy);
    return {center.x + dx * s, center.y + dy * s};
}

Vec2 EllipseBorderPoint(Vec2 center, double rx, double ry, Vec2 toward)
{
    const double dx = toward.x - center.x;
    const double dy = toward.y - center.y;
    if ((dx == 0 && dy == 0) || rx <= 0 || ry <= 0)
    {
        return center;
    }
    const double k = 1.0 / std::sqrt((dx * dx) / (rx * rx) + (dy * dy) / (ry * ry));
    return {center.x + dx * k, center.y + dy * k};
}

Vec2 PerpControl(Vec2 ca, Vec2 cb, double curve)
{
    const Vec2 mid = (ca + cb) / 2;
    if (curve == 0)
    {
        return mid;
    }
    const Vec2   d   = cb - ca;
    const double len = Length(d) > 0 ? Length(d) : 1.0;
    return {mid.x + (-d.y / len) * curve * len, mid.y + (d.x / len) * curve * len};
}

Vec2 PullBack(Vec2 from, Vec2 tip, double dist)
{
    const Vec2   d   = tip - from;
    const double len = Length(d) > 0 ? Length(d) : 1.0;
    const double k   = std::min(dist, len - 0.5);
    return tip - d * (k / len);
}

double DistToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2   d  = b - a;
    const double l2 = d.x * d.x + d.y * d.y;
    double       t  = l2 > 0 ? ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / l2 : 0.0;
    t               = std::clamp(t, 0.0, 1.0);
    return Distance(p, a + d * t);
}

} // namespace geom

} // namespace ad
