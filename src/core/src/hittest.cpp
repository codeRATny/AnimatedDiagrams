#include "ad/hittest.hpp"

#include <ranges>

#include "ad/engine.hpp"

namespace ad {

Hit hitTest(const Model& m, Vec2 p, const Selection& sel, double tolerance) {
    // порты — сверху вниз (последний узел рисуется поверх)
    for (const auto& n : m.nodes | std::views::reverse)
        for (const auto& port : n.ports)
            if (distance(p, {n.x + port.dx, n.y + port.dy}) <= 6 + tolerance)
                return {Hit::Kind::Port, n.id, port.id, 0};

    if (sel.kind == Selection::Kind::Edge)
        if (const Edge* e = m.edge(sel.id))
            for (std::size_t i = 0; i < e->waypoints.size(); ++i)
                if (distance(p, e->waypoints[i]) <= 6 + tolerance) return {Hit::Kind::Waypoint, e->id, {}, i};

    for (const auto& n : m.nodes | std::views::reverse)
        if (n.rect().contains(p)) return {Hit::Kind::Node, n.id, {}, 0};

    const Edge* best = nullptr;
    double bestD = 8 + tolerance;
    for (const auto& e : m.edges) {
        const auto g = edgeGeometry(m, e);
        if (!g) continue;
        const double d = FlatPath(g->path).distanceTo(p);
        if (d <= bestD) {
            bestD = d;
            best = &e;
        }
    }
    if (best) return {Hit::Kind::Edge, best->id, {}, 0};
    return {};
}

}  // namespace ad
