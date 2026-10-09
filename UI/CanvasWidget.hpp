#ifndef _UI_CANVAS_WIDGET_HPP_
#define _UI_CANVAS_WIDGET_HPP_

#include <QString>
#include <QWidget>

#include <optional>
#include <string>

#include "Geometry/Geometry.hpp"

/// @file CanvasWidget.hpp
/// @brief Diagram canvas: frame rendering, tools (select / node / edge), dragging, zoom and pan.

namespace ad::ui
{

class Controller;

class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Tool
    {
        Select,
        Node,
        Edge
    };

    explicit CanvasWidget(Controller &ctl, QWidget *parent = nullptr);

    void               SetTool(Tool tool);
    [[nodiscard]] Tool GetTool() const { return _tool; }
    /// Element type placed by the "node" tool.
    void                             SetPendingType(const std::string &type) { _pending_type = type; }
    [[nodiscard]] const std::string &PendingType() const { return _pending_type; }

    void ZoomBy(double factor, std::optional<QPointF> anchor = std::nullopt);
    void ResetView();
    void FitView();
    /// Visible area in world coordinates (for "as on screen" export).
    [[nodiscard]] Rect VisibleWorldRect() const;
    void               CancelInteraction();

    [[nodiscard]] Controller &GetController() const { return _ctl; }

Q_SIGNALS:
    void ToolChanged(ad::ui::CanvasWidget::Tool tool);
    /// Files dropped on the canvas (opened by the main window).
    void FilesDropped(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    enum class Drag
    {
        None,
        Node,
        Port,
        Waypoint,
        Pan
    };

    struct ConnectFrom
    {
        std::string node;
        std::string port;
    };

    [[nodiscard]] Vec2   _ToWorld(QPointF screen) const;
    [[nodiscard]] double _Tolerance() const;
    void                 _UpdateCursor();
    void                 _UpdateHint();
    void                 _BeginDragEdit();

    Controller                &_ctl;
    QString                    _hint;
    Tool                       _tool         = Tool::Select;
    std::string                _pending_type = "service";
    std::optional<ConnectFrom> _connect;
    Drag                       _drag = Drag::None;
    std::string                _drag_id;
    std::string                _drag_port;
    size_t                     _drag_waypoint = 0;
    Vec2                       _grab_offset;
    QPointF                    _press_pos;
    QPointF                    _pan_origin;
    bool                       _moved = false;
};

} // namespace ad::ui

#endif // _UI_CANVAS_WIDGET_HPP_
