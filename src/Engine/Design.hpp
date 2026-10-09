#ifndef _ENGINE_DESIGN_HPP_
#define _ENGINE_DESIGN_HPP_

#include <string>

#include "Model/Model.hpp"

/// @file Design.hpp
/// @brief Applying design systems to documents, capturing a document look as one and
///        deriving the application UI colors from a design system.

namespace ad
{

/// Make `ds` the design system of the document and copy its canvas tokens
/// (background, edge / text colors, grid) into the scene settings.
void ApplyDesignSystem(Model &m, const DesignSystem &ds);

/// Detach the design system; the scene settings keep their current values.
void ClearDesignSystem(Model &m);

/// New design system from the current scene settings, based on `base` (may be null).
DesignSystem DesignFromScene(const Model &m, const DesignSystem *base, std::string id, std::string label);

/// Application UI colors derived from a design system (the editor chrome follows the
/// look of the document).
struct UiPalette
{
    Color base;        // input fields, lists, canvas-like areas
    Color window;      // panels and dialogs
    Color panel;       // buttons, tab bars, headers
    Color hover;       // hovered buttons / items
    Color border;      // frames and separators
    Color text;        // main text
    Color muted;       // secondary text (labels, hints)
    Color faint;       // disabled text, empty-state hints
    Color accent;      // focus, selection, links
    Color accent_text; // text on the accent color
    Color selected;    // checked buttons, selected tabs
    Color danger;
    Color warning;
    Color success;
    bool  light = false; // light background

    friend bool operator==(const UiPalette &, const UiPalette &) = default;
};

/// UI colors for `ds` (null -- the default dark look). Text colors keep a readable
/// contrast with the backgrounds whatever the design system tokens are.
UiPalette DeriveUiPalette(const DesignSystem *ds);

/// WCAG contrast ratio of two colors (1 .. 21).
double ContrastRatio(Color a, Color b);

} // namespace ad

#endif // _ENGINE_DESIGN_HPP_
