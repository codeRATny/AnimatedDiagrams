#include "AgentOverlay.hpp"

#include <QPainter>
#include <QString>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ad::ui
{

namespace
{

using namespace std::chrono_literals;

double Progress(auto elapsed, auto duration) { return std::clamp(std::chrono::duration<double>(elapsed) / duration, 0.0, 1.0); }

QColor AgentColor(const std::string &id)
{
    // IDs are sequential, so adjacent agents use well separated hues.
    const auto number = QString::fromStdString(id).toULongLong();
    return QColor::fromHsv(static_cast<int>(((number % 360) * 137 + 25) % 360), 190, 247);
}

void DrawCursor(QPainter &p, QColor color, const QString &label, QRectF badge)
{
    const QPolygonF arrow{{0, 0}, {1, 19}, {6, 14}, {10, 21}, {14, 19}, {10, 12}, {17, 10}};
    p.setPen(QPen(QColor(17, 24, 39), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(color);
    p.drawPolygon(arrow);
    p.drawRoundedRect(badge, 5, 5);
    p.setPen(QColor(17, 24, 39));
    p.drawText(badge, Qt::AlignCenter, label);
}

void DrawParticles(QPainter &p, const QColor &color, double spread)
{
    constexpr int kCount = 20;
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (int i = 0; i < kCount; ++i)
    {
        const double  angle  = i * 2.39996323;
        const double  radius = (15 + (i * 13) % 30) * spread;
        const QPointF point(5 + std::cos(angle) * radius, 8 + std::sin(angle) * radius);
        const double  size = 1 + (i % 3) * 0.6;
        p.drawEllipse(point, size, size);
    }
}

} // namespace

AgentOverlay::AgentOverlay(std::chrono::milliseconds move_duration) : _move_duration(std::max(1ms, move_duration)) {}

std::chrono::milliseconds AgentOverlay::_Duration(Phase phase)
{
    switch (phase)
    {
    case Phase::Assemble:
    case Phase::Disassemble:
        return 250ms;
    case Phase::GlitchIn:
    case Phase::GlitchOut:
        return 165ms;
    case Phase::PortalIn:
    case Phase::PortalOut:
        return 300ms;
    case Phase::Hidden:
    case Phase::Visible:
        return 0ms;
    }
    return 0ms;
}

bool AgentOverlay::_Advance(Cursor &cursor, Clock::time_point now)
{
    const auto idle_at = cursor.last_activity + kIdleDelay;
    if (now >= cursor.last_activity + mcp::DocumentHost::kAgentTimeout)
    {
        return false;
    }
    if (cursor.phase == Phase::PortalOut || cursor.phase == Phase::Disassemble)
    {
        return now < idle_at && (!cursor.phase_start || now < *cursor.phase_start + _Duration(cursor.phase));
    }
    if (now >= idle_at)
    {
        cursor.phase       = now < idle_at + _Duration(Phase::GlitchOut) ? Phase::GlitchOut : Phase::Hidden;
        cursor.phase_start = idle_at;
    }
    else if (cursor.phase_start && now >= *cursor.phase_start + _Duration(cursor.phase))
    {
        cursor.phase = Phase::Visible;
    }
    return true;
}

QPointF AgentOverlay::_Position(const Cursor &cursor, Clock::time_point now) const
{
    const double t         = Progress(now - cursor.move_start, _move_duration);
    const double remaining = 1 - t;
    return cursor.to + (cursor.from - cursor.to) * (remaining * remaining * remaining);
}

void AgentOverlay::Apply(const mcp::DocumentHost::AgentActivity &activity, mcp::DocumentHost::DocumentKey canvas_document, QPointF fallback,
                         Clock::time_point now)
{
    const bool arrived  = activity.document == canvas_document && activity.previous_document != canvas_document;
    const bool departed = activity.previous_document == canvas_document && activity.document != canvas_document;
    if (!departed && activity.document != canvas_document)
    {
        if (activity.document == nullptr)
        {
            _cursors.erase(activity.id);
        }
        return;
    }

    auto it = _cursors.find(activity.id);
    if (it != _cursors.end() && !_Advance(it->second, now))
    {
        _cursors.erase(it);
        it = _cursors.end();
    }
    if (departed)
    {
        const bool manual = activity.document == nullptr;
        if (it == _cursors.end() || it->second.phase == Phase::Hidden || it->second.phase == Phase::GlitchOut)
        {
            _cursors.erase(activity.id);
            if (!manual)
            {
                _cursors.emplace(activity.id,
                                 Cursor{fallback, fallback, AgentColor(activity.id), Phase::PortalOut, {}, now, activity.called_at});
            }
            return;
        }
        auto &cursor = it->second;
        cursor.from = cursor.to = _Position(cursor, now);
        cursor.phase            = manual ? Phase::Disassemble : Phase::PortalOut;
        cursor.phase_start      = manual ? std::optional(now) : std::nullopt;
        cursor.last_activity    = activity.called_at;
        return;
    }

    const QPointF point = activity.position ? QPointF(activity.position->x, activity.position->y) : fallback;
    if (arrived || it == _cursors.end())
    {
        if (arrived || activity.position)
        {
            _cursors.insert_or_assign(
                activity.id,
                Cursor{point, point, AgentColor(activity.id), arrived ? Phase::PortalIn : Phase::Assemble, {}, now, activity.called_at});
        }
        return;
    }
    auto &cursor = it->second;
    if (cursor.phase == Phase::Hidden || cursor.phase == Phase::GlitchOut)
    {
        cursor.phase = Phase::GlitchIn;
        cursor.phase_start.reset();
        if (activity.position)
        {
            cursor.from = cursor.to = point;
        }
    }
    else if (activity.position && cursor.to != point)
    {
        cursor.from       = _Position(cursor, now);
        cursor.to         = point;
        cursor.move_start = now;
    }
    cursor.last_activity = activity.called_at;
}

std::optional<std::chrono::milliseconds> AgentOverlay::Paint(QPainter &painter, const QRect &viewport, const View &view,
                                                             Clock::time_point now)
{
    std::optional<Clock::time_point> wake;
    auto                             schedule = [&wake](Clock::time_point when)
    {
        wake = wake ? std::min(*wake, when) : when;
    };

    for (auto it = _cursors.begin(); it != _cursors.end();)
    {
        if (_Advance(it->second, now))
        {
            ++it;
        }
        else
        {
            it = _cursors.erase(it);
        }
    }
    if (_cursors.empty())
    {
        return {};
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    auto font = painter.font();
    font.setPixelSize(12);
    font.setBold(true);
    painter.setFont(font);
    std::vector<QRectF> badges;
    for (auto &[id, cursor] : _cursors)
    {
        if (cursor.phase == Phase::Hidden)
        {
            schedule(cursor.last_activity + mcp::DocumentHost::kAgentTimeout);
            continue;
        }
        schedule(cursor.phase == Phase::GlitchOut ? *cursor.phase_start + _Duration(cursor.phase) : cursor.last_activity + kIdleDelay);
        if (!cursor.phase_start)
        {
            cursor.phase_start = now;
        }
        const auto   duration = _Duration(cursor.phase);
        const double t        = duration > 0ms ? Progress(now - *cursor.phase_start, duration) : 1;
        if (t < 1 || (cursor.from != cursor.to && now < cursor.move_start + _move_duration))
        {
            schedule(now + 16ms);
        }

        const QString label = tr("Agent %1").arg(QString::fromStdString(id));
        const double  width = painter.fontMetrics().horizontalAdvance(label) + 12;
        const QPointF world = _Position(cursor, now);
        const QPointF screen(std::clamp(world.x() * view.zoom + view.pan_x, 4.0, std::max(4.0, viewport.width() - width - 25)),
                             std::clamp(world.y() * view.zoom + view.pan_y, 4.0, std::max(4.0, viewport.height() - 27.0)));
        QRectF        badge(screen + QPointF(15, 5), QSizeF(width, 20));
        for (const QRectF &other : badges)
        {
            if (badge.intersects(other))
            {
                badge.moveTop(other.bottom() + 2);
            }
        }
        badges.push_back(badge);
        badge.translate(-screen);
        painter.save();
        painter.translate(screen);
        switch (cursor.phase)
        {
        case Phase::Assemble:
        case Phase::Disassemble:
        {
            const double formed = cursor.phase == Phase::Assemble ? t : 1 - t;
            painter.setOpacity((1 - formed) * std::min(1.0, formed * 9 + 0.25));
            DrawParticles(painter, cursor.color, 1 - formed);
            painter.setOpacity(std::clamp((formed - 0.35) / 0.65, 0.0, 1.0));
            DrawCursor(painter, cursor.color, label, badge);
            break;
        }
        case Phase::GlitchIn:
        case Phase::GlitchOut:
        {
            const double formed = cursor.phase == Phase::GlitchIn ? t : 1 - t;
            painter.setOpacity(formed);
            painter.translate(std::sin(t * 76) * (1 - formed) * 8, 0);
            DrawCursor(painter, cursor.color, label, badge);
            painter.setPen(QPen(cursor.color.lighter(140), 2));
            for (int i = 0; i < 4; ++i)
            {
                const double y = 3 + i * 5;
                const double x = std::sin(t * 85 + i * 2) * 13;
                painter.drawLine(QPointF(x, y), QPointF(x + (1 - formed) * 34, y));
            }
            break;
        }
        case Phase::PortalIn:
        case Phase::PortalOut:
        {
            const double open = std::sin(t * std::numbers::pi);
            painter.setOpacity(std::min(1.0, open * 2));
            painter.setPen(QPen(cursor.color, 2));
            painter.setBrush(cursor.color.darker(350));
            painter.drawEllipse(QPointF(-9, 9), 4 + 6 * open, 13 + 6 * open);
            painter.drawEllipse(QPointF(-9, 9), 2 + 3 * open, 9 + 3 * open);
            const double formed = cursor.phase == Phase::PortalIn ? t : 1 - t;
            painter.setOpacity(formed);
            painter.translate(-9 * (1 - formed), 9 * (1 - formed));
            painter.scale(0.35 + 0.65 * formed, 0.35 + 0.65 * formed);
            DrawCursor(painter, cursor.color, label, badge);
            break;
        }
        case Phase::Visible:
            DrawCursor(painter, cursor.color, label, badge);
            break;
        case Phase::Hidden:
            break;
        }
        painter.restore();
    }
    painter.restore();
    if (!wake)
    {
        return {};
    }
    return std::max(1ms, std::chrono::ceil<std::chrono::milliseconds>(*wake - now));
}

} // namespace ad::ui
