#ifndef _ENGINE_SHAPES_HPP_
#define _ENGINE_SHAPES_HPP_

#include <string>
#include <string_view>

#include "Geometry/Path.hpp"

/// @file Shapes.hpp
/// @brief Outlines of node shapes (see Shapes() in Catalog.hpp).

namespace ad
{

/// Default corner radius of a shape (used when the style does not set one).
double DefaultCornerRadius(std::string_view shape);

/// Closed outline of `shape` inside `r`. For "custom" the SVG path data in the unit
/// square is mapped into `r`; invalid data falls back to a rounded rectangle.
Path ShapeOutline(std::string_view shape, const Rect &r, double corner_radius, const std::string &custom_path = {});

/// Inner decoration lines (cylinder rim, queue slots, note fold); empty for most shapes.
Path ShapeDecoration(std::string_view shape, const Rect &r);

/// Point where the ray from the center towards `toward` leaves the shape
/// (expanded by `gap`). Ellipse-like and diamond shapes are exact, others use the box.
Vec2 ShapeBorderPoint(std::string_view shape, const Rect &r, Vec2 toward, double gap);

/// Shapes whose text is centered (no icon column, no accent stripe).
bool IsCenteredShape(std::string_view shape);

} // namespace ad

#endif // _ENGINE_SHAPES_HPP_
