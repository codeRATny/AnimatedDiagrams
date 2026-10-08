#include "ad/document.hpp"

#include <algorithm>
#include <cmath>

namespace ad {

// ---- IdGenerator ------------------------------------------------------------

IdGenerator::IdGenerator() : rng_(std::random_device{}()) {}
IdGenerator::IdGenerator(std::uint32_t seed) : rng_(seed) {}

std::string IdGenerator::next(std::string_view prefix, const Model& model) {
    static constexpr std::string_view kAlphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<std::size_t> dist(0, kAlphabet.size() - 1);
    for (;;) {
        std::string id(prefix);
        id += '_';
        for (int i = 0; i < 7; ++i) id += kAlphabet[dist(rng_)];
        const bool taken = model.node(id) || model.edge(id) || model.step(id) ||
                           std::ranges::any_of(model.nodes, [&](const Node& n) { return n.port(id) != nullptr; });
        if (!taken) return id;
    }
}

// ---- duration ---------------------------------------------------------------

double autoDuration(const Scenario& s) {
    double maxEnd = 0;
    for (const auto& st : s.steps) maxEnd = std::max(maxEnd, st.end());
    double d = std::max(4000.0, std::ceil((maxEnd + 1200) / 500) * 500);
    if (s.userDuration) d = std::max(d, s.duration);
    return d;
}

// ---- history ----------------------------------------------------------------

void Document::reset(Model m) {
    model_ = std::move(m);
    undo_.clear();
    redo_.clear();
    lastMergeKey_.clear();
}

void Document::checkpoint(std::string_view mergeKey) {
    if (!mergeKey.empty() && mergeKey == lastMergeKey_ && !undo_.empty()) return;
    undo_.push_back(model_);
    if (undo_.size() > kMaxHistory) undo_.erase(undo_.begin());
    redo_.clear();
    lastMergeKey_ = mergeKey;
}

bool Document::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(model_));
    model_ = std::move(undo_.back());
    undo_.pop_back();
    lastMergeKey_.clear();
    return true;
}

bool Document::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(model_));
    model_ = std::move(redo_.back());
    redo_.pop_back();
    lastMergeKey_.clear();
    return true;
}

// ---- nodes / edges ----------------------------------------------------------

Node& Document::addNode(Vec2 center, std::string_view kind) {
    checkpoint();
    const auto& k = nodeKind(kind);
    Node n;
    n.id = newId("n");
    n.kind = std::string(k.id);
    n.label = std::string(k.label) + " " + std::to_string(model_.nodes.size() + 1);
    n.x = std::round(center.x - kDefaultNodeW / 2);
    n.y = std::round(center.y - kDefaultNodeH / 2);
    n.color = k.color.hex();
    n.shape = std::string(k.shape);
    model_.nodes.push_back(std::move(n));
    return model_.nodes.back();
}

Edge* Document::addEdge(std::string_view from, std::string_view to, std::string_view fromPort,
                        std::string_view toPort) {
    if (from == to || !model_.node(from) || !model_.node(to)) return nullptr;
    checkpoint();
    // параллельные связи между теми же узлами раздвигаем изгибом, чтобы не сливались
    const auto dup = std::ranges::count_if(model_.edges, [&](const Edge& e) {
        return (e.from == from && e.to == to) || (e.from == to && e.to == from);
    });
    Edge e;
    e.id = newId("e");
    e.from = std::string(from);
    e.to = std::string(to);
    e.fromPort = std::string(fromPort);
    e.toPort = std::string(toPort);
    if (dup > 0) e.curve = (dup % 2 ? 1.0 : -1.0) * 0.2 * std::ceil(static_cast<double>(dup) / 2);
    model_.edges.push_back(std::move(e));
    return &model_.edges.back();
}

void Document::removeNode(std::string_view id) {
    if (!model_.node(id)) return;
    checkpoint();
    const std::string nid(id);
    std::vector<std::string> goneEdges;
    for (const auto& e : model_.edges)
        if (e.from == nid || e.to == nid) goneEdges.push_back(e.id);
    std::erase_if(model_.nodes, [&](const Node& n) { return n.id == nid; });
    std::erase_if(model_.edges, [&](const Edge& e) { return e.from == nid || e.to == nid; });
    std::erase_if(model_.scenario.steps, [&](const Step& s) {
        const bool refsNode = s.nodeId == nid || s.from == nid || s.to == nid;
        const bool deadLink = s.type == StepType::Link && std::ranges::contains(goneEdges, s.edgeId);
        return refsNode || deadLink;
    });
    for (auto& s : model_.scenario.steps)
        if (std::ranges::contains(goneEdges, s.edgeId)) s.edgeId.clear();
}

void Document::removeEdge(std::string_view id) {
    if (!model_.edge(id)) return;
    checkpoint();
    const std::string eid(id);
    std::erase_if(model_.edges, [&](const Edge& e) { return e.id == eid; });
    // «Соединение» без связи бессмысленно — удаляем; сообщение летит напрямую между узлами
    std::erase_if(model_.scenario.steps, [&](const Step& s) { return s.type == StepType::Link && s.edgeId == eid; });
    for (auto& s : model_.scenario.steps)
        if (s.edgeId == eid) s.edgeId.clear();
}

void Document::removeStep(std::string_view id) {
    if (!model_.step(id)) return;
    checkpoint();
    std::erase_if(model_.scenario.steps, [&](const Step& s) { return s.id == id; });
    updateDuration();
}

// ---- ports / waypoints ------------------------------------------------------

Port* Document::addPort(std::string_view nodeId, std::optional<Vec2> offset) {
    Node* n = model_.node(nodeId);
    if (!n) return nullptr;
    checkpoint();
    n = model_.node(nodeId);
    const Vec2 o = offset.value_or(Vec2{n->w, n->h / 2});
    n->ports.push_back({newId("p"), std::round(o.x), std::round(o.y)});
    return &n->ports.back();
}

void Document::removePort(std::string_view nodeId, std::string_view portId) {
    Node* n = model_.node(nodeId);
    if (!n || !n->port(portId)) return;
    checkpoint();
    n = model_.node(nodeId);
    std::erase_if(n->ports, [&](const Port& p) { return p.id == portId; });
    for (auto& e : model_.edges) {
        if (e.fromPort == portId) e.fromPort.clear();
        if (e.toPort == portId) e.toPort.clear();
    }
}

void Document::addWaypoint(std::string_view edgeId, Vec2 p, std::optional<std::size_t> index) {
    if (!model_.edge(edgeId)) return;
    checkpoint();
    Edge* e = model_.edge(edgeId);
    const Vec2 wp{std::round(p.x), std::round(p.y)};
    if (!index || *index > e->waypoints.size())
        e->waypoints.push_back(wp);
    else
        e->waypoints.insert(e->waypoints.begin() + static_cast<std::ptrdiff_t>(*index), wp);
}

void Document::removeWaypoint(std::string_view edgeId, std::size_t index) {
    const Edge* e = model_.edge(edgeId);
    if (!e || index >= e->waypoints.size()) return;
    checkpoint();
    Edge* m = model_.edge(edgeId);
    m->waypoints.erase(m->waypoints.begin() + static_cast<std::ptrdiff_t>(index));
}

void Document::reverseEdge(std::string_view edgeId) {
    if (!model_.edge(edgeId)) return;
    checkpoint();
    Edge* e = model_.edge(edgeId);
    std::swap(e->from, e->to);
    std::swap(e->fromPort, e->toPort);
    e->curve = -e->curve;
    std::ranges::reverse(e->waypoints);
}

// ---- steps ------------------------------------------------------------------

Step& Document::addStep(Step s) {
    checkpoint();
    if (s.id.empty() || model_.step(s.id)) s.id = newId("s");
    s.start = std::max(0.0, s.start);
    s.duration = std::max(kMinStepDuration, s.duration);
    const std::string id = s.id;
    model_.scenario.steps.push_back(std::move(s));
    sortSteps();
    updateDuration();
    return *model_.step(id);
}

Step* Document::duplicateStep(std::string_view id) {
    const Step* src = model_.step(id);
    if (!src) return nullptr;
    Step copy = *src;
    copy.id.clear();
    copy.start = src->end() + 200;
    return &addStep(std::move(copy));
}

std::optional<Step> Document::makeDefaultStep(StepType type, double at) const {
    const auto& nodes = model_.nodes;
    Step s;
    s.type = type;
    s.start = std::round(std::max(0.0, at) / 50) * 50;
    s.duration = 1000;
    switch (type) {
        case StepType::Message: {
            if (nodes.empty()) return std::nullopt;
            s.from = nodes[0].id;
            s.to = nodes.size() > 1 ? nodes[1].id : nodes[0].id;
            if (const Edge* e = model_.edgeBetween(s.from, s.to)) {
                s.edgeId = e->id;
                s.from = e->from;
                s.to = e->to;
            }
            s.variant = "request";
            s.duration = 1100;
            break;
        }
        case StepType::Timer:
            if (nodes.empty()) return std::nullopt;
            s.nodeId = nodes[0].id;
            s.seconds = 5;
            s.unit = "s";
            s.duration = 5000;
            s.label = "timeout";
            break;
        case StepType::State:
            if (nodes.empty()) return std::nullopt;
            s.nodeId = nodes[0].id;
            s.state = "down";
            s.duration = 3000;
            break;
        case StepType::Note:
            s.text = "Заметка";
            s.x = 60;
            s.y = 30;
            s.duration = 2500;
            break;
        case StepType::Pulse:
            if (nodes.empty()) return std::nullopt;
            s.nodeId = nodes[0].id;
            s.duration = 700;
            break;
        case StepType::Action:
            if (nodes.empty()) return std::nullopt;
            s.nodeId = nodes[0].id;
            s.text = "Обработка";
            s.color = "#38bdf8";
            s.duration = 1800;
            break;
        case StepType::Link:
            if (model_.edges.empty()) return std::nullopt;
            s.edgeId = model_.edges[0].id;
            s.text = "Соединение";
            s.color = "#38bdf8";
            s.anim = "flow";
            s.duration = 2000;
            break;
    }
    return s;
}

void Document::normalizeStep(Step& s) const {
    const auto& nodes = model_.nodes;
    const std::string first = nodes.empty() ? std::string{} : nodes[0].id;
    auto pickNode = [&] { return !s.nodeId.empty() ? s.nodeId : !s.from.empty() ? s.from : first; };
    switch (s.type) {
        case StepType::Message:
            if (s.from.empty()) s.from = !s.nodeId.empty() ? s.nodeId : first;
            if (s.to.empty()) s.to = nodes.size() > 1 ? nodes[1].id : s.from;
            if (s.variant.empty()) s.variant = "request";
            if (!model_.edge(s.edgeId)) {
                s.edgeId.clear();
                if (const Edge* e = model_.edgeBetween(s.from, s.to)) s.edgeId = e->id;
            }
            break;
        case StepType::Timer:
            s.nodeId = pickNode();
            if (s.seconds <= 0) s.seconds = std::round(s.duration / 1000);
            if (s.unit.empty()) s.unit = "s";
            break;
        case StepType::State:
            s.nodeId = pickNode();
            if (s.state.empty()) s.state = "down";
            break;
        case StepType::Pulse:
            s.nodeId = pickNode();
            break;
        case StepType::Action:
            s.nodeId = pickNode();
            if (s.text.empty()) s.text = "Действие";
            break;
        case StepType::Link:
            if (s.text.empty()) s.text = "Соединение";
            if (s.anim.empty()) s.anim = "flow";
            if (s.color.empty()) s.color = "#38bdf8";
            if (!model_.edge(s.edgeId)) s.edgeId = model_.edges.empty() ? std::string{} : model_.edges[0].id;
            break;
        case StepType::Note:
            if (s.text.empty()) s.text = "Заметка";
            if (s.x == 0 && s.y == 0) {
                s.x = 60;
                s.y = 30;
            }
            break;
    }
}

void Document::setDuration(double ms) {
    checkpoint("scenario:duration");
    model_.scenario.duration = std::max(1000.0, ms);
    model_.scenario.userDuration = true;
}

void Document::sortSteps() {
    std::ranges::stable_sort(model_.scenario.steps, {}, &Step::start);
}

void Document::updateDuration() { model_.scenario.duration = autoDuration(model_.scenario); }

}  // namespace ad
