#include "Easing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace ad
{

namespace
{

constexpr std::array kEasings{
    EasingInfo{Easing::Linear, "linear", "Линейно"},
    EasingInfo{Easing::EaseIn, "ease-in", "Разгон"},
    EasingInfo{Easing::EaseOut, "ease-out", "Торможение"},
    EasingInfo{Easing::EaseInOut, "ease-in-out", "Плавно"},
    EasingInfo{Easing::CubicIn, "cubic-in", "Разгон (сильный)"},
    EasingInfo{Easing::CubicOut, "cubic-out", "Торможение (сильное)"},
    EasingInfo{Easing::CubicInOut, "cubic-in-out", "Плавно (сильно)"},
    EasingInfo{Easing::BackOut, "back-out", "С перелётом"},
    EasingInfo{Easing::ElasticOut, "elastic-out", "Пружина"},
    EasingInfo{Easing::BounceOut, "bounce-out", "Отскок"},
    EasingInfo{Easing::Step, "step", "Скачком"},
};

double BounceOut(double t)
{
    constexpr double kN = 7.5625;
    constexpr double kD = 2.75;
    if (t < 1 / kD)
    {
        return kN * t * t;
    }
    if (t < 2 / kD)
    {
        t -= 1.5 / kD;
        return kN * t * t + 0.75;
    }
    if (t < 2.5 / kD)
    {
        t -= 2.25 / kD;
        return kN * t * t + 0.9375;
    }
    t -= 2.625 / kD;
    return kN * t * t + 0.984375;
}

} // namespace

double ApplyEasing(Easing e, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (e)
    {
    case Easing::Linear:
        return t;
    case Easing::EaseIn:
        return t * t;
    case Easing::EaseOut:
        return 1 - (1 - t) * (1 - t);
    case Easing::EaseInOut:
        return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
    case Easing::CubicIn:
        return t * t * t;
    case Easing::CubicOut:
        return 1 - std::pow(1 - t, 3);
    case Easing::CubicInOut:
        return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    case Easing::BackOut:
    {
        constexpr double kC1 = 1.70158;
        constexpr double kC3 = kC1 + 1;
        return 1 + kC3 * std::pow(t - 1, 3) + kC1 * std::pow(t - 1, 2);
    }
    case Easing::ElasticOut:
    {
        if (t == 0 || t == 1)
        {
            return t;
        }
        constexpr double kC4 = (2 * std::numbers::pi) / 3;
        return std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * kC4) + 1;
    }
    case Easing::BounceOut:
        return BounceOut(t);
    case Easing::Step:
        return t < 1 ? 0.0 : 1.0;
    }
    return t;
}

std::span<const EasingInfo> Easings() { return kEasings; }

std::string_view ToString(Easing e)
{
    const auto it = std::ranges::find(kEasings, e, &EasingInfo::easing);
    return it != kEasings.end() ? it->id : "linear";
}

std::optional<Easing> EasingFromString(std::string_view s)
{
    const auto it = std::ranges::find(kEasings, s, &EasingInfo::id);
    if (it == kEasings.end())
    {
        return std::nullopt;
    }
    return it->easing;
}

} // namespace ad
