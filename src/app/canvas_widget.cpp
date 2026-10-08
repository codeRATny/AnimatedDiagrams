#include "canvas_widget.hpp"

#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "ad/engine.hpp"
#include "ad/hittest.hpp"
#include "ad/scene.hpp"
#include "controller.hpp"
#include "qt_render.hpp"

namespace app {

using Kind = ad::Selection::Kind;

CanvasWidget::CanvasWidget(Controller& ctl, QWidget* parent) : QWidget(parent), ctl_(ctl) {
    setMouseTracking(false);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumSize(320, 200);
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto repaint = [this] { update(); };
    connect(&ctl_, &Controller::modelChanged, this, repaint);
    connect(&ctl_, &Controller::selectionChanged, this, repaint);
    connect(&ctl_, &Controller::timeChanged, this, repaint);
    connect(&ctl_, &Controller::playingChanged, this, [this] {
        cancelInteraction();
        updateCursor();
        update();
    });
    updateCursor();
}

void CanvasWidget::setTool(Tool tool) {
    tool_ = tool;
    connect_.reset();
    updateCursor();
    updateHint();
    update();
    emit toolChanged(tool);
}

void CanvasWidget::cancelInteraction() {
    connect_.reset();
    drag_ = Drag::None;
    update();
}

void CanvasWidget::updateHint() {
    switch (tool_) {
        case Tool::Select:
            hint_ = tr("Выбор: клик — выделить, перетаскивание — двигать, пустое место — панорама. "
                       "Двойной клик: узел — переименовать, связь — точка изгиба.");
            break;
        case Tool::Node: hint_ = tr("Узел: кликните на холст, чтобы добавить узел выбранного типа."); break;
        case Tool::Edge: hint_ = tr("Связь: кликните первый узел (или точку соединения), затем второй."); break;
    }
}

void CanvasWidget::updateCursor() {
    if (ctl_.isPlaying()) return setCursor(Qt::ArrowCursor);
    switch (tool_) {
        case Tool::Select: setCursor(drag_ == Drag::Node ? Qt::ClosedHandCursor : Qt::ArrowCursor); break;
        case Tool::Node: setCursor(Qt::CrossCursor); break;
        case Tool::Edge: setCursor(Qt::PointingHandCursor); break;
    }
}

// ---- вид --------------------------------------------------------------------

ad::Vec2 CanvasWidget::toWorld(QPointF s) const {
    const auto& v = ctl_.model().view;
    return {(s.x() - v.panX) / v.zoom, (s.y() - v.panY) / v.zoom};
}

double CanvasWidget::tolerance() const { return 3 / ctl_.model().view.zoom; }

void CanvasWidget::zoomBy(double factor, std::optional<QPointF> anchor) {
    ad::View v = ctl_.model().view;
    const QPointF c = anchor.value_or(QPointF(width() / 2.0, height() / 2.0));
    const double wx = (c.x() - v.panX) / v.zoom;
    const double wy = (c.y() - v.panY) / v.zoom;
    v.zoom = std::clamp(v.zoom * factor, 0.25, 3.0);
    v.panX = c.x() - wx * v.zoom;
    v.panY = c.y() - wy * v.zoom;
    ctl_.setView(v);
    update();
}

void CanvasWidget::resetView() {
    ctl_.setView({});
    update();
}

void CanvasWidget::fitView() {
    const auto& m = ctl_.model();
    if (m.nodes.empty()) return resetView();
    const ad::Rect r = ad::contentBounds(m, QtTextMeasurer{});
    constexpr double pad = 60;
    const double zoom = std::clamp(std::min((width() - pad * 2) / std::max(1.0, r.w), (height() - pad * 2) / std::max(1.0, r.h)),
                                   0.25, 2.0);
    const ad::Vec2 c = r.center();
    ctl_.setView({zoom, width() / 2.0 - c.x * zoom, height() / 2.0 - c.y * zoom});
    update();
}

ad::Rect CanvasWidget::visibleWorldRect() const {
    const auto& v = ctl_.model().view;
    return {-v.panX / v.zoom, -v.panY / v.zoom, width() / v.zoom, height() / v.zoom};
}

// ---- отрисовка --------------------------------------------------------------

void CanvasWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), toQColor(ad::palette::kCanvasBg));

    // сетка в экранных координатах
    p.setPen(QPen(QColor(90, 120, 170, 18), 1));
    for (int x = 0; x < width(); x += 26) p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += 26) p.drawLine(0, y, width(), y);

    const auto& m = ctl_.model();
    p.translate(m.view.panX, m.view.panY);
    p.scale(m.view.zoom, m.view.zoom);

    ad::SceneOptions opt;
    opt.selection = ctl_.selection();
    opt.connectMode = tool_ == Tool::Edge;
    if (connect_) opt.connectFromNode = connect_->node;
    renderFrame(p, ad::buildFrame(m, ctl_.time(), opt, QtTextMeasurer{}));

    // подсказка по инструменту — в левом нижнем углу холста
    if (!hint_.isEmpty() && !ctl_.isPlaying()) {
        p.resetTransform();
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QString text = fm.elidedText(hint_, Qt::ElideRight, std::max(50, width() - 40));
        const QRectF box(10, height() - 34, fm.horizontalAdvance(text) + 20, 24);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0x22, 0x31, 0x4f), 1));
        p.setBrush(QColor(0x0b, 0x12, 0x20, 220));
        p.drawRoundedRect(box, 7, 7);
        p.setPen(QColor(0x85, 0x98, 0xb8));
        p.drawText(box, Qt::AlignCenter, text);
    }
}

// ---- мышь -------------------------------------------------------------------

void CanvasWidget::beginDragEdit() {
    if (moved_) return;
    moved_ = true;
    ctl_.document().checkpoint();  // одна запись истории на всё перетаскивание
}

void CanvasWidget::mousePressEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    const ad::Vec2 w = toWorld(pos);
    pressPos_ = pos;
    moved_ = false;

    auto startPan = [&] {
        drag_ = Drag::Pan;
        panOrigin_ = QPointF(ctl_.model().view.panX, ctl_.model().view.panY);
    };

    if (e->button() == Qt::MiddleButton) return startPan();
    if (e->button() != Qt::LeftButton) return;
    if (ctl_.isPlaying()) return startPan();  // во время проигрывания — только просмотр

    const ad::Hit hit = ad::hitTest(ctl_.model(), w, ctl_.selection(), tolerance());

    if (tool_ == Tool::Node) {
        const std::string id = ctl_.document().addNode(w, pendingKind_).id;
        ctl_.changed(true);
        ctl_.select(Kind::Node, id);
        return;
    }

    if (tool_ == Tool::Edge) {
        std::string node;
        std::string port;
        if (hit.kind == ad::Hit::Kind::Port) {
            node = hit.id;
            port = hit.portId;
        } else if (hit.kind == ad::Hit::Kind::Node) {
            node = hit.id;
        }
        if (node.empty()) {
            connect_.reset();
        } else if (!connect_) {
            connect_ = ConnectFrom{node, port};
        } else if (connect_->node != node) {
            if (const ad::Edge* edge = ctl_.document().addEdge(connect_->node, node, connect_->port, port)) {
                const std::string id = edge->id;
                ctl_.changed(true);
                ctl_.select(Kind::Edge, id);
            }
            connect_.reset();
        } else {
            connect_.reset();
        }
        update();
        return;
    }

    // инструмент «Выбор»
    switch (hit.kind) {
        case ad::Hit::Kind::Port:
            ctl_.select(Kind::Node, hit.id);
            drag_ = Drag::Port;
            dragId_ = hit.id;
            dragPort_ = hit.portId;
            break;
        case ad::Hit::Kind::Waypoint:
            drag_ = Drag::Waypoint;
            dragId_ = hit.id;
            dragWaypoint_ = hit.waypoint;
            break;
        case ad::Hit::Kind::Node: {
            ctl_.select(Kind::Node, hit.id);
            const ad::Node* n = ctl_.model().node(hit.id);
            drag_ = Drag::Node;
            dragId_ = hit.id;
            grabOffset_ = {w.x - n->x, w.y - n->y};
            updateCursor();
            break;
        }
        case ad::Hit::Kind::Edge: ctl_.select(Kind::Edge, hit.id); break;
        case ad::Hit::Kind::None:
            ctl_.clearSelection();
            startPan();
            break;
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ == Drag::None) return;
    const QPointF pos = e->position();
    if (!moved_ && (pos - pressPos_).manhattanLength() < 3) return;  // защита от дрожания при клике
    const ad::Vec2 w = toWorld(pos);

    if (drag_ == Drag::Pan) {
        ad::View v = ctl_.model().view;
        v.panX = panOrigin_.x() + (pos.x() - pressPos_.x());
        v.panY = panOrigin_.y() + (pos.y() - pressPos_.y());
        moved_ = true;
        ctl_.setView(v);
        update();
        return;
    }

    beginDragEdit();
    ad::Model& m = ctl_.document().mutableModel();
    switch (drag_) {
        case Drag::Node:
            if (ad::Node* n = m.node(dragId_)) {
                n->x = std::round(w.x - grabOffset_.x);
                n->y = std::round(w.y - grabOffset_.y);
            }
            break;
        case Drag::Port:
            if (ad::Node* n = m.node(dragId_))
                if (ad::Port* p = n->port(dragPort_)) {
                    p->dx = std::round(w.x - n->x);
                    p->dy = std::round(w.y - n->y);
                }
            break;
        case Drag::Waypoint:
            if (ad::Edge* ed = m.edge(dragId_); ed && dragWaypoint_ < ed->waypoints.size())
                ed->waypoints[dragWaypoint_] = {std::round(w.x), std::round(w.y)};
            break;
        default: break;
    }
    ctl_.changed(false);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent*) {
    if (moved_ && drag_ != Drag::Pan && drag_ != Drag::None) ctl_.document().breakMerge();
    drag_ = Drag::None;
    moved_ = false;
    updateCursor();
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || tool_ != Tool::Select || ctl_.isPlaying()) return;
    const ad::Vec2 w = toWorld(e->position());
    const ad::Hit hit = ad::hitTest(ctl_.model(), w, ctl_.selection(), tolerance());
    auto& doc = ctl_.document();
    switch (hit.kind) {
        case ad::Hit::Kind::Waypoint:  // удалить точку изгиба
            doc.removeWaypoint(hit.id, hit.waypoint);
            ctl_.changed(true);
            break;
        case ad::Hit::Kind::Edge: {  // новая точка изгиба на ближайшем сегменте
            const ad::Edge* ed = ctl_.model().edge(hit.id);
            doc.addWaypoint(hit.id, w, ad::nearestSegmentIndex(ctl_.model(), *ed, w));
            ctl_.changed(true);
            ctl_.select(Kind::Edge, hit.id);
            break;
        }
        case ad::Hit::Kind::Node: {  // переименование
            drag_ = Drag::None;
            const ad::Node* n = ctl_.model().node(hit.id);
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Узел"), tr("Название узла:"), QLineEdit::Normal,
                                                       qs(n->label), &ok);
            if (ok) {
                const std::string id = hit.id;
                ctl_.edit({}, [&](ad::Model& m) { m.node(id)->label = us(name); });
            }
            break;
        }
        default: break;
    }
}

void CanvasWidget::wheelEvent(QWheelEvent* e) {
    const double dy = e->angleDelta().y();
    if (dy == 0) return;
    zoomBy(dy > 0 ? 1.1 : 1 / 1.1, e->position());
    e->accept();
}

}  // namespace app
