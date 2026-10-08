#include "timeline_widget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "ad/timeline.hpp"
#include "controller.hpp"
#include "qt_render.hpp"

namespace app {

namespace {

const QColor kBg(0x0f, 0x1a, 0x2e);
const QColor kRulerBg(0x13, 0x1f, 0x38);
const QColor kBorder(0x22, 0x31, 0x4f);
const QColor kMuted(0x85, 0x98, 0xb8);
const QColor kPlayhead(0xff, 0x54, 0x70);
const ad::Color kBarBase = ad::Color::rgb(0x0c1526);

}  // namespace

TimelineWidget::TimelineWidget(Controller& ctl, QWidget* parent) : QAbstractScrollArea(parent), ctl_(ctl) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setMouseTracking(true);
    setToolTip(tr("Ctrl + колесо — масштаб таймлайна"));
    setMinimumHeight(120);

    connect(&ctl_, &Controller::modelChanged, this, [this](bool) {
        if (!drag_ || drag_->mode == DragState::Mode::Scrub) relayout();  // при перетаскивании дорожки не перепаковываем
        viewport()->update();
    });
    connect(&ctl_, &Controller::selectionChanged, viewport(), qOverload<>(&QWidget::update));
    connect(&ctl_, &Controller::timeChanged, this, [this] {
        if (ctl_.isPlaying()) followPlayhead();
        viewport()->update();
    });
    relayout();
}

void TimelineWidget::relayout() {
    const auto lanes = ad::packLanes(ctl_.model().scenario.steps);
    lanes_.clear();
    for (const auto& l : lanes) lanes_[l.stepId] = l.lane;
    laneCount_ = ad::laneCount(lanes);
    updateScrollRange();
}

int TimelineWidget::contentWidth() const {
    return static_cast<int>(std::ceil(timeToX(ad::contentEnd(ctl_.model())))) + 40;
}

int TimelineWidget::contentHeight() const { return kRuler + kTopPad + std::max(3, laneCount_) * kRowH + 12; }

void TimelineWidget::updateScrollRange() {
    horizontalScrollBar()->setRange(0, std::max(0, contentWidth() - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setRange(0, std::max(0, contentHeight() - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
}

void TimelineWidget::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    updateScrollRange();
}

void TimelineWidget::scrollContentsBy(int, int) { viewport()->update(); }

void TimelineWidget::followPlayhead() {
    const double x = timeToX(ctl_.time());
    QScrollBar* h = horizontalScrollBar();
    if (x < h->value() || x > h->value() + viewport()->width() - 20) h->setValue(static_cast<int>(x) - 40);
}

QRectF TimelineWidget::barRect(const std::string& stepId) const {
    const ad::Step* s = ctl_.model().step(stepId);
    const auto it = lanes_.find(stepId);
    if (!s || it == lanes_.end()) return {};
    const double x = timeToX(s->start) - horizontalScrollBar()->value();
    const double y = kRuler + kTopPad + it->second * kRowH + 2 - verticalScrollBar()->value();
    return {x, y, std::max(14.0, timeToX(s->duration)), 24};
}

// ---- отрисовка --------------------------------------------------------------

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    const auto& m = ctl_.model();
    const int sx = horizontalScrollBar()->value();
    const int w = viewport()->width();
    const int h = viewport()->height();
    p.fillRect(viewport()->rect(), kBg);

    // бары
    QFont barFont = font();
    barFont.setPixelSize(12);
    p.setFont(barFont);
    p.save();
    p.setClipRect(0, kRuler, w, h - kRuler);
    for (const auto& s : m.scenario.steps) {
        const QRectF r = barRect(s.id);
        if (r.isNull() || r.right() < 0 || r.left() > w) continue;
        const ad::Color c = ad::stepColor(s);
        const QColor qc = toQColor(c);
        QPainterPath path;
        path.addRoundedRect(r, 6, 6);
        p.fillPath(path, toQColor(c.mix(kBarBase, 0.74)));
        p.save();
        p.setClipPath(path);
        p.fillRect(QRectF(r.left(), r.top(), 4, r.height()), qc);
        p.restore();
        p.setPen(QPen(qc, 1));
        p.drawPath(path);
        if (ctl_.selection().is(ad::Selection::Kind::Step, s.id)) {
            p.setPen(QPen(Qt::white, 2));
            p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 7, 7);
        }
        const QRectF tr = r.adjusted(10, 0, -6, 0);
        if (tr.width() > 8) {
            p.setPen(QColor(0xea, 0xf1, 0xff));
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(qs(ad::stepTitle(m, s)), Qt::ElideRight, static_cast<int>(tr.width())));
        }
    }
    p.restore();

    // линейка (прилипает к верху)
    p.fillRect(QRect(0, 0, w, kRuler), kRulerBg);
    p.setPen(kBorder);
    p.drawLine(0, kRuler - 1, w, kRuler - 1);
    const double totalSec = ad::contentEnd(m) / 1000.0;
    const double fullW = timeToX(ad::contentEnd(m));
    const int count = std::max(2, static_cast<int>(std::lround(fullW / 70)));
    const double step = ad::tickStep(0, totalSec, count);
    QFont tickFont = font();
    tickFont.setPixelSize(10);
    p.setFont(tickFont);
    for (const double sec : ad::niceTicks(0, totalSec, count)) {
        const double x = sec * pxPerSec_ - sx;
        if (x < -40 || x > w) continue;
        p.setPen(QColor(120, 145, 190, 56));
        p.drawLine(QPointF(x, 0), QPointF(x, kRuler));
        p.setPen(kMuted);
        p.drawText(QPointF(x + 4, 15), qs(ad::formatTick(sec, step)) + QStringLiteral("с"));
    }

    // конец сцены
    const double endX = timeToX(ctl_.duration()) - sx;
    QPen endPen(kPlayhead, 2, Qt::DashLine);
    endPen.setColor(QColor(0xff, 0x54, 0x70, 128));
    p.setPen(endPen);
    p.drawLine(QPointF(endX, 0), QPointF(endX, h));

    // плейхед
    const double px = timeToX(ctl_.time()) - sx;
    p.setPen(QPen(kPlayhead, 2));
    p.drawLine(QPointF(px, 0), QPointF(px, h));
    QPainterPath tri;
    tri.moveTo(px - 6, 0);
    tri.lineTo(px + 6, 0);
    tri.lineTo(px, 7);
    tri.closeSubpath();
    p.fillPath(tri, kPlayhead);
}

// ---- мышь -------------------------------------------------------------------

void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPointF pos = e->position();
    if (pos.y() < kRuler) {
        ctl_.pause();
        ctl_.seek(xToTime(pos.x() + horizontalScrollBar()->value()));
        drag_ = DragState{DragState::Mode::Scrub, {}, pos.x(), 0, 0, false};
        return;
    }
    for (const auto& s : ctl_.model().scenario.steps) {
        const QRectF r = barRect(s.id);
        if (!r.contains(pos)) continue;
        auto mode = DragState::Mode::Move;
        if (pos.x() - r.left() <= kHandle) mode = DragState::Mode::ResizeL;
        else if (r.right() - pos.x() <= kHandle) mode = DragState::Mode::ResizeR;
        drag_ = DragState{mode, s.id, pos.x(), s.start, s.duration, false};
        ctl_.select(ad::Selection::Kind::Step, s.id);
        return;
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (!drag_) {
        // курсор-подсказка над краями баров
        Qt::CursorShape shape = Qt::ArrowCursor;
        for (const auto& s : ctl_.model().scenario.steps) {
            const QRectF r = barRect(s.id);
            if (!r.contains(pos)) continue;
            shape = (pos.x() - r.left() <= kHandle || r.right() - pos.x() <= kHandle) ? Qt::SizeHorCursor
                                                                                       : Qt::OpenHandCursor;
            break;
        }
        viewport()->setCursor(shape);
        return;
    }
    if (drag_->mode == DragState::Mode::Scrub) {
        ctl_.seek(xToTime(pos.x() + horizontalScrollBar()->value()));
        return;
    }
    if (!ctl_.model().step(drag_->stepId)) return;
    if (!drag_->edited) {
        ctl_.document().checkpoint();
        drag_->edited = true;
    }
    const double dt = xToTime(pos.x() - drag_->pressX);
    ad::Step& s = *ctl_.document().mutableModel().step(drag_->stepId);
    switch (drag_->mode) {
        case DragState::Mode::Move: s.start = ad::snapTime(drag_->origStart + dt); break;
        case DragState::Mode::ResizeR: s.duration = std::max(ad::kMinStepDuration, ad::snapTime(drag_->origDuration + dt)); break;
        case DragState::Mode::ResizeL: {
            const double ns = std::min(ad::snapTime(drag_->origStart + dt), drag_->origStart + drag_->origDuration - ad::kMinStepDuration);
            s.start = std::max(0.0, ns);
            s.duration = drag_->origDuration - (s.start - drag_->origStart);
            break;
        }
        case DragState::Mode::Scrub: break;
    }
    ctl_.changed(false);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ && drag_->edited) {
        drag_.reset();
        ctl_.document().sortSteps();
        ctl_.document().updateDuration();
        ctl_.changed(true);
        return;
    }
    drag_.reset();
}

void TimelineWidget::wheelEvent(QWheelEvent* e) {
    if (!(e->modifiers() & Qt::ControlModifier)) return QAbstractScrollArea::wheelEvent(e);
    const double dy = e->angleDelta().y();
    if (dy == 0) return;
    const double cursorX = e->position().x();
    const double t = xToTime(horizontalScrollBar()->value() + cursorX);  // время под курсором
    pxPerSec_ = std::clamp(pxPerSec_ * (dy > 0 ? 1.15 : 1 / 1.15), 12.0, 600.0);
    updateScrollRange();
    horizontalScrollBar()->setValue(static_cast<int>(timeToX(t) - cursorX));
    viewport()->update();
    e->accept();
}

}  // namespace app
