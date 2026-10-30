#include "CanvasWidget.hpp"

#include <QInputDialog>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
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
    connect(&_ctl, &Controller::ModelReplaced, this, &CanvasWidget::CancelInteraction);
    connect(&_ctl, &Controller::HistoryTraversal, this, &CanvasWidget::CancelInteraction);
    connect(&_ctl, &Controller::SelectionChanged, this, repaint);
    connect(&_ctl, &Controller::TimeChanged, this, repaint);
    connect(&_ctl, &Controller::LibraryChanged, this, repaint);
    connect(&_ctl, &Controller::PlayingChanged, this,
            [this]
            {
                CancelInteraction();
                _UpdateCursor();
            });
    auto shortcut = [this](const QKeySequence &key, const auto &action)
    {
        auto *binding = new QShortcut(key, this);
        binding->setContext(Qt::WidgetShortcut);
        connect(binding, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence::Copy,
             [this]
             {
                 _ctl.CopyNodes();
             });
    shortcut(QKeySequence::Paste,
             [this]
             {
                 _ctl.PasteNodes();
             });
    _UpdateCursor();
    _UpdateHint();
}

void CanvasWidget::SetTool(Tool tool)
{
    CancelInteraction();
    _tool = tool;
    _connect.reset();
    _UpdateCursor();
    _UpdateHint();
    update();
    Q_EMIT ToolChanged(tool);
}

void CanvasWidget::CancelInteraction()
{
    _drag_model.reset();
    _preview_model.reset();
    _connect.reset();
    _drag  = Drag::None;
    _moved = false;
    update();
}

void CanvasWidget::_UpdateHint()
{
    switch (_tool)
    {
    case Tool::Select:
        _hint = tr("Select: click to select, drag nodes to move, drag empty space to select. Alt-drag or middle-drag to pan. "
                   "Double-click a node to rename it, or an edge to add a waypoint.");
        break;
    case Tool::Node:
        _hint = tr("Node: click the canvas to add an element of the selected type.");
        break;
    case Tool::Edge:
        _hint = tr("Edge: click the first node (or a connection point), then the second.");
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
    if (_drag != Drag::None)
    {
        CancelInteraction();
    }
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
    CancelInteraction();
    _ctl.SetView({});
    update();
}

void CanvasWidget::FitView()
{
    CancelInteraction();
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
    const auto &m = _preview_model.has_value() ? *_preview_model : _ctl.GetModel();
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

    if (_drag == Drag::Marquee && _moved)
    {
        p.setPen(QPen(ToQColor(Ui().accent), 1, Qt::DashLine));
        p.setBrush(ToQColor(Ui().accent, 0.12));
        p.drawRect(QRectF(_press_pos, _marquee_end).normalized());
    }

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
    _moved                = true;
    _drag_model           = _ctl.GetModel();
    _preview_model        = *_drag_model;
    const auto &selection = _ctl.GetSelection();
    _drag_ids             = selection.node_ids.empty() ? std::vector<std::string>{_drag_id} : selection.node_ids;
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
        const auto selection = _ctl.GetSelection();
        auto ids = selection.kind == Kind::Node ? (selection.node_ids.empty() ? std::vector<std::string>{selection.id} : selection.node_ids)
                                                : std::vector<std::string>{};
        const bool selected = std::ranges::contains(ids, hit.id);
        if (e->modifiers().testFlag(Qt::ControlModifier) && selected)
        {
            std::erase(ids, hit.id);
            _ctl.SelectNodes(std::move(ids), selection.id);
            break;
        }
        if (e->modifiers().testFlag(Qt::ShiftModifier) || e->modifiers().testFlag(Qt::ControlModifier) || selected)
        {
            if (!selected)
            {
                ids.push_back(hit.id);
            }
            _ctl.SelectNodes(std::move(ids), hit.id);
        }
        else
        {
            _ctl.Select(Kind::Node, hit.id);
        }
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
        if (e->modifiers().testFlag(Qt::AltModifier))
        {
            start_pan();
        }
        else
        {
            _drag              = Drag::Marquee;
            _marquee_end       = pos;
            _marquee_modifiers = e->modifiers();
        }
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

    if (_drag == Drag::Marquee)
    {
        _moved       = true;
        _marquee_end = pos;
        update();
        return;
    }

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
    Model &m = *_preview_model;
    switch (_drag)
    {
    case Drag::Node:
        if (const Node *original = _drag_model->FindNode(_drag_id); original != nullptr)
        {
            const Vec2 delta{std::round(w.x - _grab_offset.x) - original->x, std::round(w.y - _grab_offset.y) - original->y};
            for (const auto &id : _drag_ids)
            {
                if (Node *n = m.FindNode(id); n != nullptr)
                {
                    if (const Node *old = _drag_model->FindNode(id); old != nullptr)
                    {
                        n->x = old->x + delta.x;
                        n->y = old->y + delta.y;
                    }
                }
            }
            for (auto &edge : m.edges)
            {
                if (std::ranges::contains(_drag_ids, edge.from) && std::ranges::contains(_drag_ids, edge.to))
                {
                    if (const Edge *old = _drag_model->FindEdge(edge.id); old != nullptr)
                    {
                        edge.waypoints = old->waypoints;
                        for (auto &point : edge.waypoints)
                        {
                            point = point + delta;
                        }
                    }
                }
            }
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
    update();
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent * /*e*/)
{
    if (_drag == Drag::Marquee)
    {
        auto        selection = _ctl.GetSelection();
        std::string primary   = selection.kind == Kind::Node ? selection.id : std::string{};
        auto ids = selection.kind == Kind::Node ? (selection.node_ids.empty() ? std::vector<std::string>{selection.id} : selection.node_ids)
                                                : std::vector<std::string>{};
        if (!_marquee_modifiers.testFlag(Qt::ShiftModifier) && !_marquee_modifiers.testFlag(Qt::ControlModifier))
        {
            ids.clear();
        }
        if (_moved)
        {
            const Vec2 a = _ToWorld(_press_pos);
            const Vec2 b = _ToWorld(_marquee_end);
            const Rect bounds{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(a.x - b.x), std::abs(a.y - b.y)};
            for (const auto &node : _ctl.GetModel().nodes)
            {
                if (bounds.Contains({node.x, node.y}) && bounds.Contains({node.x + node.w, node.y + node.h}))
                {
                    if (_marquee_modifiers.testFlag(Qt::ControlModifier) && std::ranges::contains(ids, node.id))
                    {
                        std::erase(ids, node.id);
                    }
                    else if (!std::ranges::contains(ids, node.id))
                    {
                        ids.push_back(node.id);
                        primary = node.id;
                    }
                }
            }
        }
        _ctl.SelectNodes(std::move(ids), primary);
    }
    else if (_drag_model.has_value())
    {
        Model result = _ctl.GetModel();
        if (_drag == Drag::Node)
        {
            const Node *old     = _drag_model->FindNode(_drag_id);
            const Node *preview = _preview_model->FindNode(_drag_id);
            if (old != nullptr && preview != nullptr)
            {
                const Vec2 delta{preview->x - old->x, preview->y - old->y};
                for (const auto &id : _drag_ids)
                {
                    if (Node *node = result.FindNode(id); node != nullptr)
                    {
                        node->x += delta.x;
                        node->y += delta.y;
                    }
                }
                for (auto &edge : result.edges)
                {
                    if (std::ranges::contains(_drag_ids, edge.from) && std::ranges::contains(_drag_ids, edge.to) &&
                        _drag_model->FindEdge(edge.id) != nullptr)
                    {
                        for (auto &point : edge.waypoints)
                        {
                            point = point + delta;
                        }
                    }
                }
            }
        }
        else if (_drag == Drag::Port)
        {
            const Port *old     = _drag_model->FindPort(_drag_id, _drag_port);
            const Port *preview = _preview_model->FindPort(_drag_id, _drag_port);
            Node       *node    = result.FindNode(_drag_id);
            Port       *port    = node != nullptr ? node->FindPort(_drag_port) : nullptr;
            if (old != nullptr && preview != nullptr && port != nullptr)
            {
                port->dx += preview->dx - old->dx;
                port->dy += preview->dy - old->dy;
            }
        }
        else if (_drag == Drag::Waypoint)
        {
            const Edge *old     = _drag_model->FindEdge(_drag_id);
            const Edge *preview = _preview_model->FindEdge(_drag_id);
            Edge       *edge    = result.FindEdge(_drag_id);
            if (old != nullptr && preview != nullptr && edge != nullptr && _drag_waypoint < old->waypoints.size() &&
                _drag_waypoint < preview->waypoints.size() && _drag_waypoint < edge->waypoints.size())
            {
                edge->waypoints[_drag_waypoint] =
                    edge->waypoints[_drag_waypoint] + preview->waypoints[_drag_waypoint] - old->waypoints[_drag_waypoint];
            }
        }
        _drag_model.reset();
        _preview_model.reset();
        if (result != _ctl.GetModel())
        {
            _ctl.Doc().Checkpoint();
            _ctl.Doc().Mutable() = std::move(result);
            _ctl.Changed(false);
        }
    }
    _drag  = Drag::None;
    _moved = false;
    _UpdateCursor();
    update();
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
        const QString     name = QInputDialog::getText(this, tr("Node"), tr("Node title:"), QLineEdit::Normal, old, &ok);
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
