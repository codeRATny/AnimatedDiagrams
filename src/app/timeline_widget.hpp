#pragma once
// Таймлайн сценария в стиле видеоредактора: линейка, дорожки, плейхед.

#include <QAbstractScrollArea>

#include <map>
#include <optional>
#include <string>

namespace app {

class Controller;

class TimelineWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit TimelineWidget(Controller& ctl, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    static constexpr int kRuler = 26;
    static constexpr int kRowH = 28;
    static constexpr int kTopPad = 6;
    static constexpr int kHandle = 8;

    [[nodiscard]] double timeToX(double ms) const { return ms / 1000.0 * pxPerSec_; }
    [[nodiscard]] double xToTime(double x) const { return x / pxPerSec_ * 1000.0; }
    [[nodiscard]] int contentWidth() const;
    [[nodiscard]] int contentHeight() const;
    [[nodiscard]] QRectF barRect(const std::string& stepId) const;  // в координатах viewport
    void relayout();
    void updateScrollRange();
    void followPlayhead();

    Controller& ctl_;
    double pxPerSec_ = 90;
    std::map<std::string, int> lanes_;
    int laneCount_ = 0;

    struct DragState {
        enum class Mode { Scrub, Move, ResizeL, ResizeR } mode;
        std::string stepId;
        double pressX = 0;
        double origStart = 0;
        double origDuration = 0;
        bool edited = false;
    };
    std::optional<DragState> drag_;
};

}  // namespace app
