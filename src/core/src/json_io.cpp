#include "ad/json_io.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include <nlohmann/json.hpp>

#include "ad/document.hpp"

namespace ad {

using nlohmann::json;
using ojson = nlohmann::ordered_json;  // запись — в порядке полей, для читаемости файлов

namespace {

// ---- терпимые геттеры: неверный тип поля = значение по умолчанию ------------

double num(const json& j, const char* key, double def) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return def;
    const double v = it->get<double>();
    return std::isfinite(v) ? v : def;
}

std::optional<double> optNum(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return std::nullopt;
    const double v = it->get<double>();
    return std::isfinite(v) ? std::optional{v} : std::nullopt;
}

std::string str(const json& j, const char* key, std::string def = {}) {
    const auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    return def;
}

bool boolean(const json& j, const char* key, bool def = false) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}

const json& arr(const json& j, const char* key) {
    static const json empty = json::array();
    const auto it = j.find(key);
    return it != j.end() && it->is_array() ? *it : empty;
}

const json& obj(const json& j, const char* key) {
    static const json empty = json::object();
    const auto it = j.find(key);
    return it != j.end() && it->is_object() ? *it : empty;
}

Node readNode(const json& j) {
    Node n;
    n.id = str(j, "id");
    n.label = str(j, "label");
    n.kind = str(j, "kind", "service");
    n.color = str(j, "color");
    n.shape = str(j, "shape");
    n.subtitle = str(j, "subtitle");
    n.x = num(j, "x", 0);
    n.y = num(j, "y", 0);
    n.w = num(j, "w", kDefaultNodeW);
    n.h = num(j, "h", kDefaultNodeH);
    for (const auto& p : arr(j, "ports")) {
        if (!p.is_object()) continue;
        n.ports.push_back({str(p, "id"), num(p, "dx", 0), num(p, "dy", 0)});
    }
    return n;
}

Edge readEdge(const json& j) {
    Edge e;
    e.id = str(j, "id");
    e.from = str(j, "from");
    e.to = str(j, "to");
    e.fromPort = str(j, "fromPort");
    e.toPort = str(j, "toPort");
    e.label = str(j, "label");
    e.style = str(j, "style", "solid");
    e.curve = num(j, "curve", 0);
    e.bidirectional = boolean(j, "bidirectional");
    e.labelPos = optNum(j, "labelPos");
    e.labelOff = optNum(j, "labelOff");
    e.labelSize = optNum(j, "labelSize");
    for (const auto& w : arr(j, "waypoints"))
        if (w.is_object()) e.waypoints.push_back({num(w, "x", 0), num(w, "y", 0)});
    return e;
}

std::optional<Step> readStep(const json& j) {
    const auto type = stepTypeFromString(str(j, "type"));
    if (!type) return std::nullopt;
    Step s;
    s.type = *type;
    s.id = str(j, "id");
    s.start = num(j, "start", 0);
    s.duration = num(j, "duration", 1200);
    s.from = str(j, "from");
    s.to = str(j, "to");
    s.edgeId = str(j, "edgeId");
    s.variant = str(j, "variant", "request");
    s.nodeId = str(j, "nodeId");
    s.label = str(j, "label");
    s.text = str(j, "text");
    s.color = str(j, "color");
    s.seconds = num(j, "seconds", 0);
    s.unit = str(j, "unit", "s");
    s.state = str(j, "state", "down");
    s.labelSize = optNum(j, "labelSize");
    s.labelPos = optNum(j, "labelPos");
    s.labelOff = optNum(j, "labelOff");
    s.x = num(j, "x", 0);
    s.y = num(j, "y", 0);
    s.anim = str(j, "anim", "flow");
    return s;
}

void putOpt(ojson& j, const char* key, const std::optional<double>& v) {
    if (v) j[key] = *v;
}

void putStr(ojson& j, const char* key, const std::string& v) {
    if (!v.empty()) j[key] = v;
}

ojson writeStep(const Step& s) {
    ojson j;
    j["id"] = s.id;
    j["type"] = std::string(toString(s.type));
    j["start"] = s.start;
    j["duration"] = s.duration;
    switch (s.type) {
        case StepType::Message:
            j["from"] = s.from;
            j["to"] = s.to;
            putStr(j, "edgeId", s.edgeId);
            j["variant"] = s.variant;
            j["label"] = s.label;
            break;
        case StepType::Timer:
            j["nodeId"] = s.nodeId;
            j["seconds"] = s.seconds;
            j["unit"] = s.unit;
            j["label"] = s.label;
            break;
        case StepType::State:
            j["nodeId"] = s.nodeId;
            j["state"] = s.state;
            putStr(j, "label", s.label);
            putStr(j, "color", s.color);
            putOpt(j, "labelSize", s.labelSize);
            break;
        case StepType::Action:
            j["nodeId"] = s.nodeId;
            j["text"] = s.text;
            putStr(j, "color", s.color);
            break;
        case StepType::Link:
            j["edgeId"] = s.edgeId;
            j["text"] = s.text;
            putStr(j, "color", s.color);
            j["anim"] = s.anim;
            putOpt(j, "labelPos", s.labelPos);
            putOpt(j, "labelOff", s.labelOff);
            putOpt(j, "labelSize", s.labelSize);
            break;
        case StepType::Note:
            j["text"] = s.text;
            j["x"] = s.x;
            j["y"] = s.y;
            putStr(j, "color", s.color);
            break;
        case StepType::Pulse:
            j["nodeId"] = s.nodeId;
            putStr(j, "color", s.color);
            break;
    }
    return j;
}

}  // namespace

void normalizeModel(Model& m) {
    IdGenerator ids;
    std::set<std::string> seen;
    auto uniqueId = [&](std::string& id, std::string_view prefix) {
        if (id.empty() || seen.contains(id)) id = ids.next(prefix, m);
        seen.insert(id);
    };

    if (m.meta.name.empty()) m.meta.name = "Без названия";
    if (!(m.view.zoom > 0)) m.view.zoom = 1;
    m.view.zoom = std::clamp(m.view.zoom, 0.25, 3.0);

    for (auto& n : m.nodes) {
        uniqueId(n.id, "n");
        const auto& k = nodeKind(n.kind);
        if (n.kind != k.id) n.kind = std::string(k.id);
        if (n.w < 20) n.w = kDefaultNodeW;
        if (n.h < 20) n.h = kDefaultNodeH;
        if (!Color::parse(n.color)) n.color = k.color.hex();
        if (n.shape.empty()) n.shape = std::string(k.shape);
        for (auto& p : n.ports) uniqueId(p.id, "p");
    }

    std::erase_if(m.edges, [&](const Edge& e) { return e.from == e.to || !m.node(e.from) || !m.node(e.to); });
    for (auto& e : m.edges) {
        uniqueId(e.id, "e");
        if (!m.port(e.from, e.fromPort)) e.fromPort.clear();
        if (!m.port(e.to, e.toPort)) e.toPort.clear();
        if (e.style != "dashed") e.style = "solid";
    }

    auto& steps = m.scenario.steps;
    std::erase_if(steps, [&](const Step& s) {
        switch (s.type) {
            case StepType::Message: return !m.node(s.from) || !m.node(s.to);
            case StepType::Link: return !m.edge(s.edgeId);
            case StepType::Note: return false;
            default: return !m.node(s.nodeId);
        }
    });
    for (auto& s : steps) {
        uniqueId(s.id, "s");
        s.start = std::max(0.0, s.start);
        s.duration = std::max(1.0, s.duration);
        if (!m.edge(s.edgeId)) s.edgeId.clear();
    }
    std::ranges::stable_sort(steps, {}, &Step::start);

    if (!(m.scenario.duration >= 1000)) m.scenario.duration = autoDuration(m.scenario);
}

std::expected<Model, std::string> parseModel(std::string_view text) {
    const json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::unexpected(std::string("Файл не является корректным JSON"));
    if (!j.is_object() || !j.contains("nodes") || !j.contains("scenario"))
        return std::unexpected(std::string("Некорректный формат файла: нет nodes/scenario"));

    Model m;
    const json& meta = obj(j, "meta");
    m.meta.name = str(meta, "name", m.meta.name);
    m.meta.createdAt = static_cast<std::int64_t>(num(meta, "createdAt", 0));

    const json& view = obj(j, "view");
    m.view = {num(view, "zoom", 1), num(view, "panX", 0), num(view, "panY", 0)};

    for (const auto& n : arr(j, "nodes"))
        if (n.is_object()) m.nodes.push_back(readNode(n));
    for (const auto& e : arr(j, "edges"))
        if (e.is_object()) m.edges.push_back(readEdge(e));

    const json& sc = obj(j, "scenario");
    m.scenario.duration = num(sc, "duration", 0);
    m.scenario.userDuration = boolean(sc, "userDuration");
    for (const auto& s : arr(sc, "steps"))
        if (s.is_object())
            if (auto step = readStep(s)) m.scenario.steps.push_back(std::move(*step));

    normalizeModel(m);
    return m;
}

std::string serializeModel(const Model& m, int indent) {
    ojson j;
    j["version"] = m.version;
    j["meta"] = {{"name", m.meta.name}, {"createdAt", m.meta.createdAt}};
    j["view"] = {{"zoom", m.view.zoom}, {"panX", m.view.panX}, {"panY", m.view.panY}};

    ojson nodes = ojson::array();
    for (const auto& n : m.nodes) {
        ojson jn = {{"id", n.id},   {"label", n.label}, {"kind", n.kind},   {"x", n.x},         {"y", n.y},
                   {"w", n.w},     {"h", n.h},         {"color", n.color}, {"shape", n.shape}, {"subtitle", n.subtitle}};
        if (!n.ports.empty()) {
            ojson ports = ojson::array();
            for (const auto& p : n.ports) ports.push_back({{"id", p.id}, {"dx", p.dx}, {"dy", p.dy}});
            jn["ports"] = std::move(ports);
        }
        nodes.push_back(std::move(jn));
    }
    j["nodes"] = std::move(nodes);

    ojson edges = ojson::array();
    for (const auto& e : m.edges) {
        ojson je = {{"id", e.id},       {"from", e.from},   {"to", e.to},
                   {"label", e.label}, {"style", e.style}, {"curve", e.curve},
                   {"bidirectional", e.bidirectional}};
        je["fromPort"] = e.fromPort.empty() ? ojson(nullptr) : ojson(e.fromPort);
        je["toPort"] = e.toPort.empty() ? ojson(nullptr) : ojson(e.toPort);
        ojson wps = ojson::array();
        for (const auto& w : e.waypoints) wps.push_back({{"x", w.x}, {"y", w.y}});
        je["waypoints"] = std::move(wps);
        putOpt(je, "labelPos", e.labelPos);
        putOpt(je, "labelOff", e.labelOff);
        putOpt(je, "labelSize", e.labelSize);
        edges.push_back(std::move(je));
    }
    j["edges"] = std::move(edges);

    ojson steps = ojson::array();
    for (const auto& s : m.scenario.steps) steps.push_back(writeStep(s));
    j["scenario"] = {{"duration", m.scenario.duration}, {"steps", std::move(steps)}};
    if (m.scenario.userDuration) j["scenario"]["userDuration"] = true;

    return j.dump(indent, ' ', false, ojson::error_handler_t::replace);
}

}  // namespace ad
