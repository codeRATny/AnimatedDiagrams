#include "ad/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace ad {

std::vector<LaneAssignment> packLanes(const std::vector<Step>& steps) {
    std::vector<std::size_t> order(steps.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return steps[i].start; });

    std::vector<double> laneEnds;  // конец последнего шага в каждой дорожке
    std::vector<LaneAssignment> out;
    out.reserve(steps.size());
    for (const std::size_t i : order) {
        const Step& s = steps[i];
        int lane = -1;
        for (std::size_t l = 0; l < laneEnds.size(); ++l)
            if (laneEnds[l] <= s.start) {
                lane = static_cast<int>(l);
                break;
            }
        if (lane < 0) {
            lane = static_cast<int>(laneEnds.size());
            laneEnds.push_back(0);
        }
        laneEnds[static_cast<std::size_t>(lane)] = s.end();
        out.push_back({s.id, lane});
    }
    return out;
}

int laneCount(const std::vector<LaneAssignment>& lanes) {
    int n = 0;
    for (const auto& l : lanes) n = std::max(n, l.lane + 1);
    return n;
}

double tickStep(double start, double stop, int count) {
    const double step0 = std::abs(stop - start) / std::max(1, count);
    if (!(step0 > 0)) return 1;
    double step1 = std::pow(10, std::floor(std::log10(step0)));
    const double err = step0 / step1;
    if (err >= std::sqrt(50.0))
        step1 *= 10;
    else if (err >= std::sqrt(10.0))
        step1 *= 5;
    else if (err >= std::sqrt(2.0))
        step1 *= 2;
    return step1;
}

std::vector<double> niceTicks(double start, double stop, int count) {
    std::vector<double> out;
    if (!(stop > start) || count <= 0) return out;
    const double step = tickStep(start, stop, count);
    const long long i0 = static_cast<long long>(std::ceil(start / step - 1e-9));
    const long long i1 = static_cast<long long>(std::floor(stop / step + 1e-9));
    for (long long i = i0; i <= i1; ++i) out.push_back(static_cast<double>(i) * step);
    return out;
}

std::string formatTick(double value, double step) {
    const int decimals = step >= 1 ? 0 : static_cast<int>(std::ceil(-std::log10(step) - 1e-9));
    return std::format("{:.{}f}", value, std::clamp(decimals, 0, 6));
}

double contentEnd(const Model& m) {
    double end = m.scenario.duration;
    for (const auto& s : m.scenario.steps) end = std::max(end, s.end());
    return end;
}

std::string stepTitle(const Model& m, const Step& s) {
    auto nm = [&](const std::string& id) -> std::string {
        const Node* n = m.node(id);
        return n ? n->label : "?";
    };
    auto suffix = [](const std::string& v) { return v.empty() ? std::string{} : " · " + v; };
    switch (s.type) {
        case StepType::Message: return nm(s.from) + " → " + nm(s.to) + suffix(s.label);
        case StepType::Timer: {
            const double secs = s.seconds > 0 ? s.seconds : std::round(s.duration / 1000);
            return std::format("⏱ {} · {}{}", nm(s.nodeId), secs, timeUnit(s.unit).shortLabel);
        }
        case StepType::State: {
            const std::string lbl = s.label.empty() ? std::string(nodeState(s.state).label) : s.label;
            return "⇄ " + nm(s.nodeId) + " → " + lbl;
        }
        case StepType::Action: return "⚙ " + nm(s.nodeId) + " · " + s.text;
        case StepType::Link: {
            const Edge* e = m.edge(s.edgeId);
            return "⚡ " + (e ? nm(e->from) + " ↔ " + nm(e->to) : std::string("?")) + suffix(s.text);
        }
        case StepType::Note: return "✎ " + s.text;
        case StepType::Pulse: return "✷ " + nm(s.nodeId);
    }
    return std::string(toString(s.type));
}

Color stepColor(const Step& s) {
    switch (s.type) {
        case StepType::Message: return msgVariant(s.variant).color;
        case StepType::Timer: return Color::rgb(0xf59e0b);
        case StepType::State: return Color::parseOr(s.color, nodeState(s.state).ring);
        case StepType::Action: return Color::parseOr(s.color, Color::rgb(0x38bdf8));
        case StepType::Link: return Color::parseOr(s.color, Color::rgb(0x93c5fd));
        case StepType::Note: return Color::rgb(0xfbbf24);
        case StepType::Pulse: return Color::rgb(0x22d3ee);
    }
    return Color::rgb(0x64748b);
}

double snapTime(double ms, double grid) { return std::max(0.0, std::round(ms / grid) * grid); }

std::string formatTime(double ms) {
    const double s = ms / 1000;
    return std::format("{:.{}f}с", s, s < 10 ? 2 : 1);
}

}  // namespace ad
