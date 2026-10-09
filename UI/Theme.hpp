#ifndef _UI_THEME_HPP_
#define _UI_THEME_HPP_

#include <QColor>
#include <QObject>

#include <optional>

#include "Engine/Design.hpp"

/// @file Theme.hpp
/// @brief Application theme (Fusion palette + style sheet) derived from a design
///        system: the editor chrome follows the look of the active document.

namespace ad::ui
{

class Theme : public QObject
{
    Q_OBJECT

public:
    static Theme &Instance();

    /// Use the colors of `ds` (null -- the default dark look) for the whole application.
    /// Cheap when the resulting colors do not change.
    void Apply(const DesignSystem *ds);

    [[nodiscard]] const UiPalette &Colors() const { return _colors; }
    /// The applied design system (null -- default); resolves "$token" colors in editors.
    [[nodiscard]] const DesignSystem *Design() const { return _design.has_value() ? &*_design : nullptr; }

Q_SIGNALS:
    /// The colors changed: custom-painted widgets repaint, cached colors are refreshed.
    void Changed();

private:
    Theme() = default;

    UiPalette                   _colors;
    std::optional<DesignSystem> _design;
    bool                        _applied = false;
};

/// Current UI colors (shortcut for Theme::Instance().Colors()).
[[nodiscard]] inline const UiPalette &Ui() { return Theme::Instance().Colors(); }

/// Fusion style and the default theme.
void ApplyTheme();

} // namespace ad::ui

#endif // _UI_THEME_HPP_
