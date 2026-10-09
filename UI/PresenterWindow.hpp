#ifndef _UI_PRESENTER_WINDOW_HPP_
#define _UI_PRESENTER_WINDOW_HPP_

#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <cstddef>

#include "Geometry/Geometry.hpp"

class QScreen;

/// @file PresenterWindow.hpp
/// @brief Presenter mode: a full-screen window that shows only the diagram of one document
///        (fitted to the screen, scene background, no editor chrome) and steps through the
///        scenario marker by marker.
///
///        Keys: Space / Right / PageDown / Enter / click -- play to the next marker and stop there;
///        Left / PageUp / Backspace -- back to the previous marker (paused); Home / End; Esc -- exit.
///        Playback goes through the document controller (the editor timeline follows along);
///        the document and its undo history are never modified.

namespace ad::ui
{

class Controller;

class PresenterWindow : public QWidget
{
    Q_OBJECT

public:
    /// Deletes itself when closed; closes when the controller goes away.
    explicit PresenterWindow(Controller &ctl, QWidget *parent = nullptr);

    /// Show full screen on `screen` (null -- the primary one), starting from the beginning.
    void Start(QScreen *screen);

    /// Play the next segment (finish the current one immediately when it is playing).
    void Next();
    /// Jump back to the start of the current / previous segment.
    void Previous();
    void First();
    void Last();
    /// 0-based index of the current segment and the number of segments.
    [[nodiscard]] size_t SegmentIndex() const { return _segment; }
    [[nodiscard]] size_t SegmentCount() const;

Q_SIGNALS:
    void Closed();

protected:
    void paintEvent(QPaintEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    void _UpdateWorld();
    /// Show the "n / m" indicator and fade it out after a while.
    void _ShowIndicator();

    QPointer<Controller> _ctl;
    Rect                 _world; // fitted part of the scene (content bounds + padding)
    size_t               _segment = 0;
    QTimer               _indicator_hold;
    QVariantAnimation    _indicator_fade;
    double               _indicator_opacity = 0;
    QTimer               _cursor_hide;
    int                  _wheel_delta = 0; // accumulated wheel / touchpad scrolling
};

} // namespace ad::ui

#endif // _UI_PRESENTER_WINDOW_HPP_
