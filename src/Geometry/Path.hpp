#ifndef _GEOMETRY_PATH_HPP_
#define _GEOMETRY_PATH_HPP_

#include <span>
#include <string_view>
#include <vector>

#include "Geometry/Geometry.hpp"

/// @file Path.hpp
/// @brief Vector paths (line / quadratic / cubic segments), Catmull-Rom curves,
///        SVG path data (parsed by nanosvg) and arc-length parametrisation (FlatPath).

namespace ad
{

class Path
{
public:
    enum class Kind
    {
        Move,
        Line,
        Quad,
        Cubic,
        Close
    };

    struct Segment
    {
        Kind        kind = Kind::Move;
        Vec2        c1;
        Vec2        c2;
        Vec2        to;
        friend bool operator==(const Segment &, const Segment &) = default;
    };

    void MoveTo(Vec2 p);
    void LineTo(Vec2 p);
    void QuadTo(Vec2 c, Vec2 to);
    void CubicTo(Vec2 c1, Vec2 c2, Vec2 to);
    void Close();
    /// Append all segments of another path.
    void Append(const Path &other);

    [[nodiscard]] const std::vector<Segment> &Segments() const { return _segs; }
    [[nodiscard]] bool                        Empty() const { return _segs.size() < 2; }
    [[nodiscard]] Vec2                        StartPoint() const;
    [[nodiscard]] Vec2                        EndPoint() const;
    /// Unit tangent at the start (along the path) and at the end.
    [[nodiscard]] Vec2 StartDirection() const;
    [[nodiscard]] Vec2 EndDirection() const;
    /// Path scaled from the unit square into `r` (used for normalised custom shapes).
    [[nodiscard]] Path MappedTo(double x, double y, double w, double h) const;

    static Path Line(Vec2 a, Vec2 b);
    static Path Quad(Vec2 a, Vec2 c, Vec2 b);
    /// Interpolating Catmull-Rom curve (same output as d3.curveCatmullRom.alpha(alpha)).
    static Path CatmullRom(std::span<const Vec2> pts, double alpha = 0.5);
    /// Ellipse made of four cubic segments.
    static Path Ellipse(Vec2 center, double rx, double ry);
    static Path RoundedRect(double x, double y, double w, double h, double radius);
    static Path Polygon(std::span<const Vec2> pts);

    friend bool operator==(const Path &, const Path &) = default;

private:
    std::vector<Segment> _segs;
};

/// Parse SVG path data (all commands: M L H V C S Q T A Z, absolute and relative) with nanosvg.
/// Straight segments become lines, curves and arcs cubic segments, `Z` closes the sub-path.
/// Blank data gives an empty path; data without any drawable segment throws ad::ParseError.
Path ParseSvgPath(std::string_view data);

/// Path flattened into a polyline with cumulative arc length.
/// Works on the first sub-path only (edges are single sub-paths).
class FlatPath
{
public:
    explicit FlatPath(const Path &path, int steps_per_curve = 32);

    [[nodiscard]] double Length() const { return _cum.empty() ? 0.0 : _cum.back(); }
    [[nodiscard]] Vec2   PointAtLength(double len) const;
    [[nodiscard]] Vec2   PointAtFraction(double f) const { return PointAtLength(f * Length()); }
    /// Unit direction at `len` along the path (forward) or against it.
    [[nodiscard]] Vec2 DirectionAt(double len, bool forward = true) const;
    /// Point at fraction pos in [0, 1] shifted by `off` along the normal ("up" on screen is positive).
    [[nodiscard]] Vec2                     PointAlong(double pos, double off) const;
    [[nodiscard]] double                   DistanceTo(Vec2 p) const;
    [[nodiscard]] const std::vector<Vec2> &Points() const { return _pts; }

private:
    std::vector<Vec2>   _pts;
    std::vector<double> _cum;
};

} // namespace ad

#endif // _GEOMETRY_PATH_HPP_
