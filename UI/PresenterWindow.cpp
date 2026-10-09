#include "PresenterWindow.hpp"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWheelEvent>

#include <algorithm>

#include "Controller.hpp"
#include "Engine/Scene.hpp"
#include "Export/ExportPlan.hpp"
#include "Model/Markers.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

constexpr int kIndicatorHoldMs = 1800;
constexpr int kIndicatorFadeMs = 700;
constexpr int kCursorHideMs    = 1500;

} // namespace

PresenterWindow::PresenterWindow(Controller &ctl, QWidget *parent) : QWidget(parent, Qt::Window), _ctl(&ctl)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setWindowTitle(tr("Presenting: %1").arg(Qs(ctl.GetModel().meta.name)));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    _indicator_hold.setSingleShot(true);
    _indicator_hold.setInterval(kIndicatorHoldMs);
    connect(&_indicator_hold, &QTimer::timeout, this,
            [this]
            {
                _indicator_fade.stop();
                _indicator_fade.setStartValue(_indicator_opacity);
                _indicator_fade.setEndValue(0.0);
                _indicator_fade.start();
            });
    _indicator_fade.setDuration(kIndicatorFadeMs);
    connect(&_indicator_fade, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v)
            {
                _indicator_opacity = v.toDouble();
                update();
            });

    _cursor_hide.setSingleShot(true);
    _cursor_hide.setInterval(kCursorHideMs);
    connect(&_cursor_hide, &QTimer::timeout, this,
            [this]
            {
                setCursor(Qt::BlankCursor);
            });

    connect(&ctl, &Controller::TimeChanged, this, qOverload<>(&QWidget::update));
    connect(&ctl, &Controller::ModelChanged, this,
            [this]
            {
                _UpdateWorld();
                _segment = std::min(_segment, SegmentCount() - 1);
                update();
            });
    connect(&ctl, &Controller::LibraryChanged, this,
            [this]
            {
                _UpdateWorld();
                update();
            });
    connect(&ctl, &QObject::destroyed, this, &QWidget::close);
    _UpdateWorld();
}

size_t PresenterWindow::SegmentCount() const { return _ctl != nullptr ? ScenarioSegments(_ctl->GetModel()).size() : 1; }

void PresenterWindow::Start(QScreen *screen)
{
    if (screen != nullptr)
    {
        setScreen(screen);
        setGeometry(screen->geometry());
    }
    First();
    showFullScreen();
    raise();
    activateWindow();
    setFocus();
    _cursor_hide.start();
}

void PresenterWindow::_UpdateWorld()
{
    if (_ctl == nullptr)
    {
        return;
    }
    // the same framing as the exports: content bounds + padding
    _world = ContentBounds(_ctl->GetModel(), QtTextMeasurer{}, _ctl->Reg()).Adjusted(kExportPadding);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void PresenterWindow::Next()
{
    if (_ctl == nullptr)
    {
        return;
    }
    const Scenario &sc   = _ctl->GetModel().scenario;
    const auto      stop = NextStop(sc, _ctl->Time());
    if (_ctl->IsPlaying())
    {
        // a click during the animation finishes the current segment at once
        _ctl->Pause();
        _ctl->Seek(stop.value_or(_ctl->Duration()));
    }
    else if (stop.has_value())
    {
        _segment = SegmentIndexAt(ScenarioSegments(sc), _ctl->Time());
        _ctl->PlayUntil(*stop);
    }
    _ShowIndicator(); // at the end: stay there
}

void PresenterWindow::Previous()
{
    if (_ctl == nullptr)
    {
        return;
    }
    _ctl->Pause();
    const Scenario &sc     = _ctl->GetModel().scenario;
    const double    target = PreviousStop(sc, _ctl->Time());
    _ctl->Seek(target);
    _segment = SegmentIndexAt(ScenarioSegments(sc), target);
    _ShowIndicator();
}

void PresenterWindow::First()
{
    if (_ctl == nullptr)
    {
        return;
    }
    _ctl->Pause();
    _ctl->Seek(0);
    _segment = 0;
    _ShowIndicator();
}

void PresenterWindow::Last()
{
    if (_ctl == nullptr)
    {
        return;
    }
    _ctl->Pause();
    _ctl->Seek(_ctl->Duration());
    _segment = SegmentCount() - 1;
    _ShowIndicator();
}

void PresenterWindow::_ShowIndicator()
{
    _indicator_fade.stop();
    _indicator_opacity = 1;
    _indicator_hold.start();
    update();
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void PresenterWindow::paintEvent(QPaintEvent * /*e*/)
{
    QPainter p(this);
    if (_ctl == nullptr)
    {
        p.fillRect(rect(), Qt::black);
        return;
    }
    const Model &m  = _ctl->GetModel();
    const Color  bg = Color::Parse(m.scene.background, palette::kCanvasBg);
    p.fillRect(rect(), ToQColor(bg));

    if (_world.w > 0 && _world.h > 0)
    {
        p.save();
        const double k = std::min(width() / _world.w, height() / _world.h);
        p.translate((width() - (_world.w * k)) / 2, (height() - (_world.h * k)) / 2);
        p.scale(k, k);
        p.translate(-_world.x, -_world.y);
        SceneOptions opt;
        opt.editor_chrome = false;
        RenderFrame(p, BuildFrame(m, _ctl->Time(), opt, QtTextMeasurer{}, _ctl->Reg()));
        p.restore();
    }

    if (_indicator_opacity <= 0.01)
    {
        return;
    }
    // "n / m  label" in the bottom-right corner
    const auto     segments = ScenarioSegments(m);
    const size_t   index    = std::min(_segment, segments.size() - 1);
    const Segment &seg      = segments[index];
    QString        text     = QStringLiteral("%1 / %2").arg(index + 1).arg(segments.size());
    if (!seg.label.empty())
    {
        text += QStringLiteral("  ·  ") + Qs(seg.label);
    }
    QFont f = font();
    f.setPixelSize(std::max(13, height() / 60));
    p.setFont(f);
    const QFontMetrics fm(f);
    text               = fm.elidedText(text, Qt::ElideRight, width() / 2);
    const double pad   = f.pixelSize() * 0.8;
    const double bar_h = segments.size() > 1 ? 6.0 : 0.0; // room for the progress line
    const double box_w = fm.horizontalAdvance(text) + (2 * pad);
    const double box_h = fm.height() + pad + bar_h;
    const QRectF box(width() - box_w - (pad * 2), height() - box_h - (pad * 2), box_w, box_h);
    const bool   light   = bg.Lightness() > 140;
    const double opacity = _indicator_opacity;
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(light ? QColor(255, 255, 255, static_cast<int>(170 * opacity)) : QColor(0, 0, 0, static_cast<int>(130 * opacity)));
    p.drawRoundedRect(box, 8, 8);
    // progress line along the bottom of the box: done part bright, the rest dim
    if (bar_h > 0)
    {
        const QColor line  = light ? QColor(20, 40, 80) : QColor(255, 255, 255);
        const QRectF track = QRectF(box.left() + pad, box.bottom() - (bar_h + pad / 2) + 2, box.width() - (2 * pad), 2);
        const double frac  = static_cast<double>(index + 1) / static_cast<double>(segments.size());
        QColor       dim   = line;
        dim.setAlphaF(static_cast<float>(0.2 * opacity));
        QColor bright = line;
        bright.setAlphaF(static_cast<float>(0.75 * opacity));
        p.setBrush(dim);
        p.drawRoundedRect(track, 1, 1);
        p.setBrush(bright);
        p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * frac, track.height()), 1, 1);
    }
    p.setPen(light ? QColor(20, 30, 50, static_cast<int>(220 * opacity)) : QColor(240, 245, 255, static_cast<int>(220 * opacity)));
    p.drawText(box.adjusted(0, 0, 0, -bar_h), Qt::AlignCenter, text);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void PresenterWindow::keyPressEvent(QKeyEvent *e)
{
    switch (e->key())
    {
    case Qt::Key_Space:
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_N:
        Next();
        break;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
    case Qt::Key_P:
        Previous();
        break;
    case Qt::Key_Home:
        First();
        break;
    case Qt::Key_End:
        Last();
        break;
    case Qt::Key_Escape:
    case Qt::Key_F5:
        close();
        break;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}

void PresenterWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        Next();
    }
    else if (e->button() == Qt::BackButton)
    {
        Previous();
    }
    else if (e->button() == Qt::ForwardButton)
    {
        Next();
    }
}

void PresenterWindow::mouseMoveEvent(QMouseEvent * /*e*/)
{
    unsetCursor();
    _cursor_hide.start();
}

void PresenterWindow::wheelEvent(QWheelEvent *e)
{
    // one step per wheel notch (touchpads send many small deltas)
    static constexpr int kNotch = 120;
    _wheel_delta += e->angleDelta().y();
    if (_wheel_delta <= -kNotch)
    {
        _wheel_delta = 0;
        Next();
    }
    else if (_wheel_delta >= kNotch)
    {
        _wheel_delta = 0;
        Previous();
    }
    e->accept();
}

void PresenterWindow::closeEvent(QCloseEvent *e)
{
    if (_ctl != nullptr)
    {
        _ctl->Pause();
    }
    Q_EMIT Closed();
    e->accept();
}

} // namespace ad::ui
