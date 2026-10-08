#ifndef _GEOMETRY_GEOMETRY_HPP_
#define _GEOMETRY_GEOMETRY_HPP_

#include <cmath>

/// @file Geometry.hpp
/// @brief Basic 2D primitives (Vec2, Rect, Bounds) and edge geometry helpers.

namespace ad
{

struct Vec2
{
    double x = 0;
    double y = 0;

    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator*(Vec2 a, double k) { return {a.x * k, a.y * k}; }
    friend constexpr Vec2 operator/(Vec2 a, double k) { return {a.x / k, a.y / k}; }
    friend constexpr bool operator==(Vec2, Vec2) = default;
};

inline double Length(Vec2 v) { return std::hypot(v.x, v.y); }
inline double Distance(Vec2 a, Vec2 b) { return Length(b - a); }
inline Vec2   Lerp(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }
/// Unit vector (or {1, 0} for a zero vector).
Vec2 Normalized(Vec2 v);

struct Rect
{
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;

    [[nodiscard]] Vec2    Center() const { return {x + w / 2, y + h / 2}; }
    [[nodiscard]] double  Right() const { return x + w; }
    [[nodiscard]] double  Bottom() const { return y + h; }
    [[nodiscard]] bool    Contains(Vec2 p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    [[nodiscard]] Rect    Adjusted(double d) const { return {x - d, y - d, w + 2 * d, h + 2 * d}; }
    [[nodiscard]] Rect    United(const Rect &o) const;
    friend constexpr bool operator==(const Rect &, const Rect &) = default;
};

/// Bounding box accumulator; empty until the first point is added.
class Bounds
{
public:
    void               Add(Vec2 p);
    void               Add(const Rect &r);
    [[nodiscard]] bool Empty() const { return _empty; }
    [[nodiscard]] Rect ToRect() const;

private:
    bool   _empty = true;
    double _min_x = 0;
    double _min_y = 0;
    double _max_x = 0;
    double _max_y = 0;
};

namespace geom
{

/// Intersection of the ray from the rectangle center (half sizes hw, hh) towards `toward` with its border.
Vec2 BorderPoint(Vec2 center, double hw, double hh, Vec2 toward);

/// Ray from the ellipse center towards `toward` intersected with the ellipse border.
Vec2 EllipseBorderPoint(Vec2 center, double rx, double ry, Vec2 toward);

/// Control point of a curved edge: the midpoint shifted perpendicular to the center line
/// (curve is a fraction of the length, the sign selects the side).
Vec2 PerpControl(Vec2 ca, Vec2 cb, double curve);

/// Move `tip` towards `from` by `dist` (the line ends at the arrow base, the arrow tip stays at `tip`).
Vec2 PullBack(Vec2 from, Vec2 tip, double dist);

double DistToSegment(Vec2 p, Vec2 a, Vec2 b);

} // namespace geom

} // namespace ad

#endif // _GEOMETRY_GEOMETRY_HPP_
