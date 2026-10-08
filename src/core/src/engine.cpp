#include "ad/engine.hpp"

#include <limits>

namespace ad {

const Step* stateAt(const Model& m, std::string_view nodeId, double t) {
    const double sceneEnd = m.scenario.duration;
    const Step* chosen = nullptr;
    double chosenStart = -1;
    for (const auto& s : m.scenario.steps) {
        if (s.type != StepType::State || s.nodeId != nodeId) continue;
        const bool inside = s.start <= t && (t < s.end() || (t >= sceneEnd && s.end() >= sceneEnd));
        if (inside && s.start >= chosenStart) {
            chosen = &s;
            chosenStart = s.start;
        }
    }
    return chosen;
}

std::vector<const Step*> activeSteps(const Model& m, double t) {
    std::vector<const Step*> out;
    for (const auto& s : m.scenario.steps)
        if (t >= s.start && t <= s.end()) out.push_back(&s);
    return out;
}

ResolvedState resolveNodeState(const Step* step) {
    ResolvedState r;
    if (!step) {
        const auto& b = nodeState("ok");
        r.label = std::string(b.label);
        r.fill = b.fill;
        r.ring = b.ring;
        return r;
    }
    const auto& base = nodeState(step->state);
    r.id = std::string(base.id);
    r.fill = base.fill;
    r.ring = base.ring;
    if (const auto c = Color::parse(step->color)) {
        r.ring = *c;
        r.fill = c->darker(1.9);  // тёмная заливка из акцента — как d3.color().darker(1.9)
    }
    r.customLabel = !step->label.empty();
    r.label = r.customLabel ? step->label : std::string(base.label);
    r.labelSize = step->labelSize;
    return r;
}

std::optional<EdgeGeometry> edgeGeometry(const Model& m, const Edge& e) {
    const Node* a = m.node(e.from);
    const Node* b = m.node(e.to);
    if (!a || !b) return std::nullopt;

    constexpr double kGap = 4;  // зазор между кончиком стрелки и узлом
    const Vec2 ca = a->center();
    const Vec2 cb = b->center();
    const auto& wps = e.waypoints;
    Vec2 ctrl;
    Vec2 startRef;
    Vec2 endRef;
    if (!wps.empty()) {
        startRef = wps.front();
        endRef = wps.back();
    } else {
        ctrl = geom::perpControl(ca, cb, e.curve);
        startRef = endRef = ctrl;
    }
    const Port* fp = a->port(e.fromPort);
    const Port* tp = b->port(e.toPort);

    EdgeGeometry g;
    g.rawStart = fp ? Vec2{a->x + fp->dx, a->y + fp->dy}
                    : geom::borderPoint(ca, a->w / 2 + kGap, a->h / 2 + kGap, startRef);
    g.rawEnd = tp ? Vec2{b->x + tp->dx, b->y + tp->dy}
                  : geom::borderPoint(cb, b->w / 2 + kGap, b->h / 2 + kGap, endRef);
    // линия заканчивается у основания стрелки, кончик стрелки — в rawEnd
    g.start = e.bidirectional ? geom::pullBack(startRef, g.rawStart, kArrowLen) : g.rawStart;
    g.end = geom::pullBack(endRef, g.rawEnd, kArrowLen);

    g.points.reserve(wps.size() + 2);
    g.points.push_back(g.start);
    g.points.insert(g.points.end(), wps.begin(), wps.end());
    g.points.push_back(g.end);

    if (!wps.empty())
        g.path = Path::catmullRom(g.points);
    else if (e.curve != 0)
        g.path = Path::quad(g.start, ctrl, g.end);
    else
        g.path = Path::line(g.start, g.end);
    return g;
}

std::size_t nearestSegmentIndex(const Model& m, const Edge& e, Vec2 p) {
    const auto g = edgeGeometry(m, e);
    if (!g) return 0;
    std::size_t best = 0;
    double bestD = std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k + 1 < g->points.size(); ++k) {
        const double d = geom::distToSegment(p, g->points[k], g->points[k + 1]);
        if (d < bestD) {
            bestD = d;
            best = k;
        }
    }
    return best;
}

}  // namespace ad
