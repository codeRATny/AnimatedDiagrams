#include "TimelineWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "Controller.hpp"
#include "QtRender.hpp"
#include "Timeline/TimelineLayout.hpp"

namespace ad::ui
{

namespace
{

const QColor kBg(0x0f, 0x1a, 0x2e);
const QColor kRulerBg(0x13, 0x1f, 0x38);
const QColor kBorder(0x22, 0x31, 0x4f);
const QColor kMuted(0x85, 0x98, 0xb8);
const QColor kPlayhead(0xff, 0x54, 0x70);
const Color  kBarBase = Color::Rgb(0x0c1526);

} // namespace

TimelineWidget::TimelineWidget(Controller &ctl, QWidget *parent) : QAbstractScrollArea(parent), _ctl(ctl)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setMouseTracking(true);
    setToolTip(tr("Ctrl + колесо — масштаб таймлайна"));
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

int TimelineWidget::_ContentWidth() const { return static_cast<int>(std::ceil(_TimeToX(ContentEnd(_ctl.GetModel())))) + 40; }

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

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void TimelineWidget::paintEvent(QPaintEvent * /*e*/)
{
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    const auto &m  = _ctl.GetModel();
    const int   sx = horizontalScrollBar()->value();
    const int   w  = viewport()->width();
    const int   h  = viewport()->height();
    p.fillRect(viewport()->rect(), kBg);

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
        p.fillPath(path, ToQColor(c.Mix(kBarBase, 0.74)));
        p.save();
        p.setClipPath(path);
        p.fillRect(QRectF(r.left(), r.top(), 4, r.height()), qc);
        p.restore();
        p.setPen(QPen(qc, 1));
        p.drawPath(path);
        if (_ctl.GetSelection().Is(Selection::Kind::Step, s.id))
        {
            p.setPen(QPen(Qt::white, 2));
            p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 7, 7);
        }
        const QRectF text_rect = r.adjusted(10, 0, -6, 0);
        if (text_rect.width() > 8)
        {
            p.setPen(QColor(0xea, 0xf1, 0xff));
            p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(Qs(StepTitle(m, s, _ctl.Reg())), Qt::ElideRight, static_cast<int>(text_rect.width())));
        }
    }
    p.restore();

    // ruler (sticks to the top)
    p.fillRect(QRect(0, 0, w, kRuler), kRulerBg);
    p.setPen(kBorder);
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
        p.setPen(QColor(120, 145, 190, 56));
        p.drawLine(QPointF(x, 0), QPointF(x, kRuler));
        p.setPen(kMuted);
        p.drawText(QPointF(x + 4, 15), Qs(FormatTick(sec, step)) + tr("с"));
    }

    // end of the scene
    const double end_x = _TimeToX(_ctl.Duration()) - sx;
    QPen         end_pen(QColor(0xff, 0x54, 0x70, 128), 2, Qt::DashLine);
    p.setPen(end_pen);
    p.drawLine(QPointF(end_x, 0), QPointF(end_x, h));

    // playhead
    const double px = _TimeToX(_ctl.Time()) - sx;
    p.setPen(QPen(kPlayhead, 2));
    p.drawLine(QPointF(px, 0), QPointF(px, h));
    QPainterPath tri;
    tri.moveTo(px - 6, 0);
    tri.lineTo(px + 6, 0);
    tri.lineTo(px, 7);
    tri.closeSubpath();
    p.fillPath(tri, kPlayhead);
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
        // cursor hint over the bar edges
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
        break;
    }
    _ctl.Changed(false);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent * /*e*/)
{
    const bool edited = _drag.has_value() && _drag->edited;
    _drag.reset();
    if (edited)
    {
        _ctl.Doc().SortSteps();
        _ctl.Doc().UpdateDuration();
        _ctl.Doc().BreakMerge();
        _ctl.Changed(true);
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
