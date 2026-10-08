#include "ad/geometry.hpp"

#include <algorithm>
#include <limits>
#include <numbers>

namespace ad {

Rect Rect::united(const Rect& o) const {
    Bounds b;
    b.add(*this);
    b.add(o);
    return b.rect();
}

void Bounds::add(Vec2 p) {
    if (empty_) {
        minX_ = maxX_ = p.x;
        minY_ = maxY_ = p.y;
        empty_ = false;
        return;
    }
    minX_ = std::min(minX_, p.x);
    minY_ = std::min(minY_, p.y);
    maxX_ = std::max(maxX_, p.x);
    maxY_ = std::max(maxY_, p.y);
}

void Bounds::add(const Rect& r) {
    add(Vec2{r.x, r.y});
    add(Vec2{r.right(), r.bottom()});
}

Rect Bounds::rect() const {
    if (empty_) return {};
    return {minX_, minY_, maxX_ - minX_, maxY_ - minY_};
}

namespace geom {

Vec2 borderPoint(Vec2 center, double hw, double hh, Vec2 toward) {
    const double dx = toward.x - center.x;
    const double dy = toward.y - center.y;
    if (dx == 0 && dy == 0) return center;
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double sx = dx != 0 ? hw / std::abs(dx) : inf;
    const double sy = dy != 0 ? hh / std::abs(dy) : inf;
    const double s = std::min(sx, sy);
    return {center.x + dx * s, center.y + dy * s};
}

Vec2 perpControl(Vec2 ca, Vec2 cb, double curve) {
    const Vec2 mid = (ca + cb) / 2;
    if (curve == 0) return mid;
    const Vec2 d = cb - ca;
    const double len = length(d) > 0 ? length(d) : 1.0;
    return {mid.x + (-d.y / len) * curve * len, mid.y + (d.x / len) * curve * len};
}

Vec2 pullBack(Vec2 from, Vec2 tip, double dist) {
    const Vec2 d = tip - from;
    const double len = length(d) > 0 ? length(d) : 1.0;
    const double k = std::min(dist, len - 0.5);
    return tip - d * (k / len);
}

double distToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const double l2 = d.x * d.x + d.y * d.y;
    double t = l2 > 0 ? ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / l2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return distance(p, a + d * t);
}

}  // namespace geom

// ---- Path -------------------------------------------------------------------

void Path::moveTo(Vec2 p) { segs_.push_back({Kind::Move, {}, {}, p}); }

void Path::lineTo(Vec2 p) {
    if (segs_.empty()) return moveTo(p);
    segs_.push_back({Kind::Line, {}, {}, p});
}

void Path::quadTo(Vec2 c, Vec2 to) {
    if (segs_.empty()) moveTo(c);
    segs_.push_back({Kind::Quad, c, {}, to});
}

void Path::cubicTo(Vec2 c1, Vec2 c2, Vec2 to) {
    if (segs_.empty()) moveTo(c1);
    segs_.push_back({Kind::Cubic, c1, c2, to});
}

Vec2 Path::startPoint() const { return segs_.empty() ? Vec2{} : segs_.front().to; }
Vec2 Path::endPoint() const { return segs_.empty() ? Vec2{} : segs_.back().to; }

namespace {
bool nonZero(Vec2 v) { return v.x != 0 || v.y != 0; }
}  // namespace

Vec2 Path::startDirection() const {
    if (segs_.size() < 2) return {1, 0};
    const Vec2 s = segs_[0].to;
    const Segment& g = segs_[1];
    Vec2 cands[3] = {g.to - s, g.to - s, g.to - s};
    if (g.kind == Kind::Quad) cands[0] = g.c1 - s;
    if (g.kind == Kind::Cubic) {
        cands[0] = g.c1 - s;
        cands[1] = g.c2 - s;
    }
    for (Vec2 c : cands)
        if (nonZero(c)) return c / length(c);
    return {1, 0};
}

Vec2 Path::endDirection() const {
    if (segs_.size() < 2) return {1, 0};
    const Segment& g = segs_.back();
    const Vec2 prev = segs_[segs_.size() - 2].to;
    Vec2 cands[3] = {g.to - prev, g.to - prev, g.to - prev};
    if (g.kind == Kind::Quad) cands[0] = g.to - g.c1;
    if (g.kind == Kind::Cubic) {
        cands[0] = g.to - g.c2;
        cands[1] = g.to - g.c1;
    }
    for (Vec2 c : cands)
        if (nonZero(c)) return c / length(c);
    return {1, 0};
}

Path Path::line(Vec2 a, Vec2 b) {
    Path p;
    p.moveTo(a);
    p.lineTo(b);
    return p;
}

Path Path::quad(Vec2 a, Vec2 c, Vec2 b) {
    Path p;
    p.moveTo(a);
    p.quadTo(c, b);
    return p;
}

Path Path::catmullRom(std::span<const Vec2> pts, double alpha) {
    constexpr double eps = 1e-12;
    Path p;
    const std::size_t n = pts.size();
    if (n == 0) return p;
    p.moveTo(pts[0]);
    if (n == 1) return p;
    if (n == 2) {
        p.lineTo(pts[1]);
        return p;
    }
    // l_2a = |ab|^(2α), l_a = |ab|^α — как в d3-shape (catmullRom.js)
    auto l2a = [alpha](Vec2 a, Vec2 b) {
        const Vec2 d = b - a;
        return std::pow(d.x * d.x + d.y * d.y, alpha);
    };
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const Vec2 p1 = pts[i];
        const Vec2 p2 = pts[i + 1];
        const double l12_2a = l2a(p1, p2);
        const double l12_a = std::sqrt(l12_2a);
        Vec2 c1 = p1;
        Vec2 c2 = p2;
        if (i > 0) {
            const Vec2 p0 = pts[i - 1];
            const double l01_2a = l2a(p0, p1);
            const double l01_a = std::sqrt(l01_2a);
            if (l01_a > eps) {
                const double a = 2 * l01_2a + 3 * l01_a * l12_a + l12_2a;
                const double nn = 3 * l01_a * (l01_a + l12_a);
                c1 = (p1 * a - p0 * l12_2a + p2 * l01_2a) / nn;
            }
        }
        if (i + 2 < n) {
            const Vec2 p3 = pts[i + 2];
            const double l23_2a = l2a(p2, p3);
            const double l23_a = std::sqrt(l23_2a);
            if (l23_a > eps) {
                const double b = 2 * l23_2a + 3 * l23_a * l12_a + l12_2a;
                const double m = 3 * l23_a * (l23_a + l12_a);
                c2 = (p2 * b + p1 * l23_2a - p3 * l12_2a) / m;
            }
        }
        p.cubicTo(c1, c2, p2);
    }
    return p;
}

// ---- FlatPath ---------------------------------------------------------------

FlatPath::FlatPath(const Path& path, int stepsPerCurve) {
    Vec2 cur;
    auto push = [this](Vec2 p) {
        if (pts_.empty()) {
            pts_.push_back(p);
            cum_.push_back(0);
            return;
        }
        cum_.push_back(cum_.back() + distance(pts_.back(), p));
        pts_.push_back(p);
    };
    for (const auto& s : path.segments()) {
        switch (s.kind) {
            case Path::Kind::Move:
                if (pts_.empty()) push(s.to);
                break;
            case Path::Kind::Line:
                push(s.to);
                break;
            case Path::Kind::Quad:
                for (int k = 1; k <= stepsPerCurve; ++k) {
                    const double t = static_cast<double>(k) / stepsPerCurve;
                    const double u = 1 - t;
                    push(cur * (u * u) + s.c1 * (2 * u * t) + s.to * (t * t));
                }
                break;
            case Path::Kind::Cubic:
                for (int k = 1; k <= stepsPerCurve; ++k) {
                    const double t = static_cast<double>(k) / stepsPerCurve;
                    const double u = 1 - t;
                    push(cur * (u * u * u) + s.c1 * (3 * u * u * t) + s.c2 * (3 * u * t * t) + s.to * (t * t * t));
                }
                break;
        }
        cur = s.to;
    }
}

Vec2 FlatPath::pointAtLength(double len) const {
    if (pts_.empty()) return {};
    if (pts_.size() == 1 || len <= 0) return pts_.front();
    if (len >= length()) return pts_.back();
    const auto it = std::upper_bound(cum_.begin(), cum_.end(), len);
    const auto i = static_cast<std::size_t>(it - cum_.begin());  // cum_[i-1] <= len < cum_[i]
    const double seg = cum_[i] - cum_[i - 1];
    const double t = seg > 0 ? (len - cum_[i - 1]) / seg : 0.0;
    return lerp(pts_[i - 1], pts_[i], t);
}

Vec2 FlatPath::directionAt(double len, bool forward) const {
    const double L = length();
    if (L <= 0) return {forward ? 1.0 : -1.0, 0};
    constexpr double h = 1.0;
    len = std::clamp(len, 0.0, L);
    // берём отрезок [len-h, len+h], прижатый к границам пути — так на концах
    // направление не вырождается в ноль
    const double a = std::clamp(len - h, 0.0, std::max(0.0, L - 2 * h));
    const double b = std::min(L, a + 2 * h);
    Vec2 d = pointAtLength(b) - pointAtLength(a);
    if (!forward) d = d * -1.0;
    const double n = ad::length(d);
    return n > 0 ? d / n : Vec2{forward ? 1.0 : -1.0, 0};
}

Vec2 FlatPath::pointAlong(double pos, double off) const {
    const double L = length();
    pos = std::clamp(pos, 0.0, 1.0);
    const Vec2 at = pointAtLength(L * pos);
    if (off == 0) return at;
    const Vec2 a = pointAtLength(std::max(0.0, L * pos - 3));
    const Vec2 b = pointAtLength(std::min(L, L * pos + 3));
    const double ang = std::atan2(b.y - a.y, b.x - a.x);
    double nx = std::cos(ang + std::numbers::pi / 2);
    double ny = std::sin(ang + std::numbers::pi / 2);
    if (ny > 0) {  // «вверх» на экране = положительный off
        nx = -nx;
        ny = -ny;
    }
    return {at.x + nx * off, at.y + ny * off};
}

double FlatPath::distanceTo(Vec2 p) const {
    if (pts_.empty()) return std::numeric_limits<double>::infinity();
    if (pts_.size() == 1) return distance(p, pts_.front());
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i + 1 < pts_.size(); ++i) best = std::min(best, geom::distToSegment(p, pts_[i], pts_[i + 1]));
    return best;
}

}  // namespace ad
