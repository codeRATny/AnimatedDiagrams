#ifndef _ENGINE_LAYOUT_HPP_
#define _ENGINE_LAYOUT_HPP_

#include "Model/Model.hpp"

/// @file Layout.hpp
/// @brief Automatic layered layout (useful for diagrams built by agents or imported without positions).

namespace ad
{

enum class LayoutDirection
{
    LeftToRight,
    TopToBottom
};

struct LayoutOptions
{
    LayoutDirection direction = LayoutDirection::LeftToRight;
    double          gap_major = 120; // between layers
    double          gap_minor = 60;  // between nodes of a layer
    Vec2            origin{80, 60};
};

/// Place nodes in layers by longest path from the sources (cycles are broken by
/// visiting order). Edge waypoints are cleared because they no longer fit.
void AutoLayout(Model &m, const LayoutOptions &opt = {});

} // namespace ad

#endif // _ENGINE_LAYOUT_HPP_
