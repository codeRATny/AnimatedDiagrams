#include "TimelineLayout.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace ad
{

std::vector<LaneAssignment> PackLanes(const std::vector<Step> &steps)
{
    std::vector<size_t> order(steps.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::stable_sort(order, {},
                             [&](size_t i)
                             {
                                 return steps[i].start;
                             });

    std::vector<double>         lane_ends; // end of the last step in each lane
    std::vector<LaneAssignment> out;
    out.reserve(steps.size());
    for (const size_t i : order)
    {
        const Step &s    = steps[i];
        int         lane = -1;
        for (size_t l = 0; l < lane_ends.size(); ++l)
        {
            if (lane_ends[l] <= s.start)
            {
                lane = static_cast<int>(l);
                break;
            }
        }
        if (lane < 0)
        {
            lane = static_cast<int>(lane_ends.size());
            lane_ends.push_back(0);
        }
        lane_ends[static_cast<size_t>(lane)] = s.End();
        out.push_back({s.id, lane});
    }
    return out;
}

int LaneCount(const std::vector<LaneAssignment> &lanes)
{
    int n = 0;
    for (const auto &l : lanes)
    {
        n = std::max(n, l.lane + 1);
    }
    return n;
}

double TickStep(double start, double stop, int count)
{
    const double step0 = std::abs(stop - start) / std::max(1, count);
    if (step0 <= 0)
    {
        return 1;
    }
    double       step1 = std::pow(10, std::floor(std::log10(step0)));
    const double err   = step0 / step1;
    if (err >= std::sqrt(50.0))
    {
        step1 *= 10;
    }
    else if (err >= std::sqrt(10.0))
    {
        step1 *= 5;
    }
    else if (err >= std::sqrt(2.0))
    {
        step1 *= 2;
    }
    return step1;
}

std::vector<double> NiceTicks(double start, double stop, int count)
{
    std::vector<double> out;
    if (stop <= start || count <= 0)
    {
        return out;
    }
    const double  step = TickStep(start, stop, count);
    const int64_t i0   = static_cast<int64_t>(std::ceil(start / step - 1e-9));
    const int64_t i1   = static_cast<int64_t>(std::floor(stop / step + 1e-9));
    for (int64_t i = i0; i <= i1; ++i)
    {
        out.push_back(static_cast<double>(i) * step);
    }
    return out;
}

std::string FormatTick(double value, double step)
{
    const int decimals = step >= 1 ? 0 : static_cast<int>(std::ceil(-std::log10(step) - 1e-9));
    return std::format("{:.{}f}", value, std::clamp(decimals, 0, 6));
}

double ContentEnd(const Model &m)
{
    double end = m.scenario.duration;
    for (const auto &s : m.scenario.steps)
    {
        end = std::max(end, s.End());
    }
    return end;
}

std::string StepTitle(const Model &m, const Step &s, const Registry &reg)
{
    auto nm = [&](const std::string &id) -> std::string
    {
        const Node *n = m.FindNode(id);
        return n != nullptr ? n->label : "?";
    };
    auto suffix = [](const std::string &v)
    {
        return v.empty() ? std::string{} : " · " + v;
    };
    switch (s.type)
    {
    case StepType::Message:
        return nm(s.from) + " → " + nm(s.to) + suffix(s.label);
    case StepType::Timer:
    {
        const double secs = s.seconds > 0 ? s.seconds : std::round(s.duration / 1000);
        return std::format("⏱ {} · {}{}", nm(s.node_id), secs, TimeUnit(s.unit).short_label);
    }
    case StepType::State:
    {
        const std::string lbl = s.label.empty() ? std::string(NodeState(s.state).label) : s.label;
        return "⇄ " + nm(s.node_id) + " → " + lbl;
    }
    case StepType::Action:
        return "⚙ " + nm(s.node_id) + " · " + s.text;
    case StepType::Link:
    {
        const Edge *e = m.FindEdge(s.edge_id);
        return "⚡ " + (e != nullptr ? nm(e->from) + " ↔ " + nm(e->to) : std::string("?")) + suffix(s.text);
    }
    case StepType::Note:
        return "✎ " + s.text;
    case StepType::Effect:
    {
        const EffectDef *def = reg.FindEffect(s.effect, &m.library);
        return "✷ " + nm(s.node_id) + " · " + (def != nullptr ? def->label : s.effect);
    }
    }
    return std::string(ToString(s.type));
}

Color StepColor(const Step &s)
{
    switch (s.type)
    {
    case StepType::Message:
        return Color::Parse(s.color, MsgVariant(s.variant).color);
    case StepType::Timer:
        return Color::Rgb(0xf59e0b);
    case StepType::State:
        return Color::Parse(s.color, NodeState(s.state).ring);
    case StepType::Action:
        return Color::Parse(s.color, Color::Rgb(0x38bdf8));
    case StepType::Link:
        return Color::Parse(s.color, Color::Rgb(0x93c5fd));
    case StepType::Note:
        return Color::Rgb(0xfbbf24);
    case StepType::Effect:
        return Color::Parse(s.color, Color::Rgb(0x22d3ee));
    }
    return Color::Rgb(0x64748b);
}

double SnapTime(double ms, double grid) { return std::max(0.0, std::round(ms / grid) * grid); }

std::string FormatTime(double ms)
{
    const double s = ms / 1000;
    return std::format("{:.{}f}с", s, s < 10 ? 2 : 1);
}

} // namespace ad
