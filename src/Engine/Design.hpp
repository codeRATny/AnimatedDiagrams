#ifndef _ENGINE_DESIGN_HPP_
#define _ENGINE_DESIGN_HPP_

#include <string>

#include "Model/Model.hpp"

/// @file Design.hpp
/// @brief Applying design systems to documents and capturing a document look as one.

namespace ad
{

/// Make `ds` the design system of the document and copy its canvas tokens
/// (background, edge / text colors, grid) into the scene settings.
void ApplyDesignSystem(Model &m, const DesignSystem &ds);

/// Detach the design system; the scene settings keep their current values.
void ClearDesignSystem(Model &m);

/// New design system from the current scene settings, based on `base` (may be null).
DesignSystem DesignFromScene(const Model &m, const DesignSystem *base, std::string id, std::string label);

} // namespace ad

#endif // _ENGINE_DESIGN_HPP_
