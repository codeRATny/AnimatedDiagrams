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

std::chrono::milliseconds AgentOverlay::_Duration(Phase phase) const
{
    switch (phase)
    {
    case Phase::Move:
        return _move_duration;
    case Phase::Grab:
    case Phase::Select:
        return 100ms;
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

bool AgentOverlay::_Advance(Cursor &cursor, Clock::time_point now) const
{
    const auto idle_at = cursor.last_activity + kIdleDelay;
    if (!cursor.unregistered && now >= idle_at)
    {
        if (cursor.leaving || now >= cursor.last_activity + mcp::DocumentHost::kAgentTimeout)
        {
            return false;
        }
        cursor.pending.clear();
        if (cursor.phase == Phase::Move)
        {
            cursor.frozen = _Progress(cursor, idle_at);
        }
        cursor.carrying    = false;
        cursor.phase       = now < idle_at + _Duration(Phase::GlitchOut) ? Phase::GlitchOut : Phase::Hidden;
        cursor.phase_start = idle_at;
        return true;
    }

    auto next_start = now;
    if (cursor.phase != Phase::Visible && cursor.phase != Phase::Hidden)
    {
        // A newly created cursor or portal starts when its canvas is painted.
        if (!cursor.phase_start)
        {
            return true;
        }
        next_start = *cursor.phase_start + _Duration(cursor.phase);
    }
    while (next_start <= now)
    {
        if (cursor.pending.empty())
        {
            if (cursor.phase == Phase::PortalOut || cursor.phase == Phase::Disassemble)
            {
                return false;
            }
            cursor.phase = Phase::Visible;
            cursor.phase_start.reset();
            return true;
        }

        const auto action = cursor.pending.front();
        cursor.pending.pop_front();
        cursor.from     = cursor.to;
        cursor.phase    = action.phase;
        cursor.carrying = action.carrying;
        cursor.frozen.reset();
        cursor.phase_start = next_start;
        if (action.point)
        {
            cursor.to = *action.point;
        }
        next_start += _Duration(cursor.phase);
    }
    return true;
}

double AgentOverlay::_Progress(const Cursor &cursor, Clock::time_point now) const
{
    if (cursor.phase != Phase::Move)
    {
        return cursor.frozen.value_or(1);
    }
    const double remaining = 1 - Progress(now - *cursor.phase_start, _move_duration);
    return 1 - remaining * remaining * remaining;
}

void AgentOverlay::Apply(const mcp::DocumentHost::AgentActivity &activity, mcp::DocumentHost::DocumentKey canvas_document, QPointF fallback,
                         Clock::time_point now)
{
    const bool arrived  = activity.document == canvas_document && activity.previous_document != canvas_document;
    const bool departed = activity.previous_document == canvas_document && activity.document != canvas_document;
    if (!departed && activity.document != canvas_document)
    {
        if (activity.document == nullptr && activity.previous_document == nullptr)
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
    const bool   manual = departed && activity.document == nullptr;
    const Target point{activity.position ? QPointF(activity.position->x, activity.position->y) : fallback};
    using Route        = mcp::DocumentHost::AgentRoute;
    const auto   route = activity.position && !departed ? activity.route : Route::Canvas;
    const Target first = route == Route::CreateNode ? Target{point.world, Anchor::Palette, activity.element_type} : point;

    const bool first_appearance = it == _cursors.end();
    if (first_appearance)
    {
        if (manual || (!departed && !arrived && !activity.position))
        {
            return;
        }
        Cursor cursor{.from          = first,
                      .to            = first,
                      .color         = AgentColor(activity.id),
                      .phase         = departed ? Phase::PortalOut : (arrived ? Phase::PortalIn : Phase::Assemble),
                      .phase_start   = {},
                      .last_activity = activity.called_at,
                      .pending       = {},
                      .leaving       = departed};
        it = _cursors.emplace(activity.id, std::move(cursor)).first;
    }

    auto      &cursor = it->second;
    const bool hidden = cursor.phase == Phase::Hidden || cursor.phase == Phase::GlitchOut;
    if (manual && hidden)
    {
        _cursors.erase(it);
        return;
    }
    cursor.last_activity = activity.called_at;
    cursor.leaving       = departed;
    cursor.unregistered  = manual;
    if (hidden)
    {
        cursor.phase = departed ? Phase::PortalOut : (arrived ? Phase::PortalIn : Phase::GlitchIn);
        cursor.phase_start.reset();
        if (arrived || activity.position)
        {
            cursor.from = cursor.to = first;
            cursor.frozen.reset();
        }
    }
    else if (!first_appearance && departed)
    {
        cursor.pending.push_back({manual ? Phase::Disassemble : Phase::PortalOut, {}});
    }
    else if (!first_appearance && arrived)
    {
        cursor.pending.push_back({Phase::PortalIn, first});
    }

    if (!departed && activity.position)
    {
        if (!first_appearance && !hidden && !arrived)
        {
            cursor.pending.push_back({Phase::Move, first});
        }
        if (route == Route::CreateNode)
        {
            cursor.pending.push_back({Phase::Grab, {}});
            cursor.pending.push_back({Phase::Move, point, true});
            cursor.pending.push_back({Phase::Select, {}, false, activity.created_node});
        }
        else if (route == Route::AddLinkStep)
        {
            cursor.pending.push_back({Phase::Select, {}});
            cursor.pending.push_back({Phase::Move, Target{point.world, Anchor::StepType}});
            cursor.pending.push_back({Phase::Select, {}});
            cursor.pending.push_back({Phase::Move, Target{point.world, Anchor::AddStep}});
            cursor.pending.push_back({Phase::Select, {}});
        }
    }
    _Advance(cursor, now);
}

std::set<std::string> AgentOverlay::PendingNodes() const
{
    std::set<std::string> nodes;
    for (const auto &[id, cursor] : _cursors)
    {
        for (const auto &action : cursor.pending)
        {
            if (!action.placement_node.empty())
            {
                nodes.insert(action.placement_node);
            }
        }
    }
    return nodes;
}

std::optional<std::chrono::milliseconds> AgentOverlay::Paint(QPainter &painter, const QRect &viewport, const View &view,
                                                             Clock::time_point now)
{
    return PaintProjected(
        painter, viewport,
        [&view](const Target &target)
        {
            return QPointF(target.world.x() * view.zoom + view.pan_x, target.world.y() * view.zoom + view.pan_y);
        },
        now);
}

std::optional<std::chrono::milliseconds> AgentOverlay::PaintProjected(QPainter &painter, const QRect &viewport, const Project &project,
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
    struct Projection
    {
        QPointF from;
        QPointF to;
        Anchor  from_anchor;
        Anchor  to_anchor;
    };
    std::map<std::string_view, Projection> projected;
    for (const auto &[id, cursor] : _cursors)
    {
        if (cursor.phase != Phase::Hidden)
        {
            projected.emplace(id, Projection{project(cursor.from), project(cursor.to), cursor.from.anchor, cursor.to.anchor});
        }
    }

    // Keep pointers and click rings distinguishable when agents use the same UI control.
    auto separate = [&](const auto &id, Anchor anchor, QPointF point)
    {
        if (anchor == Anchor::Canvas)
        {
            return point;
        }
        int count = 0;
        int slot  = 0;
        for (const auto &[other_id, pos] : projected)
        {
            if ((pos.from_anchor == anchor && pos.from == point) || (pos.to_anchor == anchor && pos.to == point))
            {
                slot += other_id < id;
                ++count;
            }
        }
        if (count > 1)
        {
            const int columns = std::min(count, 4);
            point += QPointF((slot % columns - (columns - 1) / 2.0) * 21, (slot / columns) * 19);
        }
        return point;
    };

    std::vector<QRectF> badges;
    for (auto &[id, cursor] : _cursors)
    {
        if (cursor.phase == Phase::Hidden)
        {
            schedule(cursor.last_activity + mcp::DocumentHost::kAgentTimeout);
            continue;
        }
        if (!cursor.unregistered)
        {
            schedule(cursor.phase == Phase::GlitchOut ? *cursor.phase_start + _Duration(cursor.phase) : cursor.last_activity + kIdleDelay);
        }
        if (cursor.phase != Phase::Visible && !cursor.phase_start)
        {
            cursor.phase_start = now;
        }
        const auto   duration = _Duration(cursor.phase);
        const double t        = duration > 0ms ? Progress(now - *cursor.phase_start, duration) : 1;
        if (t < 1)
        {
            schedule(now + 16ms);
        }

        const QString label    = tr("Agent %1").arg(QString::fromStdString(id));
        const double  width    = painter.fontMetrics().horizontalAdvance(label) + 12;
        const auto   &raw      = projected.at(id);
        const auto    from     = separate(id, cursor.from.anchor, raw.from);
        const auto    to       = separate(id, cursor.to.anchor, raw.to);
        const QPointF position = from + (to - from) * _Progress(cursor, now);
        const QPointF screen(std::clamp(position.x(), 4.0, std::max(4.0, viewport.width() - width - 25)),
                             std::clamp(position.y(), 4.0, std::max(4.0, viewport.height() - 27.0)));
        if (cursor.phase == Phase::Move && (cursor.from.anchor != Anchor::Canvas || cursor.to.anchor != Anchor::Canvas))
        {
            auto trail = cursor.color;
            trail.setAlpha(150);
            painter.setPen(QPen(trail, 2, Qt::DashLine, Qt::RoundCap));
            painter.drawLine(from, screen);
        }
        QRectF badge(screen + QPointF(15, 5), QSizeF(width, 20));
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
        case Phase::Grab:
        case Phase::Select:
            painter.setPen(QPen(cursor.color, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF{}, 5 + 10 * t, 5 + 10 * t);
            DrawCursor(painter, cursor.color, label, badge);
            break;
        case Phase::Move:
        case Phase::Visible:
            DrawCursor(painter, cursor.color, label, badge);
            if (cursor.carrying && cursor.phase == Phase::Move)
            {
                auto fill = cursor.color;
                fill.setAlpha(75);
                painter.setBrush(fill);
                painter.setPen(QPen(cursor.color, 1.5));
                painter.drawRoundedRect(QRectF(-16, -17, 28, 16), 3, 3);
            }
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
