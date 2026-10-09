#include "TimelineWidget.hpp"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "Controller.hpp"
#include "Model/Markers.hpp"
#include "QtRender.hpp"
#include "Theme.hpp"
#include "Timeline/TimelineLayout.hpp"

namespace ad::ui
{

TimelineWidget::TimelineWidget(Controller &ctl, QWidget *parent) : QAbstractScrollArea(parent), _ctl(ctl)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setMouseTracking(true);
    setToolTip(tr("Ctrl + wheel — zoom the timeline. Right-click the ruler to add a marker"));
    setMinimumHeight(120);

    connect(&_ctl, &Controller::ModelChanged, this,
            [this](bool /*structural*/)
            {
                // lanes are not repacked while a bar is being dragged
                if (!_drag.has_value() || _drag->mode == Mode::Scrub)
                {
                    _Relayout();
                }
                viewport()->update();
            });
    connect(&_ctl, &Controller::SelectionChanged, viewport(), qOverload<>(&QWidget::update));
    connect(&_ctl, &Controller::LibraryChanged, viewport(), qOverload<>(&QWidget::update));
    connect(&_ctl, &Controller::TimeChanged, this,
            [this]
            {
                if (_ctl.IsPlaying())
                {
                    _FollowPlayhead();
                }
                viewport()->update();
            });
    _Relayout();
}

void TimelineWidget::_Relayout()
{
    const auto lanes = PackLanes(_ctl.GetModel().scenario.steps);
    _lanes.clear();
    for (const auto &l : lanes)
    {
        _lanes[l.step_id] = l.lane;
    }
    _lane_count = LaneCount(lanes);
    _UpdateScrollRange();
}

int TimelineWidget::_ContentWidth() const
{
    return static_cast<int>(std::min(1e8, std::ceil(_TimeToX(ContentEnd(_ctl.GetModel()))))) + 40; // stays in int range
}

int TimelineWidget::_ContentHeight() const { return kRuler + kTopPad + std::max(3, _lane_count) * kRowH + 12; }

void TimelineWidget::_UpdateScrollRange()
{
    horizontalScrollBar()->setRange(0, std::max(0, _ContentWidth() - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setRange(0, std::max(0, _ContentHeight() - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
}

void TimelineWidget::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    _UpdateScrollRange();
}

void TimelineWidget::scrollContentsBy(int /*dx*/, int /*dy*/) { viewport()->update(); }

void TimelineWidget::_FollowPlayhead()
{
    const double x = _TimeToX(_ctl.Time());
    QScrollBar  *h = horizontalScrollBar();
    if (x < h->value() || x > h->value() + viewport()->width() - 20)
    {
        h->setValue(static_cast<int>(x) - 40);
    }
}

QRectF TimelineWidget::_BarRect(const std::string &step_id) const
{
    const Step *s  = _ctl.GetModel().FindStep(step_id);
    const auto  it = _lanes.find(step_id);
    if (s == nullptr || it == _lanes.end())
    {
        return {};
    }
    const double x = _TimeToX(s->start) - horizontalScrollBar()->value();
    const double y = kRuler + kTopPad + (it->second * kRowH) + 2 - verticalScrollBar()->value();
    return {x, y, std::max(14.0, _TimeToX(s->duration)), 24};
}

double TimelineWidget::_MarkerRoom(const Marker &m) const
{
    // a flag does not cover the next marker
    double room = 160;
    for (const auto &other : _ctl.GetModel().scenario.markers)
    {
        if (other.time > m.time + kMarkerEpsilon)
        {
            room = std::min(room, _TimeToX(other.time) - _TimeToX(m.time) - 3);
        }
    }
    return std::max(10.0, room);
}

QString TimelineWidget::_MarkerText(const Marker &m) const
{
    QFont f = font();
    f.setPixelSize(11);
    const int room = static_cast<int>(_MarkerRoom(m)) - 12;
    return room < 12 ? QString() : QFontMetrics(f).elidedText(Qs(m.label), Qt::ElideRight, room);
}

QRectF TimelineWidget::_MarkerRect(const Marker &m) const
{
    QFont f = font();
    f.setPixelSize(11);
    const QString text = _MarkerText(m);
    const double  w    = text.isEmpty() ? 10.0 : std::min(_MarkerRoom(m), QFontMetrics(f).horizontalAdvance(text) + 12.0);
    const double  x    = _TimeToX(m.time) - horizontalScrollBar()->value();
    return {x, 4, w, 16};
}

std::string TimelineWidget::_MarkerAt(QPointF pos) const
{
    const auto &markers = _ctl.GetModel().scenario.markers;
    for (auto it = markers.rbegin(); it != markers.rend(); ++it) // the last drawn is on top
    {
        if (_MarkerRect(*it).adjusted(-4, -4, 2, 6).contains(pos))
        {
            return it->id;
        }
    }
    return {};
}

bool TimelineWidget::AddMarkerAt(double time)
{
    const Marker *m = _ctl.Doc().AddMarker(std::round(time));
    if (m == nullptr)
    {
        Q_EMIT StatusMessage(tr("There already is a marker at %1").arg(Qs(FormatTime(time))));
        return false;
    }
    _ctl.Changed(false);
    return true;
}

void TimelineWidget::RenameMarker(const std::string &marker_id)
{
    const Marker *m = _ctl.GetModel().FindMarker(marker_id);
    if (m == nullptr)
    {
        return;
    }
    bool          ok    = false;
    const QString label = QInputDialog::getText(this, tr("Marker"), tr("Marker label (shown in presenter mode and on slides):"),
                                                QLineEdit::Normal, Qs(m->label), &ok);
    if (ok && _ctl.Doc().RenameMarker(marker_id, Us(label.trimmed())))
    {
        _ctl.Changed(false);
    }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void TimelineWidget::_PaintMarkers(QPainter &p, int h)
{
    const auto &markers = _ctl.GetModel().scenario.markers;
    if (markers.empty())
    {
        return;
    }
    const Color c = Ui().warning;
    QFont       f = font();
    f.setPixelSize(11);
    p.setFont(f);
    for (const auto &m : markers)
    {
        const QRectF r = _MarkerRect(m);
        if (r.right() < 0 || r.left() > viewport()->width())
        {
            continue;
        }
        const bool hot = m.id == _hover_marker || (_drag.has_value() && _drag->mode == Mode::Marker && _drag->step_id == m.id);
        // thin line over the lanes
        p.setPen(QPen(ToQColor(c, hot ? 0.9 : 0.55), 1));
        p.drawLine(QPointF(r.left() + 0.5, r.bottom()), QPointF(r.left() + 0.5, h));
        // flag on the ruler: straight left edge at the marker time
        QPainterPath flag;
        flag.moveTo(r.left(), r.top());
        flag.lineTo(r.right() - 3, r.top());
        flag.quadTo(r.right(), r.top(), r.right(), r.top() + 3);
        flag.lineTo(r.right(), r.bottom() - 3);
        flag.quadTo(r.right(), r.bottom(), r.right() - 3, r.bottom());
        flag.lineTo(r.left(), r.bottom());
        flag.closeSubpath();
        p.fillPath(flag, ToQColor(c.Mix(Ui().base, hot ? 0.45 : 0.68)));
        p.setPen(QPen(ToQColor(c), 1));
        p.drawPath(flag);
        p.fillRect(QRectF(r.left(), r.top(), 2, r.height()), ToQColor(c));
        const QString text = _MarkerText(m);
        if (!text.isEmpty())
        {
            p.setPen(ToQColor(Ui().text));
            p.drawText(r.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
        }
    }
}

void TimelineWidget::paintEvent(QPaintEvent * /*e*/)
{
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    const auto &m  = _ctl.GetModel();
    const int   sx = horizontalScrollBar()->value();
    const int   w  = viewport()->width();
    const int   h  = viewport()->height();
    p.fillRect(viewport()->rect(), ToQColor(Ui().window));

    // bars
    QFont bar_font = font();
    bar_font.setPixelSize(12);
    p.setFont(bar_font);
    p.save();
    p.setClipRect(0, kRuler, w, h - kRuler);
    for (const auto &s : m.scenario.steps)
    {
        const QRectF r = _BarRect(s.id);
        if (r.isNull() || r.right() < 0 || r.left() > w)
        {
            continue;
        }
        const Color  c  = StepColor(s);
        const QColor qc = ToQColor(c);
        QPainterPath path;
        path.addRoundedRect(r, 6, 6);
        p.fillPath(path, ToQColor(c.Mix(Ui().base, Ui().light ? 0.82 : 0.74)));
        p.save();
        p.setClipPath(path);
        p.fillRect(QRectF(r.left(), r.top(), 4, r.height()), qc);
        p.restore();
        p.setPen(QPen(qc, 1));
        p.drawPath(path);
        if (_ctl.GetSelection().Is(Selection::Kind::Step, s.id))
        {
            p.setPen(QPen(ToQColor(Ui().text), 2));
            p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 7, 7);
        }
        const QRectF text_rect = r.adjusted(10, 0, -6, 0);
        if (text_rect.width() > 8)
        {
            p.setPen(ToQColor(Ui().text));
            p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(Qs(StepTitle(m, s, _ctl.Reg())), Qt::ElideRight, static_cast<int>(text_rect.width())));
        }
    }
    p.restore();

    // ruler (sticks to the top)
    p.fillRect(QRect(0, 0, w, kRuler), ToQColor(Ui().panel));
    p.setPen(ToQColor(Ui().border));
    p.drawLine(0, kRuler - 1, w, kRuler - 1);
    const double total_sec = ContentEnd(m) / 1000.0;
    const double full_w    = _TimeToX(ContentEnd(m));
    const int    count     = std::max(2, static_cast<int>(std::lround(full_w / 70)));
    const double step      = TickStep(0, total_sec, count);
    QFont        tick_font = font();
    tick_font.setPixelSize(10);
    p.setFont(tick_font);
    for (const double sec : NiceTicks(0, total_sec, count))
    {
        const double x = (sec * _px_per_sec) - sx;
        if (x < -40 || x > w)
        {
            continue;
        }
        p.setPen(ToQColor(Ui().border, 0.45));
        p.drawLine(QPointF(x, 0), QPointF(x, kRuler));
        p.setPen(ToQColor(Ui().muted));
        p.drawText(QPointF(x + 4, 15), tr("%1s").arg(Qs(FormatTick(sec, step))));
    }

    _PaintMarkers(p, h);

    // end of the scene
    const double end_x = _TimeToX(_ctl.Duration()) - sx;
    QPen         end_pen(ToQColor(Ui().danger, 0.5), 2, Qt::DashLine);
    p.setPen(end_pen);
    p.drawLine(QPointF(end_x, 0), QPointF(end_x, h));

    // playhead
    const double px = _TimeToX(_ctl.Time()) - sx;
    p.setPen(QPen(ToQColor(Ui().danger), 2));
    p.drawLine(QPointF(px, 0), QPointF(px, h));
    QPainterPath tri;
    tri.moveTo(px - 6, 0);
    tri.lineTo(px + 6, 0);
    tri.lineTo(px, 7);
    tri.closeSubpath();
    p.fillPath(tri, ToQColor(Ui().danger));
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
    {
        return;
    }
    const QPointF pos = e->position();
    if (const std::string marker = _MarkerAt(pos); !marker.empty())
    {
        const Marker *m = _ctl.GetModel().FindMarker(marker);
        _ctl.Pause();
        _ctl.Seek(m->time);
        _drag = DragState{.mode = Mode::Marker, .step_id = marker, .press_x = pos.x(), .orig_start = m->time};
        viewport()->update();
        return;
    }
    if (pos.y() < kRuler)
    {
        _ctl.Pause();
        _ctl.Seek(_XToTime(pos.x() + horizontalScrollBar()->value()));
        _drag = DragState{.mode = Mode::Scrub, .step_id = {}, .press_x = pos.x()};
        return;
    }
    for (const auto &s : _ctl.GetModel().scenario.steps)
    {
        const QRectF r = _BarRect(s.id);
        if (!r.contains(pos))
        {
            continue;
        }
        Mode mode = Mode::Move;
        if (pos.x() - r.left() <= kHandle)
        {
            mode = Mode::ResizeL;
        }
        else if (r.right() - pos.x() <= kHandle)
        {
            mode = Mode::ResizeR;
        }
        _drag = DragState{.mode = mode, .step_id = s.id, .press_x = pos.x(), .orig_start = s.start, .orig_duration = s.duration};
        _ctl.Select(Selection::Kind::Step, s.id);
        return;
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if (!_drag.has_value())
    {
        // cursor hint over markers and the bar edges
        const std::string marker = _MarkerAt(pos);
        if (marker != _hover_marker)
        {
            _hover_marker = marker;
            viewport()->update();
        }
        if (!marker.empty())
        {
            viewport()->setCursor(Qt::SizeHorCursor);
            return;
        }
        Qt::CursorShape shape = Qt::ArrowCursor;
        for (const auto &s : _ctl.GetModel().scenario.steps)
        {
            const QRectF r = _BarRect(s.id);
            if (!r.contains(pos))
            {
                continue;
            }
            shape = (pos.x() - r.left() <= kHandle || r.right() - pos.x() <= kHandle) ? Qt::SizeHorCursor : Qt::OpenHandCursor;
            break;
        }
        viewport()->setCursor(shape);
        return;
    }
    if (_drag->mode == Mode::Scrub)
    {
        _ctl.Seek(_XToTime(pos.x() + horizontalScrollBar()->value()));
        return;
    }
    if (_drag->mode == Mode::Marker)
    {
        const Marker *m = _ctl.GetModel().FindMarker(_drag->step_id);
        if (m == nullptr)
        {
            return;
        }
        const double t = std::clamp(SnapTime(_drag->orig_start + _XToTime(pos.x() - _drag->press_x)), 0.0, _ctl.Duration());
        // one undo step for the whole drag; a time taken by another marker is skipped
        if (std::abs(t - m->time) >= kMarkerEpsilon && _ctl.Doc().MoveMarker(_drag->step_id, t, "timeline:marker-drag"))
        {
            _drag->edited = true;
            _ctl.Changed(false);
            _ctl.Seek(t);
        }
        return;
    }
    if (_ctl.GetModel().FindStep(_drag->step_id) == nullptr)
    {
        return;
    }
    if (!_drag->edited)
    {
        _ctl.Doc().Checkpoint();
        _drag->edited = true;
    }
    const double dt = _XToTime(pos.x() - _drag->press_x);
    Step        &s  = *_ctl.Doc().Mutable().FindStep(_drag->step_id);
    switch (_drag->mode)
    {
    case Mode::Move:
        s.start = SnapTime(_drag->orig_start + dt);
        break;
    case Mode::ResizeR:
        s.duration = std::max(kMinStepDuration, SnapTime(_drag->orig_duration + dt));
        break;
    case Mode::ResizeL:
    {
        const double ns = std::min(SnapTime(_drag->orig_start + dt), _drag->orig_start + _drag->orig_duration - kMinStepDuration);
        s.start         = std::max(0.0, ns);
        s.duration      = _drag->orig_duration - (s.start - _drag->orig_start);
        break;
    }
    case Mode::Scrub:
    case Mode::Marker:
        break;
    }
    _ctl.Changed(false);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent * /*e*/)
{
    const bool edited = _drag.has_value() && _drag->edited;
    const bool marker = _drag.has_value() && _drag->mode == Mode::Marker;
    _drag.reset();
    if (marker)
    {
        _ctl.Doc().BreakMerge();
        viewport()->update();
        return;
    }
    if (edited)
    {
        _ctl.Doc().SortSteps();
        _ctl.Doc().UpdateDuration();
        _ctl.Doc().BreakMerge();
        _ctl.Changed(true);
    }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
    {
        return;
    }
    if (const std::string marker = _MarkerAt(e->position()); !marker.empty())
    {
        _drag.reset();
        RenameMarker(marker);
    }
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent *e)
{
    const QPointF     pos    = e->pos();
    const std::string marker = _MarkerAt(pos);
    if (marker.empty() && pos.y() >= kRuler)
    {
        return;
    }
    const double t = std::clamp(SnapTime(_XToTime(pos.x() + horizontalScrollBar()->value())), 0.0, _ctl.Duration());
    QMenu        menu(this);
    QAction     *add = menu.addAction(tr("Add marker here (%1)").arg(Qs(FormatTime(t))));
    add->setEnabled(marker.empty() && MarkerAt(_ctl.GetModel().scenario, t) == nullptr);
    connect(add, &QAction::triggered, this,
            [this, t]
            {
                AddMarkerAt(t);
            });
    if (!marker.empty())
    {
        menu.addAction(tr("Rename marker…"), this,
                       [this, marker]
                       {
                           RenameMarker(marker);
                       });
        menu.addAction(tr("Delete marker"), this,
                       [this, marker]
                       {
                           if (_ctl.Doc().RemoveMarker(marker))
                           {
                               _hover_marker.clear();
                               _ctl.Changed(false);
                           }
                       });
    }
    menu.exec(e->globalPos());
}

void TimelineWidget::leaveEvent(QEvent *e)
{
    QAbstractScrollArea::leaveEvent(e);
    if (!_hover_marker.empty())
    {
        _hover_marker.clear();
        viewport()->update();
    }
}

void TimelineWidget::wheelEvent(QWheelEvent *e)
{
    if (!e->modifiers().testFlag(Qt::ControlModifier))
    {
        QAbstractScrollArea::wheelEvent(e);
        return;
    }
    const double dy = e->angleDelta().y();
    if (dy == 0)
    {
        return;
    }
    const double cursor_x = e->position().x();
    const double t        = _XToTime(horizontalScrollBar()->value() + cursor_x); // time under the cursor
    _px_per_sec           = std::clamp(_px_per_sec * (dy > 0 ? 1.15 : 1 / 1.15), 12.0, 600.0);
    _UpdateScrollRange();
    horizontalScrollBar()->setValue(static_cast<int>(_TimeToX(t) - cursor_x));
    viewport()->update();
    e->accept();
}

} // namespace ad::ui
