#include "CanvasWidget.hpp"

#include <QInputDialog>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "Controller.hpp"
#include "Engine/Engine.hpp"
#include "Engine/Scene.hpp"
#include "Interaction/HitTest.hpp"
#include "PaletteWidget.hpp"
#include "QtRender.hpp"
#include "Theme.hpp"

namespace ad::ui
{

using Kind = Selection::Kind;

CanvasWidget::CanvasWidget(Controller &ctl, QWidget *parent) : QWidget(parent), _ctl(ctl)
{
    setFocusPolicy(Qt::ClickFocus);
    setMinimumSize(320, 200);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAcceptDrops(true);
    auto repaint = [this]
    {
        update();
    };
    connect(&_ctl, &Controller::ModelChanged, this, repaint);
    connect(&_ctl, &Controller::SelectionChanged, this, repaint);
    connect(&_ctl, &Controller::TimeChanged, this, repaint);
    connect(&_ctl, &Controller::LibraryChanged, this, repaint);
    connect(&_ctl, &Controller::PlayingChanged, this,
            [this]
            {
                CancelInteraction();
                _UpdateCursor();
            });
    _UpdateCursor();
    _UpdateHint();
}

void CanvasWidget::SetTool(Tool tool)
{
    _tool = tool;
    _connect.reset();
    _UpdateCursor();
    _UpdateHint();
    update();
    Q_EMIT ToolChanged(tool);
}

void CanvasWidget::CancelInteraction()
{
    _connect.reset();
    _drag = Drag::None;
    update();
}

void CanvasWidget::_UpdateHint()
{
    switch (_tool)
    {
    case Tool::Select:
        _hint = tr("Выбор: клик — выделить, перетаскивание — двигать, пустое место — панорама. "
                   "Двойной клик: узел — переименовать, связь — точка изгиба.");
        break;
    case Tool::Node:
        _hint = tr("Узел: кликните на холст, чтобы добавить элемент выбранного типа.");
        break;
    case Tool::Edge:
        _hint = tr("Связь: кликните первый узел (или точку соединения), затем второй.");
        break;
    }
}

void CanvasWidget::_UpdateCursor()
{
    if (_ctl.IsPlaying())
    {
        setCursor(Qt::ArrowCursor);
        return;
    }
    switch (_tool)
    {
    case Tool::Select:
        setCursor(_drag == Drag::Node ? Qt::ClosedHandCursor : Qt::ArrowCursor);
        break;
    case Tool::Node:
        setCursor(Qt::CrossCursor);
        break;
    case Tool::Edge:
        setCursor(Qt::PointingHandCursor);
        break;
    }
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

Vec2 CanvasWidget::_ToWorld(QPointF s) const
{
    const auto &v = _ctl.GetModel().view;
    return {(s.x() - v.pan_x) / v.zoom, (s.y() - v.pan_y) / v.zoom};
}

double CanvasWidget::_Tolerance() const { return 3 / _ctl.GetModel().view.zoom; }

void CanvasWidget::ZoomBy(double factor, std::optional<QPointF> anchor)
{
    View          v  = _ctl.GetModel().view;
    const QPointF c  = anchor.value_or(QPointF(width() / 2.0, height() / 2.0));
    const double  wx = (c.x() - v.pan_x) / v.zoom;
    const double  wy = (c.y() - v.pan_y) / v.zoom;
    v.zoom           = std::clamp(v.zoom * factor, 0.25, 3.0);
    v.pan_x          = c.x() - wx * v.zoom;
    v.pan_y          = c.y() - wy * v.zoom;
    _ctl.SetView(v);
    update();
}

void CanvasWidget::ResetView()
{
    _ctl.SetView({});
    update();
}

void CanvasWidget::FitView()
{
    const auto &m = _ctl.GetModel();
    if (m.nodes.empty())
    {
        ResetView();
        return;
    }
    const Rect       r    = ContentBounds(m, QtTextMeasurer{}, _ctl.Reg());
    constexpr double kPad = 60;
    const double     zoom =
        std::clamp(std::min((width() - kPad * 2) / std::max(1.0, r.w), (height() - kPad * 2) / std::max(1.0, r.h)), 0.25, 2.0);
    const Vec2 c = r.Center();
    _ctl.SetView({zoom, width() / 2.0 - c.x * zoom, height() / 2.0 - c.y * zoom});
    update();
}

Rect CanvasWidget::VisibleWorldRect() const
{
    const auto &v = _ctl.GetModel().view;
    return {-v.pan_x / v.zoom, -v.pan_y / v.zoom, width() / v.zoom, height() / v.zoom};
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void CanvasWidget::paintEvent(QPaintEvent * /*e*/)
{
    QPainter    p(this);
    const auto &m = _ctl.GetModel();
    p.fillRect(rect(), ToQColor(Color::Parse(m.scene.background, palette::kCanvasBg)));

    if (m.scene.grid)
    {
        // grid follows the view (world coordinates)
        const double step = std::max(4.0, m.scene.grid_size) * m.view.zoom;
        if (step >= 6)
        {
            const bool light = Color::Parse(m.scene.background, palette::kCanvasBg).Lightness() > 140;
            p.setPen(QPen(light ? QColor(30, 50, 90, 22) : QColor(90, 120, 170, 18), 1));
            for (double x = std::fmod(m.view.pan_x, step); x < width(); x += step)
            {
                p.drawLine(QPointF(x, 0), QPointF(x, height()));
            }
            for (double y = std::fmod(m.view.pan_y, step); y < height(); y += step)
            {
                p.drawLine(QPointF(0, y), QPointF(width(), y));
            }
        }
    }

    p.save();
    p.translate(m.view.pan_x, m.view.pan_y);
    p.scale(m.view.zoom, m.view.zoom);
    SceneOptions opt;
    opt.selection    = _ctl.GetSelection();
    opt.connect_mode = _tool == Tool::Edge;
    if (_connect.has_value())
    {
        opt.connect_from_node = _connect->node;
    }
    RenderFrame(p, BuildFrame(m, _ctl.Time(), opt, QtTextMeasurer{}, _ctl.Reg()));
    p.restore();

    // tool hint in the bottom-left corner
    if (!_hint.isEmpty() && !_ctl.IsPlaying())
    {
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QString      text = fm.elidedText(_hint, Qt::ElideRight, std::max(50, width() - 40));
        const QRectF       box(10, height() - 34, fm.horizontalAdvance(text) + 20, 24);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(ToQColor(Ui().border), 1));
        p.setBrush(ToQColor(Ui().base, 0.86));
        p.drawRoundedRect(box, 7, 7);
        p.setPen(ToQColor(Ui().muted));
        p.drawText(box, Qt::AlignCenter, text);
    }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void CanvasWidget::_BeginDragEdit()
{
    if (_moved)
    {
        return;
    }
    _moved = true;
    _ctl.Doc().Checkpoint(); // one history entry for the whole drag
}

void CanvasWidget::mousePressEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    const Vec2    w   = _ToWorld(pos);
    _press_pos        = pos;
    _moved            = false;

    auto start_pan = [&]
    {
        _drag       = Drag::Pan;
        _pan_origin = QPointF(_ctl.GetModel().view.pan_x, _ctl.GetModel().view.pan_y);
    };

    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && _ctl.IsPlaying()))
    {
        start_pan(); // while playing the canvas is view-only
        return;
    }
    if (e->button() != Qt::LeftButton)
    {
        return;
    }
    const Hit hit = HitTest(_ctl.GetModel(), w, _ctl.GetSelection(), _Tolerance(), _ctl.Reg());

    if (_tool == Tool::Node)
    {
        const ElementType &type = _ctl.Reg().Element(_pending_type, &_ctl.GetModel().library);
        Node              &n    = _ctl.Doc().AddNode(w, type);
        n.type                  = _pending_type;
        const std::string id    = n.id;
        _ctl.Changed(true);
        _ctl.Select(Kind::Node, id);
        return;
    }

    if (_tool == Tool::Edge)
    {
        std::string node;
        std::string port;
        if (hit.kind == Hit::Kind::Port)
        {
            node = hit.id;
            port = hit.port_id;
        }
        else if (hit.kind == Hit::Kind::Node)
        {
            node = hit.id;
        }
        if (node.empty())
        {
            _connect.reset();
        }
        else if (!_connect.has_value())
        {
            _connect = ConnectFrom{node, port};
        }
        else if (_connect->node != node)
        {
            if (const Edge *edge = _ctl.Doc().AddEdge(_connect->node, node, _connect->port, port); edge != nullptr)
            {
                const std::string id = edge->id;
                _ctl.Changed(true);
                _ctl.Select(Kind::Edge, id);
            }
            _connect.reset();
        }
        else
        {
            _connect.reset();
        }
        update();
        return;
    }

    switch (hit.kind)
    {
    case Hit::Kind::Port:
        _ctl.Select(Kind::Node, hit.id);
        _drag      = Drag::Port;
        _drag_id   = hit.id;
        _drag_port = hit.port_id;
        break;
    case Hit::Kind::Waypoint:
        _drag          = Drag::Waypoint;
        _drag_id       = hit.id;
        _drag_waypoint = hit.waypoint;
        break;
    case Hit::Kind::Node:
    {
        _ctl.Select(Kind::Node, hit.id);
        const Node *n = _ctl.GetModel().FindNode(hit.id);
        _drag         = Drag::Node;
        _drag_id      = hit.id;
        _grab_offset  = {w.x - n->x, w.y - n->y};
        _UpdateCursor();
        break;
    }
    case Hit::Kind::Edge:
        _ctl.Select(Kind::Edge, hit.id);
        break;
    case Hit::Kind::None:
        _ctl.ClearSelection();
        start_pan();
        break;
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (_drag == Drag::None)
    {
        return;
    }
    const QPointF pos = e->position();
    if (!_moved && (pos - _press_pos).manhattanLength() < 3)
    {
        return; // click jitter
    }
    const Vec2 w = _ToWorld(pos);

    if (_drag == Drag::Pan)
    {
        View v  = _ctl.GetModel().view;
        v.pan_x = _pan_origin.x() + (pos.x() - _press_pos.x());
        v.pan_y = _pan_origin.y() + (pos.y() - _press_pos.y());
        _moved  = true;
        _ctl.SetView(v);
        update();
        return;
    }

    _BeginDragEdit();
    Model &m = _ctl.Doc().Mutable();
    switch (_drag)
    {
    case Drag::Node:
        if (Node *n = m.FindNode(_drag_id); n != nullptr)
        {
            n->x = std::round(w.x - _grab_offset.x);
            n->y = std::round(w.y - _grab_offset.y);
        }
        break;
    case Drag::Port:
        if (Node *n = m.FindNode(_drag_id); n != nullptr)
        {
            if (Port *p = n->FindPort(_drag_port); p != nullptr)
            {
                p->dx = std::round(w.x - n->x);
                p->dy = std::round(w.y - n->y);
            }
        }
        break;
    case Drag::Waypoint:
        if (Edge *ed = m.FindEdge(_drag_id); ed != nullptr && _drag_waypoint < ed->waypoints.size())
        {
            ed->waypoints[_drag_waypoint] = {std::round(w.x), std::round(w.y)};
        }
        break;
    default:
        break;
    }
    _ctl.Changed(false);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent * /*e*/)
{
    if (_moved && _drag != Drag::Pan && _drag != Drag::None)
    {
        _ctl.Doc().BreakMerge();
    }
    _drag  = Drag::None;
    _moved = false;
    _UpdateCursor();
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || _tool != Tool::Select || _ctl.IsPlaying())
    {
        return;
    }
    const Vec2 w   = _ToWorld(e->position());
    const Hit  hit = HitTest(_ctl.GetModel(), w, _ctl.GetSelection(), _Tolerance(), _ctl.Reg());
    auto      &doc = _ctl.Doc();
    switch (hit.kind)
    {
    case Hit::Kind::Waypoint:
        doc.RemoveWaypoint(hit.id, hit.waypoint);
        _ctl.Changed(true);
        break;
    case Hit::Kind::Edge:
    {
        const Edge *ed = _ctl.GetModel().FindEdge(hit.id);
        doc.AddWaypoint(hit.id, w, NearestSegmentIndex(_ctl.GetModel(), *ed, w, _ctl.Reg()));
        _ctl.Changed(true);
        _ctl.Select(Kind::Edge, hit.id);
        break;
    }
    case Hit::Kind::Node:
    {
        _drag                  = Drag::None;
        const std::string id   = hit.id;
        const Node       *n    = _ctl.GetModel().FindNode(id);
        bool              ok   = false;
        const QString     old  = n != nullptr ? Qs(n->label) : QString();
        const QString     name = QInputDialog::getText(this, tr("Узел"), tr("Название узла:"), QLineEdit::Normal, old, &ok);
        // the node may be gone after the dialog (undo, an MCP agent)
        if (ok && _ctl.GetModel().FindNode(id) != nullptr)
        {
            _ctl.Edit({},
                      [&](Model &mm)
                      {
                          if (Node *node = mm.FindNode(id); node != nullptr)
                          {
                              node->label = Us(name);
                          }
                      });
        }
        break;
    }
    default:
        break;
    }
}

void CanvasWidget::wheelEvent(QWheelEvent *e)
{
    const double dy = e->angleDelta().y();
    if (dy == 0)
    {
        return;
    }
    ZoomBy(dy > 0 ? 1.1 : 1 / 1.1, e->position());
    e->accept();
}

void CanvasWidget::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kElementMime)) || e->mimeData()->hasUrls())
    {
        e->acceptProposedAction();
    }
}

void CanvasWidget::dragMoveEvent(QDragMoveEvent *e) { e->acceptProposedAction(); }

void CanvasWidget::dropEvent(QDropEvent *e)
{
    const QMimeData *mime = e->mimeData();
    if (mime->hasFormat(QString::fromLatin1(kElementMime)))
    {
        const std::string  id   = mime->data(QString::fromLatin1(kElementMime)).toStdString();
        const ElementType &type = _ctl.Reg().Element(id, &_ctl.GetModel().library);
        Node              &n    = _ctl.Doc().AddNode(_ToWorld(e->position()), type);
        n.type                  = id;
        const std::string nid   = n.id;
        _ctl.Changed(true);
        _ctl.Select(Kind::Node, nid);
        e->acceptProposedAction();
        return;
    }
    QStringList files;
    for (const QUrl &url : mime->urls())
    {
        if (url.isLocalFile())
        {
            files << url.toLocalFile();
        }
    }
    if (!files.isEmpty())
    {
        Q_EMIT FilesDropped(files);
        e->acceptProposedAction();
    }
}

} // namespace ad::ui
