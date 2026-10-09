#include "QtRender.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <map>
#include <memory>
#include <variant>

namespace ad::ui
{

namespace
{

constexpr double kRefPx = 100; // reference font size; the real size is a painter scale

struct FontEntry
{
    QFont         font;
    QFontMetricsF metrics;

    FontEntry(const QString &family, bool bold) : font(Make(family, bold)), metrics(font) {}

    static QFont Make(const QString &family, bool bold)
    {
        QFont f;
        if (!family.isEmpty())
        {
            f.setFamily(family);
        }
        f.setPixelSize(static_cast<int>(kRefPx));
        f.setBold(bold);
        f.setHintingPreference(QFont::PreferNoHinting); // metrics independent of the scale
        f.setStyleStrategy(QFont::PreferAntialias);
        return f;
    }
};

/// Per-thread cache (export renders in worker threads), keyed by family and weight.
const FontEntry &GetFont(const Font &font)
{
    thread_local std::map<std::pair<std::string, bool>, std::unique_ptr<FontEntry>> cache;
    auto                                                                           &slot = cache[{font.family, font.bold}];
    if (!slot)
    {
        slot = std::make_unique<FontEntry>(Qs(font.family), font.bold);
    }
    return *slot;
}

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

QPen MakePen(const Stroke &s, double alpha = 1.0, double extra_width = 0)
{
    QPen pen(ToQColor(s.color, alpha), s.width + extra_width);
    pen.setCapStyle(s.round_cap ? Qt::RoundCap : Qt::FlatCap);
    pen.setJoinStyle(Qt::RoundJoin);
    if (!s.dash.empty() && s.width > 0)
    {
        QList<qreal> pattern;
        for (const double d : s.dash)
        {
            pattern.push_back(std::max(0.01, d / s.width)); // Qt dashes are in pen widths
        }
        if (pattern.size() % 2 != 0)
        {
            pattern += QList<qreal>(pattern); // odd pattern repeats (as in SVG)
        }
        pen.setDashPattern(pattern);
        pen.setDashOffset(s.dash_offset / s.width);
    }
    return pen;
}

void PaintPath(QPainter &p, const QPainterPath &path, const Paint &paint)
{
    if (paint.effect == PaintEffect::Shadow)
    {
        // soft shadow: a few translucent passes shifted down
        const QPainterPath shifted = path.translated(0, 3);
        for (const double w : {7.0, 4.0, 1.5})
        {
            p.strokePath(shifted, QPen(QColor(0, 0, 0, 16), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        if (paint.fill.has_value())
        {
            p.fillPath(shifted, QColor(0, 0, 0, 40));
        }
    }
    else if (paint.effect == PaintEffect::Glow)
    {
        if (paint.stroke.has_value())
        {
            p.strokePath(path, MakePen(*paint.stroke, 0.12, 8));
            p.strokePath(path, MakePen(*paint.stroke, 0.22, 4));
        }
        else if (paint.fill.has_value())
        {
            for (const double w : {8.0, 4.0})
            {
                p.strokePath(path, QPen(ToQColor(*paint.fill, 0.18), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            }
        }
    }
    if (paint.fill.has_value())
    {
        p.fillPath(path, ToQColor(*paint.fill));
    }
    if (paint.stroke.has_value())
    {
        p.strokePath(path, MakePen(*paint.stroke));
    }
}

void DrawText(QPainter &p, const TextShape &t, const Paint &paint)
{
    if (t.text.empty() || t.font.size <= 0)
    {
        return;
    }
    const FontEntry     &f    = GetFont(t.font);
    const QFont         &font = f.font;
    const QFontMetricsF &fm   = f.metrics;
    const double         k    = t.font.size / kRefPx;
    const QString        s    = Qs(t.text);
    const double         w    = fm.horizontalAdvance(s) * k;
    double               x    = t.pos.x;
    if (t.align == HAlign::Center)
    {
        x -= w / 2;
    }
    if (t.align == HAlign::Right)
    {
        x -= w;
    }
    double y = t.pos.y;
    if (t.valign == VAlign::Middle)
    {
        y += fm.xHeight() * k / 2; // as dominant-baseline: middle
    }
    p.save();
    p.translate(x, y);
    p.scale(k, k);
    if (t.halo.has_value())
    {
        QPainterPath tp;
        tp.addText(0, 0, font, s);
        p.strokePath(tp, QPen(ToQColor(t.halo->color), t.halo->width / k, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    p.setFont(font);
    p.setPen(ToQColor(paint.fill.value_or(Color::Rgb(0xffffff))));
    p.drawText(QPointF(0, 0), s);
    p.restore();
}

} // namespace

QColor ToQColor(Color c, double alpha)
{
    return QColor::fromRgbF(c.r / 255.F, c.g / 255.F, c.b / 255.F, static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
}

Color FromQColor(const QColor &c)
{
    return {static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue())};
}

QPainterPath ToQPath(const Path &path)
{
    QPainterPath qp;
    for (const auto &s : path.Segments())
    {
        switch (s.kind)
        {
        case Path::Kind::Move:
            qp.moveTo(s.to.x, s.to.y);
            break;
        case Path::Kind::Line:
            qp.lineTo(s.to.x, s.to.y);
            break;
        case Path::Kind::Quad:
            qp.quadTo(s.c1.x, s.c1.y, s.to.x, s.to.y);
            break;
        case Path::Kind::Cubic:
            qp.cubicTo(s.c1.x, s.c1.y, s.c2.x, s.c2.y, s.to.x, s.to.y);
            break;
        case Path::Kind::Close:
            qp.closeSubpath();
            break;
        }
    }
    return qp;
}

double QtTextMeasurer::Width(std::string_view utf8, const Font &font) const
{
    return GetFont(font).metrics.horizontalAdvance(Qs(utf8)) * font.size / kRefPx;
}

void RenderFrame(QPainter &p, const Frame &frame)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    for (const Item &it : frame.items)
    {
        p.save();
        if (it.transform.has_value())
        {
            const auto &t = *it.transform;
            p.translate(t.origin.x + t.translate.x, t.origin.y + t.translate.y);
            p.rotate(t.rotate_deg);
            p.scale(t.scale, t.scale);
            p.translate(-t.origin.x, -t.origin.y);
        }
        if (it.clip.has_value())
        {
            p.setClipPath(ToQPath(*it.clip), Qt::IntersectClip);
        }
        p.setOpacity(p.opacity() * std::clamp(it.paint.opacity, 0.0, 1.0));

        std::visit(
            Overloaded{
                [&](const RectShape &s)
                {
                    QPainterPath path;
                    const double rad = std::min({s.radius, s.rect.w / 2, s.rect.h / 2});
                    path.addRoundedRect(QRectF(s.rect.x, s.rect.y, s.rect.w, s.rect.h), rad, rad);
                    PaintPath(p, path, it.paint);
                },
                [&](const EllipseShape &s)
                {
                    QPainterPath path;
                    path.addEllipse(QPointF(s.center.x, s.center.y), s.rx, s.ry);
                    PaintPath(p, path, it.paint);
                },
                [&](const PathShape &s)
                {
                    PaintPath(p, ToQPath(s.path), it.paint);
                },
                [&](const ArcShape &s)
                {
                    QPainterPath path;
                    const QRectF r(s.center.x - s.radius, s.center.y - s.radius, 2 * s.radius, 2 * s.radius);
                    // Qt angles are counter-clockwise -- flip the sign
                    path.arcMoveTo(r, -s.start_deg);
                    path.arcTo(r, -s.start_deg, -s.sweep_deg);
                    Paint stroke_only = it.paint;
                    stroke_only.fill.reset();
                    PaintPath(p, path, stroke_only);
                },
                [&](const TextShape &s)
                {
                    DrawText(p, s, it.paint);
                },
            },
            it.shape);
        p.restore();
    }
}

} // namespace ad::ui
