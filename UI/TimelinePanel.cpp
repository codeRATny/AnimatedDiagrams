#include "TimelinePanel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

#include "Controller.hpp"
#include "QtRender.hpp"
#include "Timeline/TimelineLayout.hpp"
#include "TimelineWidget.hpp"

namespace ad::ui
{

TimelinePanel::TimelinePanel(Controller &ctl, QWidget *parent) : QWidget(parent), _ctl(ctl)
{
    auto *bar = new QWidget;
    auto *l   = new QHBoxLayout(bar);
    l->setContentsMargins(8, 6, 8, 6);
    l->setSpacing(6);

    auto mk = [&](QStyle::StandardPixmap icon, const QString &tip, const std::function<void()> &slot)
    {
        auto *b = new QPushButton(style()->standardIcon(icon), QString());
        b->setToolTip(tip);
        b->setFixedWidth(40);
        connect(b, &QPushButton::clicked, this, slot);
        l->addWidget(b);
        return b;
    };
    mk(QStyle::SP_MediaSkipBackward, tr("Go to start (Home)"),
       [this]
       {
           _ctl.Seek(0);
       });
    _play_btn = mk(QStyle::SP_MediaPlay, tr("Play / Pause (Space)"),
                   [this]
                   {
                       _ctl.TogglePlay();
                   });
    _play_btn->setObjectName(QStringLiteral("primaryButton"));
    mk(QStyle::SP_MediaStop, tr("Stop"),
       [this]
       {
           _ctl.Stop();
       });
    mk(QStyle::SP_MediaSkipForward, tr("Go to end (End)"),
       [this]
       {
           _ctl.Seek(_ctl.Duration());
       });

    _readout = new QLabel;
    _readout->setObjectName(QStringLiteral("readout"));
    l->addWidget(_readout);

    l->addWidget(new QLabel(tr("Speed")));
    _speed = new QComboBox;
    for (const double s : {0.25, 0.5, 1.0, 2.0, 4.0})
    {
        _speed->addItem(QStringLiteral("%1×").arg(s), s);
    }
    _speed->setCurrentIndex(2);
    connect(_speed, &QComboBox::currentIndexChanged, this,
            [this]
            {
                _ctl.SetSpeed(_speed->currentData().toDouble());
            });
    l->addWidget(_speed);

    l->addWidget(new QLabel(tr("Duration, s")));
    _duration = new QDoubleSpinBox;
    _duration->setRange(1, 3600);
    _duration->setDecimals(1);
    _duration->setSingleStep(0.5);
    _duration->setKeyboardTracking(false);
    connect(_duration, &QDoubleSpinBox::valueChanged, this,
            [this](double v)
            {
                if (std::abs((v * 1000) - _ctl.Duration()) < 1)
                {
                    return;
                }
                _ctl.Doc().SetDuration(v * 1000);
                _ctl.Changed(false);
            });
    l->addWidget(_duration);

    _loop = new QCheckBox(tr("Loop"));
    _loop->setChecked(_ctl.Loop());
    connect(_loop, &QCheckBox::toggled, this,
            [this](bool v)
            {
                _ctl.SetLoop(v);
            });
    l->addWidget(_loop);

    l->addStretch(1);
    _step_type = new QComboBox;
    for (const auto &t : StepTypes())
    {
        _step_type->addItem(Qs(t.label), static_cast<int>(t.type));
    }
    l->addWidget(_step_type);
    auto *add = new QPushButton(tr("＋ Add step"));
    add->setObjectName(QStringLiteral("primaryButton"));
    connect(add, &QPushButton::clicked, this, &TimelinePanel::AddStep);
    l->addWidget(add);
    auto *anim = new QPushButton(tr("▶ Animation…"));
    anim->setToolTip(tr("Insert a ready-made animation (template from the library)"));
    connect(anim, &QPushButton::clicked, this, &TimelinePanel::AnimationRequested);
    l->addWidget(anim);

    _timeline    = new TimelineWidget(_ctl);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(_timeline, 1);

    connect(&_ctl, &Controller::TimeChanged, this, &TimelinePanel::_Update);
    connect(&_ctl, &Controller::ModelChanged, this, &TimelinePanel::_Update);
    connect(&_ctl, &Controller::PlayingChanged, this, &TimelinePanel::_Update);
    _Update();
}

void TimelinePanel::_Update()
{
    _readout->setText(Qs(FormatTime(_ctl.Time())) + QStringLiteral(" / ") + Qs(FormatTime(_ctl.Duration())));
    _play_btn->setIcon(style()->standardIcon(_ctl.IsPlaying() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    if (!_duration->hasFocus())
    {
        const QSignalBlocker block(_duration);
        _duration->setValue(_ctl.Duration() / 1000);
    }
}

void TimelinePanel::AddStep()
{
    const auto type = static_cast<StepType>(_step_type->currentData().toInt());
    auto       step = _ctl.Doc().MakeDefaultStep(type, _ctl.Time());
    if (!step.has_value())
    {
        Q_EMIT StatusMessage(type == StepType::Link ? tr("Add an edge between nodes first") : tr("Add nodes to the diagram first"));
        return;
    }
    const std::string id = _ctl.Doc().AddStep(std::move(*step)).id;
    _ctl.Changed(true);
    _ctl.Select(Selection::Kind::Step, id);
}

} // namespace ad::ui
