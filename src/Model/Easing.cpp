#include "Easing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "Utils/I18n.hpp"

namespace ad
{

namespace
{

// built on first use: labels are translated with the UI language installed at startup
const std::vector<EasingInfo> &EasingTable()
{
    auto t = [](const char *text)
    {
        return Tr("easing", text);
    };
    static const std::vector<EasingInfo> kTable{
        {Easing::Linear, "linear", t(AD_TR_NOOP("easing", "Linear"))},
        {Easing::EaseIn, "ease-in", t(AD_TR_NOOP("easing", "Ease in"))},
        {Easing::EaseOut, "ease-out", t(AD_TR_NOOP("easing", "Ease out"))},
        {Easing::EaseInOut, "ease-in-out", t(AD_TR_NOOP("easing", "Ease in-out"))},
        {Easing::CubicIn, "cubic-in", t(AD_TR_NOOP("easing", "Ease in (strong)"))},
        {Easing::CubicOut, "cubic-out", t(AD_TR_NOOP("easing", "Ease out (strong)"))},
        {Easing::CubicInOut, "cubic-in-out", t(AD_TR_NOOP("easing", "Ease in-out (strong)"))},
        {Easing::BackOut, "back-out", t(AD_TR_NOOP("easing", "Overshoot"))},
        {Easing::ElasticOut, "elastic-out", t(AD_TR_NOOP("easing", "Spring"))},
        {Easing::BounceOut, "bounce-out", t(AD_TR_NOOP("easing", "Bounce"))},
        {Easing::Step, "step", t(AD_TR_NOOP("easing", "Step"))},
    };
    return kTable;
}

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

std::span<const EasingInfo> Easings() { return EasingTable(); }

std::string_view ToString(Easing e)
{
    const auto &table = EasingTable();
    const auto  it    = std::ranges::find(table, e, &EasingInfo::easing);
    return it != table.end() ? it->id : "linear";
}

std::optional<Easing> EasingFromString(std::string_view s)
{
    const auto &table = EasingTable();
    const auto  it    = std::ranges::find(table, s, &EasingInfo::id);
    if (it == table.end())
    {
        return std::nullopt;
    }
    return it->easing;
}

} // namespace ad
