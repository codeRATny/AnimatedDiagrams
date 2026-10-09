#include "TitleBar.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h first
#include <dwmapi.h>
#endif

#include "QtRender.hpp"
#include "Theme.hpp"

namespace ad::ui
{

namespace
{

constexpr int kBarHeight    = 34;
constexpr int kButtonWidth  = 46;
constexpr int kDragDistance = 4;

} // namespace

// ---------------------------------------------------------------------------
// WindowButton
// ---------------------------------------------------------------------------

WindowButton::WindowButton(Kind kind, QWidget *parent) : QAbstractButton(parent), _kind(kind)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    SetKind(kind);
}

void WindowButton::SetKind(Kind kind)
{
    _kind = kind;
    switch (kind)
    {
    case Kind::Minimize:
        setToolTip(tr("Minimize"));
        break;
    case Kind::Maximize:
        setToolTip(tr("Maximize"));
        break;
    case Kind::Restore:
        setToolTip(tr("Restore"));
        break;
    case Kind::Close:
        setToolTip(tr("Close"));
        break;
    }
    update();
}

QSize WindowButton::sizeHint() const { return {kButtonWidth, kBarHeight}; }

void WindowButton::enterEvent(QEnterEvent *e)
{
    _hover = true;
    update();
    QAbstractButton::enterEvent(e);
}

void WindowButton::leaveEvent(QEvent *e)
{
    _hover = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void WindowButton::paintEvent(QPaintEvent * /*e*/)
{
    const UiPalette &u = Ui();
    QPainter         p(this);
    const bool       close = _kind == Kind::Close;
    if (_hover || isDown())
    {
        const Color bg = close ? Color::Rgb(0xe81123) : u.hover;
        p.fillRect(rect(), ToQColor(isDown() && !close ? u.selected : bg, isDown() && close ? 0.8 : 1.0));
    }
    const bool   active = window()->isActiveWindow();
    const QColor fg     = _hover && close ? QColor(Qt::white) : ToQColor(active ? u.text : u.muted);
    p.setRenderHint(QPainter::Antialiasing, close);
    p.setPen(QPen(fg, 1.0));
    p.setBrush(Qt::NoBrush);
    // 10x10 glyph centered (pixel aligned for crisp 1px lines)
    const QPointF c(std::floor(width() / 2.0) + 0.5, std::floor(height() / 2.0) + 0.5);
    switch (_kind)
    {
    case Kind::Minimize:
        p.drawLine(QPointF(c.x() - 5, c.y()), QPointF(c.x() + 5, c.y()));
        break;
    case Kind::Maximize:
        p.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10));
        break;
    case Kind::Restore:
        p.drawRect(QRectF(c.x() - 5, c.y() - 3, 8, 8));
        p.drawPolyline(QPolygonF({QPointF(c.x() - 3, c.y() - 3), QPointF(c.x() - 3, c.y() - 5), QPointF(c.x() + 5, c.y() - 5),
                                  QPointF(c.x() + 5, c.y() + 3), QPointF(c.x() + 3, c.y() + 3)}));
        break;
    case Kind::Close:
        p.drawLine(QPointF(c.x() - 5, c.y() - 5), QPointF(c.x() + 5, c.y() + 5));
        p.drawLine(QPointF(c.x() + 5, c.y() - 5), QPointF(c.x() - 5, c.y() + 5));
        break;
    }
}

// ---------------------------------------------------------------------------
// TitleBar
// ---------------------------------------------------------------------------

TitleBar::TitleBar(QWidget *window, QMenuBar *menu) : QWidget(window), _window(window), _menu(menu)
{
    setObjectName(QStringLiteral("titleBar"));
    setAttribute(Qt::WA_StyledBackground, false);

    _icon = new QLabel;
    _icon->setFixedSize(30, kBarHeight);
    _icon->setAlignment(Qt::AlignCenter);
    _icon->setPixmap(window->windowIcon().pixmap(18, 18));

    _menu->setParent(this);
    _menu->setNativeMenuBar(false);
    _menu->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    _minimize = new WindowButton(WindowButton::Kind::Minimize);
    _maximize = new WindowButton(WindowButton::Kind::Maximize);
    _close    = new WindowButton(WindowButton::Kind::Close);
    connect(_minimize, &QAbstractButton::clicked, _window, &QWidget::showMinimized);
    connect(_maximize, &QAbstractButton::clicked, this, &TitleBar::_ToggleMaximized);
    connect(_close, &QAbstractButton::clicked, _window, &QWidget::close);

    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(4, 0, 0, 0);
    l->setSpacing(0);
    l->addWidget(_icon);
    l->addWidget(_menu, 0, Qt::AlignVCenter);
    l->addStretch(1); // the title is painted centered over the whole bar
    l->addWidget(_minimize);
    l->addWidget(_maximize);
    l->addWidget(_close);

    _window->installEventFilter(this);
    connect(&Theme::Instance(), &Theme::Changed, this,
            [this]
            {
                update();
            });
    _UpdateState();
}

QSize TitleBar::sizeHint() const
{
    const QSize menu = _menu->sizeHint();
    return {menu.width() + 200, _custom ? kBarHeight : menu.height()};
}

void TitleBar::SetCustomFrame(bool on)
{
    _custom = on;
    _icon->setVisible(on);
    _minimize->setVisible(on);
    _maximize->setVisible(on);
    _close->setVisible(on);
    setFixedHeight(on ? kBarHeight : _menu->sizeHint().height());
    _UpdateState();
    update();
}

QString TitleBar::_Title() const
{
    QString title = _window->windowTitle();
    title.remove(QStringLiteral("[*]"));
    return _window->isWindowModified() ? QStringLiteral("● ") + title : title;
}

void TitleBar::_UpdateState()
{
    const bool maximized = _window->isMaximized() || _window->isFullScreen();
    _maximize->SetKind(maximized ? WindowButton::Kind::Restore : WindowButton::Kind::Maximize);
    _icon->setPixmap(_window->windowIcon().pixmap(18, 18));
    update();
}

void TitleBar::_ToggleMaximized()
{
    if (_window->isFullScreen())
    {
        _window->showNormal();
    }
    else
    {
        _window->isMaximized() ? _window->showNormal() : _window->showMaximized();
    }
}

bool TitleBar::eventFilter(QObject *watched, QEvent *e)
{
    if (watched == _window)
    {
        switch (e->type())
        {
        case QEvent::WindowTitleChange:
        case QEvent::ModifiedChange:
        case QEvent::WindowIconChange:
        case QEvent::WindowStateChange:
        case QEvent::ActivationChange:
            _UpdateState();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, e);
}

void TitleBar::paintEvent(QPaintEvent * /*e*/)
{
    const UiPalette &u = Ui();
    QPainter         p(this);
    p.fillRect(rect(), ToQColor(u.base));
    p.setPen(ToQColor(u.border));
    p.drawLine(0, height() - 1, width(), height() - 1);
    if (!_custom)
    {
        return;
    }
    // title centered over the whole bar, kept clear of the menu and the buttons
    const int left  = _menu->geometry().right() + 16;
    const int right = _minimize->geometry().left() - 16;
    if (right - left < 40)
    {
        return;
    }
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.95);
    p.setFont(f);
    const QFontMetrics fm(f);
    const QString      text = fm.elidedText(_Title(), Qt::ElideMiddle, right - left);
    const int          w    = fm.horizontalAdvance(text);
    const int          x    = std::clamp((width() - w) / 2, left, right - w);
    p.setPen(ToQColor(_window->isActiveWindow() ? u.text : u.muted));
    p.drawText(QRect(x, 0, w, height()), Qt::AlignVCenter | Qt::AlignLeft, text);
}

void TitleBar::mousePressEvent(QMouseEvent *e)
{
    if (!_custom || e->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(e);
        return;
    }
    // the move starts on the first mouse move: a system move started on press would
    // grab the pointer and swallow the double click
    _pressed   = true;
    _press_pos = e->globalPosition().toPoint();
    _drag_from = _press_pos - _window->frameGeometry().topLeft();
}

void TitleBar::mouseMoveEvent(QMouseEvent *e)
{
    if (!_pressed || (e->buttons() & Qt::LeftButton) == 0)
    {
        QWidget::mouseMoveEvent(e);
        return;
    }
    const QPoint global = e->globalPosition().toPoint();
    if (!_dragging)
    {
        if ((global - _press_pos).manhattanLength() < kDragDistance)
        {
            return;
        }
        // the system move keeps window snapping / tiling of the window manager
        if (QWindow *w = _window->windowHandle(); w != nullptr && w->startSystemMove())
        {
            _pressed = false;
            return;
        }
        _dragging = true; // no system move (e.g. no window manager): move by hand
        if (_window->isMaximized())
        {
            _window->showNormal();
        }
    }
    _window->move(global - _drag_from);
}

void TitleBar::mouseReleaseEvent(QMouseEvent *e)
{
    _pressed  = false;
    _dragging = false;
    QWidget::mouseReleaseEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (_custom && e->button() == Qt::LeftButton)
    {
        _pressed = false;
        _ToggleMaximized();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

// ---------------------------------------------------------------------------
// System frames (dialogs) on Windows
// ---------------------------------------------------------------------------

namespace
{

#ifdef Q_OS_WIN
void ColorNativeFrame(QWidget *w)
{
    if (!w->isWindow() || w->windowHandle() == nullptr || w->testAttribute(Qt::WA_DontShowOnScreen))
    {
        return;
    }
    const UiPalette &u    = Ui();
    auto             hwnd = reinterpret_cast<HWND>(w->winId());
    auto             ref  = [](Color c)
    {
        return static_cast<COLORREF>(RGB(c.r, c.g, c.b));
    };
    const BOOL     dark    = u.light ? FALSE : TRUE;
    const COLORREF caption = ref(u.base);
    const COLORREF text    = ref(u.text);
    const COLORREF border  = ref(u.border);
    // attributes missing on older Windows versions are ignored
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
}

class NativeFrameFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *e) override
    {
        if (e->type() == QEvent::Show && watched->isWidgetType())
        {
            ColorNativeFrame(static_cast<QWidget *>(watched));
        }
        return QObject::eventFilter(watched, e);
    }
};
#endif

} // namespace

void InstallNativeFrameTheming()
{
#ifdef Q_OS_WIN
    static auto *filter = new NativeFrameFilter(qApp);
    qApp->installEventFilter(filter);
    QObject::connect(&Theme::Instance(), &Theme::Changed, filter,
                     []
                     {
                         for (QWidget *w : QApplication::topLevelWidgets())
                         {
                             if (w->isVisible())
                             {
                                 ColorNativeFrame(w);
                             }
                         }
                     });
#endif
}

} // namespace ad::ui
