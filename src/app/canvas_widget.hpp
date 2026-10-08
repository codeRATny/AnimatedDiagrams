#pragma once
// Холст диаграммы: отрисовка кадра, инструменты (выбор/узел/связь), перетаскивание, зум и панорама.

#include <QWidget>

#include <optional>
#include <string>

#include "ad/geometry.hpp"

namespace app {

class Controller;

class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    enum class Tool { Select, Node, Edge };

    explicit CanvasWidget(Controller& ctl, QWidget* parent = nullptr);

    void setTool(Tool tool);
    [[nodiscard]] Tool tool() const { return tool_; }
    void setPendingKind(const std::string& kind) { pendingKind_ = kind; }

    void zoomBy(double factor, std::optional<QPointF> anchor = std::nullopt);
    void resetView();
    void fitView();
    /// Видимая область в мировых координатах (для экспорта «как на экране»).
    [[nodiscard]] ad::Rect visibleWorldRect() const;
    void cancelInteraction();

signals:
    void toolChanged(app::CanvasWidget::Tool tool);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    enum class Drag { None, Node, Port, Waypoint, Pan };

    [[nodiscard]] ad::Vec2 toWorld(QPointF screen) const;
    [[nodiscard]] double tolerance() const;
    void updateCursor();
    void updateHint();
    void beginDragEdit();

    Controller& ctl_;
    QString hint_;
    Tool tool_ = Tool::Select;
    std::string pendingKind_ = "service";

    struct ConnectFrom {
        std::string node;
        std::string port;
    };
    std::optional<ConnectFrom> connect_;

    Drag drag_ = Drag::None;
    std::string dragId_;
    std::string dragPort_;
    std::size_t dragWaypoint_ = 0;
    ad::Vec2 grabOffset_;
    QPointF pressPos_;
    QPointF panOrigin_;
    bool moved_ = false;
};

}  // namespace app
