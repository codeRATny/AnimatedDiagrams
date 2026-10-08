#pragma once
// Геометрия связей: векторы, пути (line/quad/cubic), Catmull-Rom и
// параметризация пути по длине дуги (аналог SVGPathElement.getPointAtLength).

#include <cmath>
#include <span>
#include <vector>

namespace ad {

struct Vec2 {
    double x = 0;
    double y = 0;

    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator*(Vec2 a, double k) { return {a.x * k, a.y * k}; }
    friend constexpr Vec2 operator/(Vec2 a, double k) { return {a.x / k, a.y / k}; }
    friend constexpr bool operator==(Vec2, Vec2) = default;
};

inline double length(Vec2 v) { return std::hypot(v.x, v.y); }
inline double distance(Vec2 a, Vec2 b) { return length(b - a); }
inline Vec2 lerp(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }

struct Rect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;

    [[nodiscard]] Vec2 center() const { return {x + w / 2, y + h / 2}; }
    [[nodiscard]] double right() const { return x + w; }
    [[nodiscard]] double bottom() const { return y + h; }
    [[nodiscard]] bool contains(Vec2 p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    [[nodiscard]] Rect adjusted(double d) const { return {x - d, y - d, w + 2 * d, h + 2 * d}; }
    [[nodiscard]] Rect united(const Rect& o) const;
    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

/// Накопитель габаритов: пустой, пока не добавлена хоть одна точка.
class Bounds {
public:
    void add(Vec2 p);
    void add(const Rect& r);
    [[nodiscard]] bool empty() const { return empty_; }
    [[nodiscard]] Rect rect() const;

private:
    bool empty_ = true;
    double minX_ = 0, minY_ = 0, maxX_ = 0, maxY_ = 0;
};

namespace geom {

/// Пересечение луча из центра прямоугольника (полуразмеры hw, hh) в сторону `toward` с его границей.
Vec2 borderPoint(Vec2 center, double hw, double hh, Vec2 toward);

/// Контрольная точка кривой между центрами: середина + смещение по перпендикуляру
/// (curve — доля длины, знак — сторона).
Vec2 perpControl(Vec2 ca, Vec2 cb, double curve);

/// Сдвинуть `tip` к `from` на `dist` (конец линии стыкуется с основанием стрелки).
Vec2 pullBack(Vec2 from, Vec2 tip, double dist);

double distToSegment(Vec2 p, Vec2 a, Vec2 b);

}  // namespace geom

/// Путь из сегментов, всегда начинается с moveTo.
class Path {
public:
    enum class Kind { Move, Line, Quad, Cubic };
    struct Segment {
        Kind kind = Kind::Move;
        Vec2 c1;
        Vec2 c2;
        Vec2 to;
    };

    void moveTo(Vec2 p);
    void lineTo(Vec2 p);
    void quadTo(Vec2 c, Vec2 to);
    void cubicTo(Vec2 c1, Vec2 c2, Vec2 to);

    [[nodiscard]] const std::vector<Segment>& segments() const { return segs_; }
    [[nodiscard]] bool empty() const { return segs_.size() < 2; }
    [[nodiscard]] Vec2 startPoint() const;
    [[nodiscard]] Vec2 endPoint() const;
    /// Направление касательной в начале (по ходу пути) и в конце.
    [[nodiscard]] Vec2 startDirection() const;
    [[nodiscard]] Vec2 endDirection() const;

    static Path line(Vec2 a, Vec2 b);
    static Path quad(Vec2 a, Vec2 c, Vec2 b);
    /// Интерполирующая кривая Catmull-Rom (как d3.curveCatmullRom.alpha(alpha)).
    static Path catmullRom(std::span<const Vec2> pts, double alpha = 0.5);

private:
    std::vector<Segment> segs_;
};

/// Путь, разбитый на ломаную с накопленной длиной дуги.
class FlatPath {
public:
    explicit FlatPath(const Path& path, int stepsPerCurve = 32);

    [[nodiscard]] double length() const { return cum_.empty() ? 0.0 : cum_.back(); }
    [[nodiscard]] Vec2 pointAtLength(double len) const;
    [[nodiscard]] Vec2 pointAtFraction(double f) const { return pointAtLength(f * length()); }
    /// Единичный вектор направления в точке `len` (по ходу или против хода пути).
    [[nodiscard]] Vec2 directionAt(double len, bool forward = true) const;
    /// Точка на доле pos∈[0,1] со смещением off по нормали («вверх» на экране = положительный off).
    [[nodiscard]] Vec2 pointAlong(double pos, double off) const;
    [[nodiscard]] double distanceTo(Vec2 p) const;
    [[nodiscard]] const std::vector<Vec2>& points() const { return pts_; }

private:
    std::vector<Vec2> pts_;
    std::vector<double> cum_;
};

}  // namespace ad
