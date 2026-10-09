#ifndef _UI_TIMELINE_WIDGET_HPP_
#define _UI_TIMELINE_WIDGET_HPP_

#include <QAbstractScrollArea>

#include <map>
#include <optional>
#include <string>

/// @file TimelineWidget.hpp
/// @brief Video-editor-like scenario timeline: ruler, lanes, playhead; bars can be moved and resized.
///        Markers (chapters) sit on the ruler: drag to move, double-click to rename, context menu
///        to add / delete.

namespace ad
{
struct Marker;
} // namespace ad

namespace ad::ui
{

class Controller;

class TimelineWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TimelineWidget(Controller &ctl, QWidget *parent = nullptr);

    /// Add a marker at `time` (snapped to whole milliseconds); false when one is already there.
    bool AddMarkerAt(double time);
    /// Ask for a new label of the marker.
    void RenameMarker(const std::string &marker_id);

Q_SIGNALS:
    void StatusMessage(const QString &text);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    static constexpr int kRuler  = 26;
    static constexpr int kRowH   = 28;
    static constexpr int kTopPad = 6;
    static constexpr int kHandle = 8;

    enum class Mode
    {
        Scrub,
        Move,
        ResizeL,
        ResizeR,
        Marker
    };

    struct DragState
    {
        Mode        mode = Mode::Scrub;
        std::string step_id; // or the marker id (Mode::Marker)
        double      press_x       = 0;
        double      orig_start    = 0;
        double      orig_duration = 0;
        bool        edited        = false;
    };

    [[nodiscard]] double _TimeToX(double ms) const { return ms / 1000.0 * _px_per_sec; }
    [[nodiscard]] double _XToTime(double x) const { return x / _px_per_sec * 1000.0; }
    [[nodiscard]] int    _ContentWidth() const;
    [[nodiscard]] int    _ContentHeight() const;
    /// Bar rectangle in viewport coordinates.
    [[nodiscard]] QRectF _BarRect(const std::string &step_id) const;
    /// Marker flag on the ruler in viewport coordinates.
    [[nodiscard]] QRectF _MarkerRect(const ad::Marker &m) const;
    /// Topmost marker whose flag contains `pos` (empty when none).
    [[nodiscard]] std::string _MarkerAt(QPointF pos) const;
    [[nodiscard]] QString     _MarkerText(const ad::Marker &m) const;
    /// Width available for the flag of `m` (up to the next marker).
    [[nodiscard]] double _MarkerRoom(const ad::Marker &m) const;
    void                 _PaintMarkers(QPainter &p, int h);
    void                 _Relayout();
    void                 _UpdateScrollRange();
    void                 _FollowPlayhead();

    Controller                &_ctl;
    double                     _px_per_sec = 90;
    std::map<std::string, int> _lanes;
    int                        _lane_count = 0;
    std::optional<DragState>   _drag;
    std::string                _hover_marker;
};

} // namespace ad::ui

#endif // _UI_TIMELINE_WIDGET_HPP_
