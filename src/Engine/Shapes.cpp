#include "Shapes.hpp"

#include <algorithm>
#include <numbers>

#include "Common/Exceptions.hpp"

namespace ad
{

namespace
{

constexpr double kKappa = 0.5522847498307936;

/// Half of an ellipse between its left and right extreme points through the top (upper)
/// or bottom; drawn from left to right when `left_to_right`.
void HalfEllipse(Path &p, Vec2 c, double rx, double ry, bool upper, bool left_to_right)
{
    const double sy = upper ? -1.0 : 1.0;
    const double ox = rx * kKappa;
    const double oy = ry * kKappa * sy;
    const Vec2   l{c.x - rx, c.y};
    const Vec2   r{c.x + rx, c.y};
    const Vec2   m{c.x, c.y + ry * sy};
    if (left_to_right)
    {
        p.CubicTo({l.x, l.y + oy}, {m.x - ox, m.y}, m);
        p.CubicTo({m.x + ox, m.y}, {r.x, r.y + oy}, r);
    }
    else
    {
        p.CubicTo({r.x, r.y + oy}, {m.x + ox, m.y}, m);
        p.CubicTo({m.x - ox, m.y}, {l.x, l.y + oy}, l);
    }
}

double CylinderCap(const Rect &r) { return std::min(r.h * 0.18, 12.0); }

} // namespace

double DefaultCornerRadius(std::string_view shape)
{
    if (shape == "rounded")
    {
        return 14;
    }
    if (shape == "queue")
    {
        return 4;
    }
    return 0;
}

bool IsCenteredShape(std::string_view shape) { return shape == "ellipse" || shape == "diamond" || shape == "cloud"; }

Path ShapeOutline(std::string_view shape, const Rect &r, double corner_radius, const std::string &custom_path)
{
    const double cx = r.x + r.w / 2;
    const double cy = r.y + r.h / 2;
    if (shape == "ellipse")
    {
        return Path::Ellipse({cx, cy}, r.w / 2, r.h / 2);
    }
    if (shape == "diamond")
    {
        const Vec2 pts[4]{{cx, r.y}, {r.Right(), cy}, {cx, r.Bottom()}, {r.x, cy}};
        return Path::Polygon(pts);
    }
    if (shape == "hexagon")
    {
        const double k = std::min(r.w * 0.18, r.h * 0.5);
        const Vec2   pts[6]{{r.x + k, r.y}, {r.Right() - k, r.y}, {r.Right(), cy}, {r.Right() - k, r.Bottom()}, {r.x + k, r.Bottom()},
                            {r.x, cy}};
        return Path::Polygon(pts);
    }
    if (shape == "parallelogram")
    {
        const double k = std::min(r.w * 0.15, 20.0);
        const Vec2   pts[4]{{r.x + k, r.y}, {r.Right(), r.y}, {r.Right() - k, r.Bottom()}, {r.x, r.Bottom()}};
        return Path::Polygon(pts);
    }
    if (shape == "cylinder")
    {
        const double e = CylinderCap(r);
        Path         p;
        p.MoveTo({r.x, r.y + e});
        p.LineTo({r.x, r.Bottom() - e});
        HalfEllipse(p, {cx, r.Bottom() - e}, r.w / 2, e, false, true);
        p.LineTo({r.Right(), r.y + e});
        HalfEllipse(p, {cx, r.y + e}, r.w / 2, e, true, false);
        p.Close();
        return p;
    }
    if (shape == "document")
    {
        const double a = std::min(r.h * 0.12, 10.0);
        Path         p;
        p.MoveTo({r.x, r.y});
        p.LineTo({r.Right(), r.y});
        p.LineTo({r.Right(), r.Bottom() - a});
        p.CubicTo({r.x + r.w * 0.75, r.Bottom() - 3 * a}, {r.x + r.w * 0.25, r.Bottom() + a}, {r.x, r.Bottom() - a});
        p.Close();
        return p;
    }
    if (shape == "note")
    {
        const double f = std::min({14.0, r.w / 4, r.h / 4});
        const Vec2   pts[5]{{r.x, r.y}, {r.Right() - f, r.y}, {r.Right(), r.y + f}, {r.Right(), r.Bottom()}, {r.x, r.Bottom()}};
        return Path::Polygon(pts);
    }
    if (shape == "cloud")
    {
        constexpr int kBumps = 9;
        const Vec2    c{cx, cy};
        const double  rx = r.w / 2 * 0.80;
        const double  ry = r.h / 2 * 0.72;
        auto          at = [&](int i)
        {
            const double a = 2 * std::numbers::pi * i / kBumps - std::numbers::pi / 2;
            return Vec2{c.x + rx * std::cos(a), c.y + ry * std::sin(a)};
        };
        Path p;
        p.MoveTo(at(0));
        for (int i = 0; i < kBumps; ++i)
        {
            const Vec2 a = at(i);
            const Vec2 b = at(i + 1);
            const Vec2 m = (a + b) / 2;
            p.QuadTo(c + (m - c) * 1.42, b);
        }
        p.Close();
        return p;
    }
    if (shape == "custom" && !custom_path.empty())
    {
        try
        {
            const Path unit = ParseSvgPath(custom_path);
            if (!unit.Empty())
            {
                return unit.MappedTo(r.x, r.y, r.w, r.h);
            }
        }
        catch (const ParseError &)
        {
            // invalid custom outline: fall back to the rounded rectangle below
        }
        return Path::RoundedRect(r.x, r.y, r.w, r.h, 14);
    }
    const double radius = shape == "rect" || shape == "rounded" || shape == "queue" ? corner_radius : 14;
    return Path::RoundedRect(r.x, r.y, r.w, r.h, radius);
}

Path ShapeDecoration(std::string_view shape, const Rect &r)
{
    Path p;
    if (shape == "cylinder")
    {
        const double e = CylinderCap(r);
        p.MoveTo({r.x, r.y + e});
        HalfEllipse(p, {r.x + r.w / 2, r.y + e}, r.w / 2, e, false, true);
    }
    else if (shape == "queue")
    {
        for (int i = 0; i < 3; ++i)
        {
            const double x = r.Right() - 12 - i * 8.0;
            p.MoveTo({x, r.y + 10});
            p.LineTo({x, r.Bottom() - 10});
        }
    }
    else if (shape == "note")
    {
        const double f = std::min({14.0, r.w / 4, r.h / 4});
        p.MoveTo({r.Right() - f, r.y});
        p.LineTo({r.Right() - f, r.y + f});
        p.LineTo({r.Right(), r.y + f});
    }
    return p;
}

Vec2 ShapeBorderPoint(std::string_view shape, const Rect &r, Vec2 toward, double gap)
{
    const Vec2   c  = r.Center();
    const double hw = r.w / 2 + gap;
    const double hh = r.h / 2 + gap;
    if (shape == "ellipse" || shape == "cloud")
    {
        return geom::EllipseBorderPoint(c, hw, hh, toward);
    }
    if (shape == "diamond")
    {
        const double dx = toward.x - c.x;
        const double dy = toward.y - c.y;
        if (dx == 0 && dy == 0)
        {
            return c;
        }
        const double s = 1.0 / (std::abs(dx) / hw + std::abs(dy) / hh);
        return {c.x + dx * s, c.y + dy * s};
    }
    return geom::BorderPoint(c, hw, hh, toward);
}

} // namespace ad
