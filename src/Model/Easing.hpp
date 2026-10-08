#ifndef _MODEL_EASING_HPP_
#define _MODEL_EASING_HPP_

#include <optional>
#include <span>
#include <string_view>

/// @file Easing.hpp
/// @brief Easing curves used by message packets, effect keyframes and templates.

namespace ad
{

enum class Easing
{
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    CubicIn,
    CubicOut,
    CubicInOut,
    BackOut,
    ElasticOut,
    BounceOut,
    Step
};

struct EasingInfo
{
    Easing           easing;
    std::string_view id;
    std::string_view label;
};

/// Map t in [0, 1] through the curve (t is clamped). Overshooting curves may leave [0, 1].
double ApplyEasing(Easing e, double t);

std::span<const EasingInfo> Easings();
std::string_view            ToString(Easing e);
std::optional<Easing>       EasingFromString(std::string_view s);

} // namespace ad

#endif // _MODEL_EASING_HPP_
