#ifndef _UI_TIMELINE_PANEL_HPP_
#define _UI_TIMELINE_PANEL_HPP_

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

/// @file TimelinePanel.hpp
/// @brief Transport bar (play / pause, time, speed, duration, add step) + timeline of one tab.

namespace ad::ui
{

class Controller;
class TimelineWidget;

class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(Controller &ctl, QWidget *parent = nullptr);

    void AddStep();

Q_SIGNALS:
    void AnimationRequested();
    void StatusMessage(const QString &text);

private:
    void _Update();

    Controller     &_ctl;
    TimelineWidget *_timeline  = nullptr;
    QPushButton    *_play_btn  = nullptr;
    QLabel         *_readout   = nullptr;
    QDoubleSpinBox *_duration  = nullptr;
    QComboBox      *_speed     = nullptr;
    QCheckBox      *_loop      = nullptr;
    QComboBox      *_step_type = nullptr;
};

} // namespace ad::ui

#endif // _UI_TIMELINE_PANEL_HPP_
