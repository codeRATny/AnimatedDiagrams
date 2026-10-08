#include "Layout.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace ad
{

void AutoLayout(Model &m, const LayoutOptions &opt)
{
    if (m.nodes.empty())
    {
        return;
    }
    std::map<std::string, std::vector<std::string>> out;
    std::map<std::string, int>                      indeg;
    for (const auto &n : m.nodes)
    {
        indeg[n.id] = 0;
    }
    for (const auto &e : m.edges)
    {
        if (indeg.contains(e.from) && indeg.contains(e.to) && e.from != e.to)
        {
            out[e.from].push_back(e.to);
            ++indeg[e.to];
        }
    }

    // Kahn's algorithm with longest-path layering; nodes left in cycles start new roots
    std::map<std::string, int> layer;
    std::set<std::string>      done;
    auto                       remaining = indeg;
    while (done.size() < m.nodes.size())
    {
        std::vector<std::string> queue;
        for (const auto &n : m.nodes)
        {
            if (!done.contains(n.id) && remaining[n.id] == 0)
            {
                queue.push_back(n.id);
            }
        }
        if (queue.empty())
        {
            // cycle: take the first unvisited node in document order
            for (const auto &n : m.nodes)
            {
                if (!done.contains(n.id))
                {
                    queue.push_back(n.id);
                    break;
                }
            }
        }
        for (const auto &id : queue)
        {
            if (!done.insert(id).second)
            {
                continue;
            }
            for (const auto &to : out[id])
            {
                if (done.contains(to))
                {
                    continue;
                }
                layer[to] = std::max(layer[to], layer[id] + 1);
                --remaining[to];
            }
        }
    }

    std::map<int, std::vector<Node *>> by_layer;
    double                             max_w = 0;
    double                             max_h = 0;
    for (auto &n : m.nodes)
    {
        by_layer[layer[n.id]].push_back(&n);
        max_w = std::max(max_w, n.w);
        max_h = std::max(max_h, n.h);
    }
    const bool lr = opt.direction == LayoutDirection::LeftToRight;
    for (auto &[l, nodes] : by_layer)
    {
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            Node        *n     = nodes[i];
            const double major = (lr ? max_w : max_h) + opt.gap_major;
            const double minor = (lr ? max_h : max_w) + opt.gap_minor;
            const double a     = l * major;
            const double b     = static_cast<double>(i) * minor;
            n->x               = opt.origin.x + (lr ? a + (max_w - n->w) / 2 : b + (max_w - n->w) / 2);
            n->y               = opt.origin.y + (lr ? b + (max_h - n->h) / 2 : a + (max_h - n->h) / 2);
        }
    }
    for (auto &e : m.edges)
    {
        e.waypoints.clear();
    }
}

} // namespace ad
