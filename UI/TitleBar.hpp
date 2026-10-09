#ifndef _UI_TITLE_BAR_HPP_
#define _UI_TITLE_BAR_HPP_

#include <QAbstractButton>
#include <QPoint>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QMenuBar;

/// @file TitleBar.hpp
/// @brief Window title bar in the application theme: icon, menu bar, title and the
///        minimize / maximize / close buttons (the window frame itself is hidden).
///        With the system frame (setting) only the menu bar row is shown.

namespace ad::ui
{

/// Minimize / maximize / restore / close button drawn with the theme colors.
class WindowButton : public QAbstractButton
{
    Q_OBJECT

public:
    enum class Kind
    {
        Minimize,
        Maximize,
        Restore,
        Close
    };

    explicit WindowButton(Kind kind, QWidget *parent = nullptr);
    void SetKind(Kind kind);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    Kind _kind;
    bool _hover = false;
};

class TitleBar : public QWidget
{
    Q_OBJECT

public:
    /// `window` -- the top-level window; `menu` is placed into the bar (ownership is taken).
    TitleBar(QWidget *window, QMenuBar *menu);

    /// Own frame (title, window buttons, dragging) or the system frame (menu bar only).
    void               SetCustomFrame(bool on);
    [[nodiscard]] bool CustomFrame() const { return _custom; }

    [[nodiscard]] QSize sizeHint() const override;

protected:
    bool eventFilter(QObject *watched, QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    void                  _ToggleMaximized();
    void                  _UpdateState();
    [[nodiscard]] QString _Title() const;

    QWidget      *_window;
    QMenuBar     *_menu;
    QLabel       *_icon     = nullptr;
    WindowButton *_minimize = nullptr;
    WindowButton *_maximize = nullptr;
    WindowButton *_close    = nullptr;
    bool          _custom   = true;
    bool          _pressed  = false;
    QPoint        _press_pos;
    bool          _dragging = false; // manual move (no system move support, e.g. no window manager)
    QPoint        _drag_from;        // global position minus window position
};

/// Windows: color the system title bars (dialogs, the main window with the system frame)
/// with the theme via DWM; follows theme changes. No-op elsewhere.
void InstallNativeFrameTheming();

} // namespace ad::ui

#endif // _UI_TITLE_BAR_HPP_
