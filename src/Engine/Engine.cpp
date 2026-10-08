#include "Engine.hpp"

#include <cmath>
#include <limits>

#include "Engine/Shapes.hpp"

namespace ad
{

const Step *StateAt(const Model &m, std::string_view node_id, double t)
{
    const double scene_end    = m.scenario.duration;
    const Step  *chosen       = nullptr;
    double       chosen_start = -1;
    for (const auto &s : m.scenario.steps)
    {
        if (s.type != StepType::State || s.node_id != node_id)
        {
            continue;
        }
        const bool inside = s.start <= t && (t < s.End() || (t >= scene_end && s.End() >= scene_end));
        if (inside && s.start >= chosen_start)
        {
            chosen       = &s;
            chosen_start = s.start;
        }
    }
    return chosen;
}

std::vector<const Step *> ActiveSteps(const Model &m, double t)
{
    std::vector<const Step *> out;
    for (const auto &s : m.scenario.steps)
    {
        if (t >= s.start && t <= s.End())
        {
            out.push_back(&s);
        }
    }
    return out;
}

ResolvedState ResolveNodeState(const Step *step, const NodeStyle &style)
{
    ResolvedState r;
    if (step == nullptr)
    {
        const auto &b = NodeState("ok");
        r.label       = std::string(b.label);
        r.fill        = Color::Parse(style.fill.value_or(""), b.fill);
        r.ring        = Color::Parse(style.stroke.value_or(""), b.ring);
        return r;
    }
    const auto &base = NodeState(step->state);
    r.id             = std::string(base.id);
    r.fill           = base.fill;
    r.ring           = base.ring;
    if (const auto c = Color::Parse(step->color); c.has_value())
    {
        r.ring = *c;
        r.fill = c->Darker(1.9); // dark fill derived from the accent, as d3.color().darker(1.9)
    }
    r.custom_label = !step->label.empty();
    r.label        = r.custom_label ? step->label : std::string(base.label);
    r.label_size   = step->label_size;
    return r;
}

NodeStyle ResolveNodeStyle(const Model &m, const Node &n, const Registry &reg)
{
    return reg.Element(n.type, &m.library).style.Merged(n.style);
}

std::string ResolveShape(const NodeStyle &style)
{
    const std::string shape = style.shape.value_or("rounded");
    return IsKnownOption(Shapes(), shape) ? shape : "rounded";
}

namespace
{

bool HasArrow(const std::optional<std::string> &head, bool def) { return head.has_value() ? *head != "none" : def; }

} // namespace

std::optional<EdgeGeometry> ComputeEdgeGeometry(const Model &m, const Edge &e, const Registry &reg)
{
    const Node *a = m.FindNode(e.from);
    const Node *b = m.FindNode(e.to);
    if (a == nullptr || b == nullptr)
    {
        return std::nullopt;
    }

    constexpr double  kGap    = 4; // gap between the arrow tip and the node
    const std::string shape_a = ResolveShape(ResolveNodeStyle(m, *a, reg));
    const std::string shape_b = ResolveShape(ResolveNodeStyle(m, *b, reg));
    const std::string routing = e.style.routing.value_or("curved");
    const Vec2        ca      = a->Center();
    const Vec2        cb      = b->Center();
    const auto       &wps     = e.waypoints;
    const Port       *fp      = a->FindPort(e.from_port);
    const Port       *tp      = b->FindPort(e.to_port);

    EdgeGeometry g;
    g.arrow_start = HasArrow(e.style.arrow_start, false);
    g.arrow_end   = HasArrow(e.style.arrow_end, true);

    Vec2 ctrl;
    Vec2 start_ref;
    Vec2 end_ref;
    if (!wps.empty())
    {
        start_ref = wps.front();
        end_ref   = wps.back();
    }
    else if (routing == "orthogonal")
    {
        // leave/enter through the sides facing each other along the dominant axis
        const bool horizontal = std::abs(cb.x - ca.x) >= std::abs(cb.y - ca.y);
        start_ref             = horizontal ? Vec2{cb.x, ca.y} : Vec2{ca.x, cb.y};
        end_ref               = horizontal ? Vec2{ca.x, cb.y} : Vec2{cb.x, ca.y};
    }
    else
    {
        ctrl      = geom::PerpControl(ca, cb, routing == "straight" ? 0.0 : e.curve);
        start_ref = end_ref = ctrl;
    }

    g.raw_start = fp != nullptr ? Vec2{a->x + fp->dx, a->y + fp->dy} : ShapeBorderPoint(shape_a, a->Bounds(), start_ref, kGap);
    g.raw_end   = tp != nullptr ? Vec2{b->x + tp->dx, b->y + tp->dy} : ShapeBorderPoint(shape_b, b->Bounds(), end_ref, kGap);

    std::vector<Vec2> inner = wps;
    if (wps.empty() && routing == "orthogonal")
    {
        const bool horizontal = std::abs(cb.x - ca.x) >= std::abs(cb.y - ca.y);
        if (horizontal)
        {
            const double mx = (g.raw_start.x + g.raw_end.x) / 2;
            inner           = {{mx, g.raw_start.y}, {mx, g.raw_end.y}};
        }
        else
        {
            const double my = (g.raw_start.y + g.raw_end.y) / 2;
            inner           = {{g.raw_start.x, my}, {g.raw_end.x, my}};
        }
    }
    const Vec2 first_ref = inner.empty() ? start_ref : inner.front();
    const Vec2 last_ref  = inner.empty() ? end_ref : inner.back();
    // the line ends at the arrow base, the arrow tip stays at raw_end
    g.start = g.arrow_start ? geom::PullBack(first_ref, g.raw_start, kArrowLen) : g.raw_start;
    g.end   = g.arrow_end ? geom::PullBack(last_ref, g.raw_end, kArrowLen) : g.raw_end;

    g.points.reserve(inner.size() + 2);
    g.points.push_back(g.start);
    g.points.insert(g.points.end(), inner.begin(), inner.end());
    g.points.push_back(g.end);

    if (!inner.empty() && routing == "curved")
    {
        g.path = Path::CatmullRom(g.points);
    }
    else if (!inner.empty())
    {
        g.path.MoveTo(g.points.front());
        for (size_t i = 1; i < g.points.size(); ++i)
        {
            g.path.LineTo(g.points[i]);
        }
    }
    else if (routing == "curved" && e.curve != 0)
    {
        g.path = Path::Quad(g.start, ctrl, g.end);
    }
    else
    {
        g.path = Path::Line(g.start, g.end);
    }
    return g;
}

size_t NearestSegmentIndex(const Model &m, const Edge &e, Vec2 p, const Registry &reg)
{
    const auto g = ComputeEdgeGeometry(m, e, reg);
    if (!g.has_value())
    {
        return 0;
    }
    if (e.waypoints.empty())
    {
        return 0; // generated orthogonal elbows are not waypoints
    }
    size_t best   = 0;
    double best_d = std::numeric_limits<double>::infinity();
    for (size_t k = 0; k + 1 < g->points.size(); ++k)
    {
        const double d = geom::DistToSegment(p, g->points[k], g->points[k + 1]);
        if (d < best_d)
        {
            best_d = d;
            best   = k;
        }
    }
    return best;
}

} // namespace ad
