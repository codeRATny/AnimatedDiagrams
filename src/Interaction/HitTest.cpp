#include "HitTest.hpp"

#include <ranges>

#include "Engine/Engine.hpp"

namespace ad
{

Hit HitTest(const Model &m, Vec2 p, const Selection &sel, double tolerance, const Registry &reg)
{
    // ports, top to bottom (the last node is drawn on top)
    for (const auto &n : m.nodes | std::views::reverse)
    {
        for (const auto &port : n.ports)
        {
            if (Distance(p, {n.x + port.dx, n.y + port.dy}) <= 6 + tolerance)
            {
                return {Hit::Kind::Port, n.id, port.id, 0};
            }
        }
    }

    if (sel.kind == Selection::Kind::Edge)
    {
        if (const Edge *e = m.FindEdge(sel.id); e != nullptr)
        {
            for (size_t i = 0; i < e->waypoints.size(); ++i)
            {
                if (Distance(p, e->waypoints[i]) <= 6 + tolerance)
                {
                    return {Hit::Kind::Waypoint, e->id, {}, i};
                }
            }
        }
    }

    for (const auto &n : m.nodes | std::views::reverse)
    {
        if (n.Bounds().Contains(p))
        {
            return {Hit::Kind::Node, n.id, {}, 0};
        }
    }

    const Edge *best   = nullptr;
    double      best_d = 8 + tolerance;
    for (const auto &e : m.edges)
    {
        const auto g = ComputeEdgeGeometry(m, e, reg);
        if (!g.has_value())
        {
            continue;
        }
        const double d = FlatPath(g->path).DistanceTo(p);
        if (d <= best_d)
        {
            best_d = d;
            best   = &e;
        }
    }
    if (best != nullptr)
    {
        return {Hit::Kind::Edge, best->id, {}, 0};
    }
    return {};
}

} // namespace ad
