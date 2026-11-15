#ifndef _UI_AGENT_OVERLAY_HPP_
#define _UI_AGENT_OVERLAY_HPP_

#include <QColor>
#include <QCoreApplication>
#include <QPointF>
#include <QRect>

#include <deque>
#include <functional>
#include <map>
#include <set>

#include "Mcp/DocumentTools.hpp"

class QPainter;

namespace ad::ui
{

/// Cursor feedback for one canvas. Positions are in world coordinates; glyphs are drawn in pixels.
class AgentOverlay
{
    Q_DECLARE_TR_FUNCTIONS(AgentOverlay)

public:
    using Clock                        = mcp::DocumentHost::Clock;
    static constexpr auto kIdleDelay   = std::chrono::seconds(30);
    static constexpr auto kDefaultMove = std::chrono::milliseconds(180);

    enum class Anchor
    {
        Canvas,
        Palette,
        StepType,
        AddStep
    };
    struct Target
    {
        QPointF     world;
        Anchor      anchor       = Anchor::Canvas;
        std::string element_type = {};
    };
    using Project = std::function<QPointF(const Target &)>;

    explicit AgentOverlay(std::chrono::milliseconds move_duration = kDefaultMove);

    void Apply(const mcp::DocumentHost::AgentActivity &activity, mcp::DocumentHost::DocumentKey canvas_document, QPointF fallback,
               Clock::time_point now);
    void Clear() { _cursors.clear(); }
    [[nodiscard]] std::set<std::string> PendingNodes() const;

    /// Draw and return the delay before the next animation frame or idle transition.
    std::optional<std::chrono::milliseconds> Paint(QPainter &painter, const QRect &viewport, const View &view, Clock::time_point now);
    std::optional<std::chrono::milliseconds> PaintProjected(QPainter &painter, const QRect &viewport, const Project &project,
                                                            Clock::time_point now);

private:
    enum class Phase
    {
        Assemble,
        Visible,
        Move,
        Grab,
        Select,
        GlitchOut,
        Hidden,
        GlitchIn,
        PortalIn,
        PortalOut,
        Disassemble,
    };

    struct Action
    {
        Phase                 phase;
        std::optional<Target> point;
        bool                  carrying       = false;
        std::string           placement_node = {};
    };

    struct Cursor
    {
        Target                           from;
        Target                           to;
        QColor                           color;
        Phase                            phase = Phase::Assemble;
        std::optional<Clock::time_point> phase_start;
        Clock::time_point                last_activity;
        std::deque<Action>               pending;
        std::optional<double>            frozen       = {};
        bool                             carrying     = false;
        bool                             leaving      = false;
        bool                             unregistered = false;
    };

    std::chrono::milliseconds _Duration(Phase phase) const;
    bool                      _Advance(Cursor &cursor, Clock::time_point now) const;
    double                    _Progress(const Cursor &cursor, Clock::time_point now) const;

    std::chrono::milliseconds     _move_duration;
    std::map<std::string, Cursor> _cursors;
};

} // namespace ad::ui

#endif // _UI_AGENT_OVERLAY_HPP_
