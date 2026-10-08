#include "qt_render.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <variant>

namespace app {

namespace {

constexpr double kRefPx = 100;  // эталонный размер шрифта; реальный — масштабом painter'а

struct Fonts {
    QFont regular;
    QFont bold;
    QFontMetricsF regularFm;
    QFontMetricsF boldFm;

    Fonts() : regular(makeFont(false)), bold(makeFont(true)), regularFm(regular), boldFm(bold) {}

    static QFont makeFont(bool isBold) {
        QFont f;
        f.setPixelSize(static_cast<int>(kRefPx));
        f.setBold(isBold);
        f.setHintingPreference(QFont::PreferNoHinting);  // метрики не зависят от масштаба
        f.setStyleStrategy(QFont::PreferAntialias);
        return f;
    }
    const QFont& font(bool b) const { return b ? bold : regular; }
    const QFontMetricsF& fm(bool b) const { return b ? boldFm : regularFm; }
};

const Fonts& fonts() {
    thread_local const Fonts f;  // экспорт рисует в рабочем потоке
    return f;
}

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

QPen makePen(const ad::Stroke& s, double alpha = 1.0, double extraWidth = 0) {
    const double w = s.width + extraWidth;
    QPen pen(toQColor(s.color, alpha), w);
    pen.setCapStyle(s.roundCap ? Qt::RoundCap : Qt::FlatCap);
    pen.setJoinStyle(Qt::RoundJoin);
    if (!s.dash.empty() && s.width > 0) {
        QList<qreal> pattern;
        for (double d : s.dash) pattern.push_back(std::max(0.01, d / s.width));  // Qt — в единицах толщины
        if (pattern.size() % 2) pattern += QList<qreal>(pattern);  // нечётный шаблон — повторить (как в SVG)
        pen.setDashPattern(pattern);
        pen.setDashOffset(s.dashOffset / s.width);
    }
    return pen;
}

void paintPath(QPainter& p, const QPainterPath& path, const ad::Paint& paint) {
    if (paint.effect == ad::Effect::Shadow) {
        // мягкая тень: несколько полупрозрачных проходов со смещением вниз
        const QPainterPath shifted = path.translated(0, 3);
        for (const double w : {7.0, 4.0, 1.5})
            p.strokePath(shifted, QPen(QColor(0, 0, 0, 16), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (paint.fill) p.fillPath(shifted, QColor(0, 0, 0, 40));
    } else if (paint.effect == ad::Effect::Glow) {
        if (paint.stroke) {
            p.strokePath(path, makePen(*paint.stroke, 0.12, 8));
            p.strokePath(path, makePen(*paint.stroke, 0.22, 4));
        } else if (paint.fill) {
            for (const double w : {8.0, 4.0})
                p.strokePath(path, QPen(toQColor(*paint.fill, 0.18), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
    }
    if (paint.fill) p.fillPath(path, toQColor(*paint.fill));
    if (paint.stroke) p.strokePath(path, makePen(*paint.stroke));
}

void drawText(QPainter& p, const ad::TextShape& t, const ad::Paint& paint) {
    if (t.text.empty() || t.font.size <= 0) return;
    const Fonts& f = fonts();
    const QFont& font = f.font(t.font.bold);
    const QFontMetricsF& fm = f.fm(t.font.bold);
    const double k = t.font.size / kRefPx;
    const QString s = qs(t.text);
    const double w = fm.horizontalAdvance(s) * k;
    double x = t.pos.x;
    if (t.align == ad::HAlign::Center) x -= w / 2;
    if (t.align == ad::HAlign::Right) x -= w;
    double y = t.pos.y;
    if (t.valign == ad::VAlign::Middle) y += fm.xHeight() * k / 2;  // как dominant-baseline: middle

    p.save();
    p.translate(x, y);
    p.scale(k, k);
    if (t.halo) {
        QPainterPath tp;
        tp.addText(0, 0, font, s);
        p.strokePath(tp, QPen(toQColor(t.halo->color), t.halo->width / k, Qt::SolidLine, Qt::RoundCap,
                              Qt::RoundJoin));
    }
    p.setFont(font);
    p.setPen(toQColor(paint.fill.value_or(ad::Color::rgb(0xffffff))));
    p.drawText(QPointF(0, 0), s);
    p.restore();
}

}  // namespace

QColor toQColor(ad::Color c, double alpha) { return QColor::fromRgbF(c.r / 255.f, c.g / 255.f, c.b / 255.f, static_cast<float>(alpha)); }

ad::Color fromQColor(const QColor& c) {
    return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()), static_cast<std::uint8_t>(c.blue())};
}

QPainterPath toQPath(const ad::Path& path) {
    QPainterPath qp;
    for (const auto& s : path.segments()) {
        switch (s.kind) {
            case ad::Path::Kind::Move: qp.moveTo(s.to.x, s.to.y); break;
            case ad::Path::Kind::Line: qp.lineTo(s.to.x, s.to.y); break;
            case ad::Path::Kind::Quad: qp.quadTo(s.c1.x, s.c1.y, s.to.x, s.to.y); break;
            case ad::Path::Kind::Cubic: qp.cubicTo(s.c1.x, s.c1.y, s.c2.x, s.c2.y, s.to.x, s.to.y); break;
        }
    }
    return qp;
}

double QtTextMeasurer::width(std::string_view utf8, const ad::Font& font) const {
    return fonts().fm(font.bold).horizontalAdvance(qs(utf8)) * font.size / kRefPx;
}

void renderFrame(QPainter& p, const ad::Frame& frame) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    for (const ad::Item& it : frame.items) {
        p.save();
        if (it.transform) {
            const auto& t = *it.transform;
            p.translate(t.origin.x + t.translate.x, t.origin.y + t.translate.y);
            p.rotate(t.rotateDeg);
            p.scale(t.scale, t.scale);
            p.translate(-t.origin.x, -t.origin.y);
        }
        if (it.clip) {
            QPainterPath cp;
            const auto& r = it.clip->rect;
            const double rad = std::min({it.clip->radius, r.w / 2, r.h / 2});
            cp.addRoundedRect(QRectF(r.x, r.y, r.w, r.h), rad, rad);
            p.setClipPath(cp, Qt::IntersectClip);
        }
        p.setOpacity(p.opacity() * std::clamp(it.paint.opacity, 0.0, 1.0));

        std::visit(Overloaded{
                       [&](const ad::RectShape& s) {
                           QPainterPath path;
                           const double rad = std::min({s.radius, s.rect.w / 2, s.rect.h / 2});
                           path.addRoundedRect(QRectF(s.rect.x, s.rect.y, s.rect.w, s.rect.h), rad, rad);
                           paintPath(p, path, it.paint);
                       },
                       [&](const ad::EllipseShape& s) {
                           QPainterPath path;
                           path.addEllipse(QPointF(s.center.x, s.center.y), s.rx, s.ry);
                           paintPath(p, path, it.paint);
                       },
                       [&](const ad::PathShape& s) { paintPath(p, toQPath(s.path), it.paint); },
                       [&](const ad::ArcShape& s) {
                           QPainterPath path;
                           const QRectF r(s.center.x - s.radius, s.center.y - s.radius, 2 * s.radius, 2 * s.radius);
                           // Qt: углы против часовой стрелки — меняем знак
                           path.arcMoveTo(r, -s.startDeg);
                           path.arcTo(r, -s.startDeg, -s.sweepDeg);
                           ad::Paint strokeOnly = it.paint;
                           strokeOnly.fill.reset();
                           paintPath(p, path, strokeOnly);
                       },
                       [&](const ad::TextShape& s) { drawText(p, s, it.paint); },
                   },
                   it.shape);
        p.restore();
    }
}

}  // namespace app
