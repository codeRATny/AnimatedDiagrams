#ifndef _ENGINE_SCENE_HPP_
#define _ENGINE_SCENE_HPP_

#include <string>

#include "Engine/DisplayList.hpp"
#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file Scene.hpp
/// @brief Builds the display list of a frame at a moment of time.

namespace ad
{

struct SceneOptions
{
    Selection   selection;
    std::string connect_from_node;     // edge creation source (highlighted)
    bool        connect_mode  = false; // "edge" tool: larger ports
    bool        editor_chrome = true;  // waypoint handles and selection outlines
};

namespace palette
{
inline constexpr Color kPanelBg   = Color::Rgb(0x0b1220);
inline constexpr Color kCanvasBg  = Color::Rgb(0x0a111f);
inline constexpr Color kEdgeLabel = Color::Rgb(0x9fb3d6);
} // namespace palette

Frame BuildFrame(const Model &m, double t, const SceneOptions &opt, const TextMeasurer &tm, const Registry &reg = Registry::Default());

/// Bounding box of the content (nodes, waypoints, notes, badges) without padding.
Rect ContentBounds(const Model &m, const TextMeasurer &tm, const Registry &reg = Registry::Default());

} // namespace ad

#endif // _ENGINE_SCENE_HPP_
