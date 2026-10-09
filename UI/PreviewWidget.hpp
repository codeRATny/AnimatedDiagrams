#ifndef _UI_PREVIEW_WIDGET_HPP_
#define _UI_PREVIEW_WIDGET_HPP_

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include "Model/Library.hpp"
#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file PreviewWidget.hpp
/// @brief Looping live preview of a library definition (element, effect or animation).

namespace ad::ui
{

class PreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget *parent = nullptr);

    void ShowElement(const ElementType &type, const Registry &reg);
    void ShowEffect(const EffectDef &effect, const Registry &reg);
    void ShowAnimation(const AnimationTemplate &tpl, const Registry &reg);
    void ShowDesignSystem(const DesignSystem &ds, const Registry &reg);
    void Clear();

    /// Scene with one node per role (used by the preview and by tests).
    static Model AnimationScene(const AnimationTemplate &tpl);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    void _SetScene(Model m, const Registry &reg, bool animated);

    Model         _model;
    Registry      _registry;
    bool          _has_scene = false;
    bool          _animated  = false;
    QTimer        _timer;
    QElapsedTimer _clock;
};

} // namespace ad::ui

#endif // _UI_PREVIEW_WIDGET_HPP_
