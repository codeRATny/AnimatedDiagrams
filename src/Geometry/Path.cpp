#include "Path.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <numbers>
#include <string>

#include "Common/Exceptions.hpp"

namespace ad
{

namespace
{

constexpr double kKappa = 0.5522847498307936; // cubic approximation of a quarter circle

bool NonZero(Vec2 v) { return v.x != 0 || v.y != 0; }

/// First non-zero candidate as a unit vector, or {1, 0}.
Vec2 FirstDirection(std::span<const Vec2> candidates)
{
    for (const Vec2 c : candidates)
    {
        if (NonZero(c))
        {
            return c / Length(c);
        }
    }
    return {1, 0};
}

} // namespace

// ---------------------------------------------------------------------------
// Path construction
// ---------------------------------------------------------------------------

void Path::MoveTo(Vec2 p) { _segs.push_back({Kind::Move, {}, {}, p}); }

void Path::LineTo(Vec2 p)
{
    if (_segs.empty())
    {
        MoveTo(p);
        return;
    }
    _segs.push_back({Kind::Line, {}, {}, p});
}

void Path::QuadTo(Vec2 c, Vec2 to)
{
    if (_segs.empty())
    {
        MoveTo(c);
    }
    _segs.push_back({Kind::Quad, c, {}, to});
}

void Path::CubicTo(Vec2 c1, Vec2 c2, Vec2 to)
{
    if (_segs.empty())
    {
        MoveTo(c1);
    }
    _segs.push_back({Kind::Cubic, c1, c2, to});
}

void Path::Close()
{
    if (_segs.empty())
    {
        return;
    }
    // the closing segment ends at the start of the current sub-path
    Vec2 start = _segs.front().to;
    for (auto it = _segs.rbegin(); it != _segs.rend(); ++it)
    {
        if (it->kind == Kind::Move)
        {
            start = it->to;
            break;
        }
    }
    _segs.push_back({Kind::Close, {}, {}, start});
}

void Path::Append(const Path &other) { _segs.insert(_segs.end(), other._segs.begin(), other._segs.end()); }

Vec2 Path::StartPoint() const { return _segs.empty() ? Vec2{} : _segs.front().to; }
Vec2 Path::EndPoint() const { return _segs.empty() ? Vec2{} : _segs.back().to; }

Vec2 Path::StartDirection() const
{
    if (_segs.size() < 2)
    {
        return {1, 0};
    }
    const Vec2     s = _segs[0].to;
    const Segment &g = _segs[1];
    Vec2           cands[3]{g.to - s, g.to - s, g.to - s};
    if (g.kind == Kind::Quad || g.kind == Kind::Cubic)
    {
        cands[0] = g.c1 - s;
    }
    if (g.kind == Kind::Cubic)
    {
        cands[1] = g.c2 - s;
    }
    return FirstDirection(cands);
}

Vec2 Path::EndDirection() const
{
    if (_segs.size() < 2)
    {
        return {1, 0};
    }
    const Segment &g    = _segs.back();
    const Vec2     prev = _segs[_segs.size() - 2].to;
    Vec2           cands[3]{g.to - prev, g.to - prev, g.to - prev};
    if (g.kind == Kind::Quad)
    {
        cands[0] = g.to - g.c1;
    }
    if (g.kind == Kind::Cubic)
    {
        cands[0] = g.to - g.c2;
        cands[1] = g.to - g.c1;
    }
    return FirstDirection(cands);
}

Path Path::MappedTo(double x, double y, double w, double h) const
{
    auto map = [&](Vec2 p)
    {
        return Vec2{x + p.x * w, y + p.y * h};
    };
    Path out;
    out._segs.reserve(_segs.size());
    for (const auto &s : _segs)
    {
        out._segs.push_back({s.kind, map(s.c1), map(s.c2), map(s.to)});
    }
    return out;
}

Path Path::Line(Vec2 a, Vec2 b)
{
    Path p;
    p.MoveTo(a);
    p.LineTo(b);
    return p;
}

Path Path::Quad(Vec2 a, Vec2 c, Vec2 b)
{
    Path p;
    p.MoveTo(a);
    p.QuadTo(c, b);
    return p;
}

Path Path::CatmullRom(std::span<const Vec2> pts, double alpha)
{
    constexpr double kEps = 1e-12;
    Path             p;
    const size_t     n = pts.size();
    if (n == 0)
    {
        return p;
    }
    p.MoveTo(pts[0]);
    if (n == 1)
    {
        return p;
    }
    if (n == 2)
    {
        p.LineTo(pts[1]);
        return p;
    }
    // l_2a = |ab|^(2*alpha), l_a = |ab|^alpha -- as in d3-shape (catmullRom.js)
    auto l2a = [alpha](Vec2 a, Vec2 b)
    {
        const Vec2 d = b - a;
        return std::pow(d.x * d.x + d.y * d.y, alpha);
    };
    for (size_t i = 0; i + 1 < n; ++i)
    {
        const Vec2   p1     = pts[i];
        const Vec2   p2     = pts[i + 1];
        const double l12_2a = l2a(p1, p2);
        const double l12_a  = std::sqrt(l12_2a);
        Vec2         c1     = p1;
        Vec2         c2     = p2;
        if (i > 0)
        {
            const Vec2   p0     = pts[i - 1];
            const double l01_2a = l2a(p0, p1);
            const double l01_a  = std::sqrt(l01_2a);
            if (l01_a > kEps)
            {
                const double a  = 2 * l01_2a + 3 * l01_a * l12_a + l12_2a;
                const double nn = 3 * l01_a * (l01_a + l12_a);
                c1              = (p1 * a - p0 * l12_2a + p2 * l01_2a) / nn;
            }
        }
        if (i + 2 < n)
        {
            const Vec2   p3     = pts[i + 2];
            const double l23_2a = l2a(p2, p3);
            const double l23_a  = std::sqrt(l23_2a);
            if (l23_a > kEps)
            {
                const double b = 2 * l23_2a + 3 * l23_a * l12_a + l12_2a;
                const double m = 3 * l23_a * (l23_a + l12_a);
                c2             = (p2 * b + p1 * l23_2a - p3 * l12_2a) / m;
            }
        }
        p.CubicTo(c1, c2, p2);
    }
    return p;
}

Path Path::Ellipse(Vec2 c, double rx, double ry)
{
    const double ox = rx * kKappa;
    const double oy = ry * kKappa;
    Path         p;
    p.MoveTo({c.x + rx, c.y});
    p.CubicTo({c.x + rx, c.y + oy}, {c.x + ox, c.y + ry}, {c.x, c.y + ry});
    p.CubicTo({c.x - ox, c.y + ry}, {c.x - rx, c.y + oy}, {c.x - rx, c.y});
    p.CubicTo({c.x - rx, c.y - oy}, {c.x - ox, c.y - ry}, {c.x, c.y - ry});
    p.CubicTo({c.x + ox, c.y - ry}, {c.x + rx, c.y - oy}, {c.x + rx, c.y});
    p.Close();
    return p;
}

Path Path::RoundedRect(double x, double y, double w, double h, double radius)
{
    const double r = std::clamp(radius, 0.0, std::min(w, h) / 2);
    Path         p;
    if (r <= 0)
    {
        const Vec2 pts[4]{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
        return Polygon(pts);
    }
    const double k = r * (1 - kKappa);
    p.MoveTo({x + r, y});
    p.LineTo({x + w - r, y});
    p.CubicTo({x + w - k, y}, {x + w, y + k}, {x + w, y + r});
    p.LineTo({x + w, y + h - r});
    p.CubicTo({x + w, y + h - k}, {x + w - k, y + h}, {x + w - r, y + h});
    p.LineTo({x + r, y + h});
    p.CubicTo({x + k, y + h}, {x, y + h - k}, {x, y + h - r});
    p.LineTo({x, y + r});
    p.CubicTo({x, y + k}, {x + k, y}, {x + r, y});
    p.Close();
    return p;
}

Path Path::Polygon(std::span<const Vec2> pts)
{
    Path p;
    if (pts.empty())
    {
        return p;
    }
    p.MoveTo(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i)
    {
        p.LineTo(pts[i]);
    }
    p.Close();
    return p;
}

// ---------------------------------------------------------------------------
// SVG path parser
// ---------------------------------------------------------------------------

namespace
{

class SvgPathParser
{
public:
    explicit SvgPathParser(std::string_view s) : _s(s) {}

    Path Parse()
    {
        char cmd = 0;
        while (true)
        {
            _SkipSeparators();
            if (_pos >= _s.size())
            {
                break;
            }
            const char c = _s[_pos];
            if (std::isalpha(static_cast<unsigned char>(c)) != 0)
            {
                cmd = c;
                ++_pos;
            }
            else if (cmd == 0)
            {
                throw ParseError("SVG path must start with a command");
            }
            _Execute(cmd);
            // after M/m implicit repeats become L/l
            if (cmd == 'M')
            {
                cmd = 'L';
            }
            else if (cmd == 'm')
            {
                cmd = 'l';
            }
        }
        return std::move(_path);
    }

private:
    void _SkipSeparators()
    {
        while (_pos < _s.size() && (std::isspace(static_cast<unsigned char>(_s[_pos])) != 0 || _s[_pos] == ','))
        {
            ++_pos;
        }
    }

    double _Number()
    {
        _SkipSeparators();
        size_t end = _pos;
        if (end < _s.size() && (_s[end] == '-' || _s[end] == '+'))
        {
            ++end;
        }
        while (end < _s.size() && (std::isdigit(static_cast<unsigned char>(_s[end])) != 0 || _s[end] == '.' || _s[end] == 'e' ||
                                   _s[end] == 'E' || ((_s[end] == '-' || _s[end] == '+') && (_s[end - 1] == 'e' || _s[end - 1] == 'E'))))
        {
            ++end;
        }
        std::string tok(_s.substr(_pos, end - _pos));
        if (!tok.empty() && tok.front() == '+')
        {
            tok.erase(0, 1);
        }
        double value         = 0;
        const auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), value);
        if (tok.empty() || ec != std::errc{} || ptr != tok.data() + tok.size())
        {
            throw ParseError("SVG path: expected a number at position " + std::to_string(_pos));
        }
        _pos = end;
        return value;
    }

    Vec2 _Point(bool relative)
    {
        const double x = _Number();
        const double y = _Number();
        return relative ? _cur + Vec2{x, y} : Vec2{x, y};
    }

    void _Execute(char cmd)
    {
        const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
        switch (std::toupper(static_cast<unsigned char>(cmd)))
        {
        case 'M':
            _cur   = _Point(rel);
            _start = _cur;
            _path.MoveTo(_cur);
            break;
        case 'L':
            _cur = _Point(rel);
            _path.LineTo(_cur);
            break;
        case 'H':
        {
            const double x = _Number();
            _cur           = {rel ? _cur.x + x : x, _cur.y};
            _path.LineTo(_cur);
            break;
        }
        case 'V':
        {
            const double y = _Number();
            _cur           = {_cur.x, rel ? _cur.y + y : y};
            _path.LineTo(_cur);
            break;
        }
        case 'C':
        {
            const Vec2 c1 = _Point(rel);
            const Vec2 c2 = _Point(rel);
            const Vec2 to = _Point(rel);
            _path.CubicTo(c1, c2, to);
            _cur = to;
            break;
        }
        case 'Q':
        {
            const Vec2 c  = _Point(rel);
            const Vec2 to = _Point(rel);
            _path.QuadTo(c, to);
            _cur = to;
            break;
        }
        case 'Z':
            _path.Close();
            _cur = _start;
            break;
        default:
            throw ParseError(std::string("SVG path: unsupported command '") + cmd + "'");
        }
    }

    std::string_view _s;
    size_t           _pos = 0;
    Path             _path;
    Vec2             _cur;
    Vec2             _start;
};

} // namespace

Path ParseSvgPath(std::string_view data) { return SvgPathParser(data).Parse(); }

// ---------------------------------------------------------------------------
// FlatPath
// ---------------------------------------------------------------------------

FlatPath::FlatPath(const Path &path, int steps_per_curve)
{
    Vec2 cur;
    auto push = [this](Vec2 p)
    {
        if (_pts.empty())
        {
            _pts.push_back(p);
            _cum.push_back(0);
            return;
        }
        _cum.push_back(_cum.back() + Distance(_pts.back(), p));
        _pts.push_back(p);
    };
    for (const auto &s : path.Segments())
    {
        switch (s.kind)
        {
        case Path::Kind::Move:
            if (!_pts.empty())
            {
                return; // first sub-path only
            }
            push(s.to);
            break;
        case Path::Kind::Line:
        case Path::Kind::Close:
            push(s.to);
            break;
        case Path::Kind::Quad:
            for (int k = 1; k <= steps_per_curve; ++k)
            {
                const double t = static_cast<double>(k) / steps_per_curve;
                const double u = 1 - t;
                push(cur * (u * u) + s.c1 * (2 * u * t) + s.to * (t * t));
            }
            break;
        case Path::Kind::Cubic:
            for (int k = 1; k <= steps_per_curve; ++k)
            {
                const double t = static_cast<double>(k) / steps_per_curve;
                const double u = 1 - t;
                push(cur * (u * u * u) + s.c1 * (3 * u * u * t) + s.c2 * (3 * u * t * t) + s.to * (t * t * t));
            }
            break;
        }
        cur = s.to;
    }
}

Vec2 FlatPath::PointAtLength(double len) const
{
    if (_pts.empty())
    {
        return {};
    }
    if (_pts.size() == 1 || len <= 0)
    {
        return _pts.front();
    }
    if (len >= Length())
    {
        return _pts.back();
    }
    const auto   it  = std::upper_bound(_cum.begin(), _cum.end(), len);
    const auto   i   = static_cast<size_t>(it - _cum.begin()); // _cum[i-1] <= len < _cum[i]
    const double seg = _cum[i] - _cum[i - 1];
    const double t   = seg > 0 ? (len - _cum[i - 1]) / seg : 0.0;
    return ad::Lerp(_pts[i - 1], _pts[i], t);
}

Vec2 FlatPath::DirectionAt(double len, bool forward) const
{
    const double total = Length();
    if (total <= 0)
    {
        return {forward ? 1.0 : -1.0, 0};
    }
    constexpr double kH = 1.0;
    len                 = std::clamp(len, 0.0, total);
    // take [len-h, len+h] clamped to the path so the direction never degenerates at the ends
    const double a = std::clamp(len - kH, 0.0, std::max(0.0, total - 2 * kH));
    const double b = std::min(total, a + 2 * kH);
    Vec2         d = PointAtLength(b) - PointAtLength(a);
    if (!forward)
    {
        d = d * -1.0;
    }
    const double n = ad::Length(d);
    return n > 0 ? d / n : Vec2{forward ? 1.0 : -1.0, 0};
}

Vec2 FlatPath::PointAlong(double pos, double off) const
{
    const double total = Length();
    pos                = std::clamp(pos, 0.0, 1.0);
    const Vec2 at      = PointAtLength(total * pos);
    if (off == 0)
    {
        return at;
    }
    const Vec2   a   = PointAtLength(std::max(0.0, total * pos - 3));
    const Vec2   b   = PointAtLength(std::min(total, total * pos + 3));
    const double ang = std::atan2(b.y - a.y, b.x - a.x);
    double       nx  = std::cos(ang + std::numbers::pi / 2);
    double       ny  = std::sin(ang + std::numbers::pi / 2);
    if (ny > 0) // "up" on screen is the positive offset
    {
        nx = -nx;
        ny = -ny;
    }
    return {at.x + nx * off, at.y + ny * off};
}

double FlatPath::DistanceTo(Vec2 p) const
{
    if (_pts.empty())
    {
        return std::numeric_limits<double>::infinity();
    }
    if (_pts.size() == 1)
    {
        return Distance(p, _pts.front());
    }
    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i + 1 < _pts.size(); ++i)
    {
        best = std::min(best, geom::DistToSegment(p, _pts[i], _pts[i + 1]));
    }
    return best;
}

} // namespace ad
