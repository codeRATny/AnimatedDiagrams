#include "Document.hpp"

#include <algorithm>
#include <cmath>

#include "Model/Markers.hpp"
#include "Utils/I18n.hpp"

namespace ad
{

// ---------------------------------------------------------------------------
// IdGenerator
// ---------------------------------------------------------------------------

IdGenerator::IdGenerator() : _rng(std::random_device{}()) {}
IdGenerator::IdGenerator(uint32_t seed) : _rng(seed) {}

std::string IdGenerator::Next(std::string_view prefix, const Model &model)
{
    static constexpr std::string_view     kAlphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<size_t> dist(0, kAlphabet.size() - 1);
    while (true)
    {
        std::string id(prefix);
        id += '_';
        for (int i = 0; i < 7; ++i)
        {
            id += kAlphabet[dist(_rng)];
        }
        const bool taken = model.FindNode(id) != nullptr || model.FindEdge(id) != nullptr || model.FindStep(id) != nullptr ||
                           model.FindMarker(id) != nullptr ||
                           std::ranges::any_of(model.nodes,
                                               [&](const Node &n)
                                               {
                                                   return n.FindPort(id) != nullptr;
                                               });
        if (!taken)
        {
            return id;
        }
    }
}

// ---------------------------------------------------------------------------
// Duration
// ---------------------------------------------------------------------------

double AutoDuration(const Scenario &s)
{
    double max_end = 0;
    for (const auto &st : s.steps)
    {
        max_end = std::max(max_end, st.End());
    }
    double d = std::max(4000.0, std::ceil((max_end + 1200) / 500) * 500);
    for (const auto &m : s.markers)
    {
        d = std::max(d, m.time); // never cut off a marker
    }
    if (s.user_duration)
    {
        d = std::max(d, s.duration);
    }
    return d;
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void Document::Reset(Model m)
{
    _model = std::move(m);
    _undo.clear();
    _redo.clear();
    _last_merge_key.clear();
}

void Document::Checkpoint(std::string_view merge_key)
{
    if (!merge_key.empty() && merge_key == _last_merge_key && !_undo.empty())
    {
        return;
    }
    _undo.push_back(_model);
    if (_undo.size() > kMaxHistory)
    {
        _undo.erase(_undo.begin());
    }
    _redo.clear();
    _last_merge_key = merge_key;
}

bool Document::Undo()
{
    if (_undo.empty())
    {
        return false;
    }
    _redo.push_back(std::move(_model));
    _model = std::move(_undo.back());
    _undo.pop_back();
    _last_merge_key.clear();
    return true;
}

bool Document::Redo()
{
    if (_redo.empty())
    {
        return false;
    }
    _undo.push_back(std::move(_model));
    _model = std::move(_redo.back());
    _redo.pop_back();
    _last_merge_key.clear();
    return true;
}

// ---------------------------------------------------------------------------
// Nodes and edges
// ---------------------------------------------------------------------------

Node &Document::AddNode(Vec2 center, const ElementType &type)
{
    Checkpoint();
    const auto same_type = std::ranges::count(_model.nodes, type.id, &Node::type);
    Node       n;
    n.id    = NewId("n");
    n.type  = type.id;
    n.label = type.label + " " + std::to_string(same_type + 1);
    n.w     = type.width;
    n.h     = type.height;
    n.x     = std::round(center.x - n.w / 2);
    n.y     = std::round(center.y - n.h / 2);
    _model.nodes.push_back(std::move(n));
    return _model.nodes.back();
}

Edge *Document::AddEdge(std::string_view from, std::string_view to, std::string_view from_port, std::string_view to_port)
{
    if (from == to || _model.FindNode(from) == nullptr || _model.FindNode(to) == nullptr)
    {
        return nullptr;
    }
    Checkpoint();
    // parallel edges between the same nodes get alternating curvature so they do not overlap
    const auto dup = std::ranges::count_if(_model.edges,
                                           [&](const Edge &e)
                                           {
                                               return (e.from == from && e.to == to) || (e.from == to && e.to == from);
                                           });
    Edge       e;
    e.id        = NewId("e");
    e.from      = std::string(from);
    e.to        = std::string(to);
    e.from_port = std::string(from_port);
    e.to_port   = std::string(to_port);
    if (dup > 0)
    {
        e.curve = (dup % 2 != 0 ? 1.0 : -1.0) * 0.2 * std::ceil(static_cast<double>(dup) / 2);
    }
    _model.edges.push_back(std::move(e));
    return &_model.edges.back();
}

void Document::RemoveNode(std::string_view id)
{
    if (_model.FindNode(id) == nullptr)
    {
        return;
    }
    Checkpoint();
    const std::string        nid(id);
    std::vector<std::string> gone_edges;
    for (const auto &e : _model.edges)
    {
        if (e.from == nid || e.to == nid)
        {
            gone_edges.push_back(e.id);
        }
    }
    std::erase_if(_model.nodes,
                  [&](const Node &n)
                  {
                      return n.id == nid;
                  });
    std::erase_if(_model.edges,
                  [&](const Edge &e)
                  {
                      return e.from == nid || e.to == nid;
                  });
    std::erase_if(_model.scenario.steps,
                  [&](const Step &s)
                  {
                      const bool refs_node = s.node_id == nid || s.from == nid || s.to == nid;
                      const bool dead_link = s.type == StepType::Link && std::ranges::contains(gone_edges, s.edge_id);
                      return refs_node || dead_link;
                  });
    for (auto &s : _model.scenario.steps)
    {
        if (std::ranges::contains(gone_edges, s.edge_id))
        {
            s.edge_id.clear();
        }
    }
}

void Document::RemoveEdge(std::string_view id)
{
    if (_model.FindEdge(id) == nullptr)
    {
        return;
    }
    Checkpoint();
    const std::string eid(id);
    std::erase_if(_model.edges,
                  [&](const Edge &e)
                  {
                      return e.id == eid;
                  });
    // a link step without its edge is meaningless; a message keeps flying between the nodes
    std::erase_if(_model.scenario.steps,
                  [&](const Step &s)
                  {
                      return s.type == StepType::Link && s.edge_id == eid;
                  });
    for (auto &s : _model.scenario.steps)
    {
        if (s.edge_id == eid)
        {
            s.edge_id.clear();
        }
    }
}

void Document::RemoveStep(std::string_view id)
{
    if (_model.FindStep(id) == nullptr)
    {
        return;
    }
    Checkpoint();
    std::erase_if(_model.scenario.steps,
                  [&](const Step &s)
                  {
                      return s.id == id;
                  });
    UpdateDuration();
}

// ---------------------------------------------------------------------------
// Ports and waypoints
// ---------------------------------------------------------------------------

Port *Document::AddPort(std::string_view node_id, std::optional<Vec2> offset)
{
    if (_model.FindNode(node_id) == nullptr)
    {
        return nullptr;
    }
    Checkpoint();
    Node      *n = _model.FindNode(node_id);
    const Vec2 o = offset.value_or(Vec2{n->w, n->h / 2});
    n->ports.push_back({NewId("p"), std::round(o.x), std::round(o.y)});
    return &n->ports.back();
}

void Document::RemovePort(std::string_view node_id, std::string_view port_id)
{
    const Node *n = _model.FindNode(node_id);
    if (n == nullptr || n->FindPort(port_id) == nullptr)
    {
        return;
    }
    Checkpoint();
    std::erase_if(_model.FindNode(node_id)->ports,
                  [&](const Port &p)
                  {
                      return p.id == port_id;
                  });
    for (auto &e : _model.edges)
    {
        if (e.from_port == port_id)
        {
            e.from_port.clear();
        }
        if (e.to_port == port_id)
        {
            e.to_port.clear();
        }
    }
}

void Document::AddWaypoint(std::string_view edge_id, Vec2 p, std::optional<size_t> index)
{
    if (_model.FindEdge(edge_id) == nullptr)
    {
        return;
    }
    Checkpoint();
    Edge      *e = _model.FindEdge(edge_id);
    const Vec2 wp{std::round(p.x), std::round(p.y)};
    if (!index.has_value() || *index > e->waypoints.size())
    {
        e->waypoints.push_back(wp);
    }
    else
    {
        e->waypoints.insert(e->waypoints.begin() + static_cast<std::ptrdiff_t>(*index), wp);
    }
}

void Document::RemoveWaypoint(std::string_view edge_id, size_t index)
{
    const Edge *e = _model.FindEdge(edge_id);
    if (e == nullptr || index >= e->waypoints.size())
    {
        return;
    }
    Checkpoint();
    auto &wps = _model.FindEdge(edge_id)->waypoints;
    wps.erase(wps.begin() + static_cast<std::ptrdiff_t>(index));
}

void Document::ReverseEdge(std::string_view edge_id)
{
    if (_model.FindEdge(edge_id) == nullptr)
    {
        return;
    }
    Checkpoint();
    Edge *e = _model.FindEdge(edge_id);
    std::swap(e->from, e->to);
    std::swap(e->from_port, e->to_port);
    std::swap(e->style.arrow_start, e->style.arrow_end);
    e->curve = -e->curve;
    std::ranges::reverse(e->waypoints);
}

// ---------------------------------------------------------------------------
// Steps
// ---------------------------------------------------------------------------

Step &Document::AddStep(Step s)
{
    Checkpoint();
    if (s.id.empty() || _model.FindStep(s.id) != nullptr)
    {
        s.id = NewId("s");
    }
    s.start              = std::max(0.0, s.start);
    s.duration           = std::max(kMinStepDuration, s.duration);
    const std::string id = s.id;
    _model.scenario.steps.push_back(std::move(s));
    SortSteps();
    UpdateDuration();
    return *_model.FindStep(id);
}

Step *Document::DuplicateStep(std::string_view id)
{
    const Step *src = _model.FindStep(id);
    if (src == nullptr)
    {
        return nullptr;
    }
    Step copy = *src;
    copy.id.clear();
    copy.start = src->End() + 200;
    return &AddStep(std::move(copy));
}

std::optional<Step> Document::MakeDefaultStep(StepType type, double at) const
{
    const auto &nodes = _model.nodes;
    Step        s;
    s.type     = type;
    s.start    = std::round(std::max(0.0, at) / 50) * 50;
    s.duration = 1000;
    if (type != StepType::Note && type != StepType::Link && nodes.empty())
    {
        return std::nullopt;
    }
    switch (type)
    {
    case StepType::Message:
        s.from = nodes[0].id;
        s.to   = nodes.size() > 1 ? nodes[1].id : nodes[0].id;
        if (const Edge *e = _model.EdgeBetween(s.from, s.to); e != nullptr)
        {
            s.edge_id = e->id;
            s.from    = e->from;
            s.to      = e->to;
        }
        s.duration = 1100;
        break;
    case StepType::Timer:
        s.node_id  = nodes[0].id;
        s.seconds  = 5;
        s.duration = 5000;
        s.label    = "timeout";
        break;
    case StepType::State:
        s.node_id  = nodes[0].id;
        s.duration = 3000;
        break;
    case StepType::Note:
        s.text     = Tr("document", "Note");
        s.x        = 60;
        s.y        = 30;
        s.duration = 2500;
        break;
    case StepType::Effect:
        s.node_id  = nodes[0].id;
        s.effect   = "pulse";
        s.duration = 900;
        break;
    case StepType::Action:
        s.node_id  = nodes[0].id;
        s.text     = Tr("document", "Processing");
        s.color    = "#38bdf8";
        s.duration = 1800;
        break;
    case StepType::Link:
        if (_model.edges.empty())
        {
            return std::nullopt;
        }
        s.edge_id  = _model.edges[0].id;
        s.text     = Tr("document", "Connection");
        s.color    = "#38bdf8";
        s.duration = 2000;
        break;
    }
    return s;
}

void Document::NormalizeStep(Step &s) const
{
    const auto       &nodes = _model.nodes;
    const std::string first = nodes.empty() ? std::string{} : nodes[0].id;
    auto              pick  = [&]
    {
        if (_model.FindNode(s.node_id) != nullptr)
        {
            return s.node_id;
        }
        return _model.FindNode(s.from) != nullptr ? s.from : first;
    };
    switch (s.type)
    {
    case StepType::Message:
        if (s.from.empty())
        {
            s.from = !s.node_id.empty() ? s.node_id : first;
        }
        if (s.to.empty())
        {
            s.to = nodes.size() > 1 ? nodes[1].id : s.from;
        }
        if (_model.FindEdge(s.edge_id) == nullptr)
        {
            s.edge_id.clear();
            if (const Edge *e = _model.EdgeBetween(s.from, s.to); e != nullptr)
            {
                s.edge_id = e->id;
            }
        }
        break;
    case StepType::Timer:
        s.node_id = pick();
        if (s.seconds <= 0)
        {
            s.seconds = std::round(s.duration / 1000);
        }
        break;
    case StepType::State:
    case StepType::Effect:
        s.node_id = pick();
        break;
    case StepType::Action:
        s.node_id = pick();
        if (s.text.empty())
        {
            s.text = Tr("document", "Action");
        }
        break;
    case StepType::Link:
        if (s.text.empty())
        {
            s.text = Tr("document", "Connection");
        }
        if (s.color.empty())
        {
            s.color = "#38bdf8";
        }
        if (_model.FindEdge(s.edge_id) == nullptr)
        {
            s.edge_id = _model.edges.empty() ? std::string{} : _model.edges[0].id;
        }
        break;
    case StepType::Note:
        if (s.text.empty())
        {
            s.text = Tr("document", "Note");
        }
        if (s.x == 0 && s.y == 0)
        {
            s.x = 60;
            s.y = 30;
        }
        break;
    }
}

void Document::SetDuration(double ms)
{
    Checkpoint("scenario:duration");
    _model.scenario.duration      = std::max(1000.0, ms);
    _model.scenario.user_duration = true;
    NormalizeMarkers(_model.scenario); // markers after the new end move to it
}

void Document::SortSteps() { std::ranges::stable_sort(_model.scenario.steps, {}, &Step::start); }

void Document::UpdateDuration() { _model.scenario.duration = AutoDuration(_model.scenario); }

// ---------------------------------------------------------------------------
// Markers
// ---------------------------------------------------------------------------

Marker *Document::AddMarker(double time, std::string label)
{
    time = std::clamp(time, 0.0, _model.scenario.duration);
    if (MarkerAt(_model.scenario, time) != nullptr)
    {
        return nullptr;
    }
    Checkpoint();
    Marker            m{.id = NewId("m"), .time = time, .label = std::move(label)};
    const std::string id = m.id;
    _model.scenario.markers.push_back(std::move(m));
    NormalizeMarkers(_model.scenario);
    return _model.FindMarker(id);
}

bool Document::MoveMarker(std::string_view id, double time, std::string_view merge_key)
{
    if (_model.FindMarker(id) == nullptr)
    {
        return false;
    }
    time = std::clamp(time, 0.0, _model.scenario.duration);
    if (const Marker *other = MarkerAt(_model.scenario, time); other != nullptr && other->id != id)
    {
        return false;
    }
    Checkpoint(merge_key);
    _model.FindMarker(id)->time = time;
    NormalizeMarkers(_model.scenario);
    return true;
}

bool Document::RenameMarker(std::string_view id, std::string label)
{
    if (_model.FindMarker(id) == nullptr)
    {
        return false;
    }
    Checkpoint();
    _model.FindMarker(id)->label = std::move(label);
    return true;
}

bool Document::RemoveMarker(std::string_view id)
{
    if (_model.FindMarker(id) == nullptr)
    {
        return false;
    }
    Checkpoint();
    std::erase_if(_model.scenario.markers,
                  [id](const Marker &m)
                  {
                      return m.id == id;
                  });
    return true;
}

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------

void Document::UpsertElement(const ElementType &e)
{
    Checkpoint();
    _model.library.Upsert(e);
}

void Document::UpsertEffect(const EffectDef &e)
{
    Checkpoint();
    _model.library.Upsert(e);
}

void Document::UpsertAnimation(const AnimationTemplate &a)
{
    Checkpoint();
    _model.library.Upsert(a);
}

void Document::UpsertDesignSystem(const DesignSystem &d)
{
    Checkpoint();
    _model.library.Upsert(d);
}

bool Document::RemoveLibraryItem(std::string_view id)
{
    auto      &lib = _model.library;
    const bool has = lib.Element(id) != nullptr || lib.Effect(id) != nullptr || lib.Animation(id) != nullptr || lib.Design(id) != nullptr;
    if (!has)
    {
        return false;
    }
    Checkpoint();
    auto by_id = [id](const auto &x)
    {
        return x.id == id;
    };
    std::erase_if(lib.elements, by_id);
    std::erase_if(lib.effects, by_id);
    std::erase_if(lib.animations, by_id);
    std::erase_if(lib.design_systems, by_id);
    return true;
}

} // namespace ad
