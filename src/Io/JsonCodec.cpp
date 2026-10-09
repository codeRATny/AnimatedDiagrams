#include "JsonCodec.hpp"

#include <algorithm>
#include <cmath>

namespace ad::json
{

namespace
{

// ---------------------------------------------------------------------------
// Tolerant getters: a wrong field type means "use the default"
// ---------------------------------------------------------------------------

std::optional<double> OptNum(const Json &j, const char *key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number())
    {
        return std::nullopt;
    }
    const double v = it->get<double>();
    return std::isfinite(v) ? std::optional{v} : std::nullopt;
}

double Num(const Json &j, const char *key, double def) { return OptNum(j, key).value_or(def); }

/// Number limited to [lo, hi] (documents and MCP agents may pass anything, e.g. 1e308).
double Num(const Json &j, const char *key, double def, double lo, double hi) { return std::clamp(Num(j, key, def), lo, hi); }

int Int(const Json &j, const char *key, int def, int lo, int hi) { return static_cast<int>(std::lround(Num(j, key, def, lo, hi))); }

constexpr double kMaxTimeMs = 24.0 * 3600 * 1000; // a day: far beyond any animation

std::optional<std::string> OptStr(const Json &j, const char *key)
{
    const auto it = j.find(key);
    if (it == j.end())
    {
        return std::nullopt;
    }
    if (it->is_string())
    {
        return it->get<std::string>();
    }
    if (it->is_number_integer())
    {
        return std::to_string(it->get<int64_t>());
    }
    return std::nullopt;
}

std::string Str(const Json &j, const char *key, std::string def = {}) { return OptStr(j, key).value_or(std::move(def)); }

std::optional<bool> OptBool(const Json &j, const char *key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_boolean())
    {
        return std::nullopt;
    }
    return it->get<bool>();
}

bool Bool(const Json &j, const char *key, bool def = false) { return OptBool(j, key).value_or(def); }

const Json &Arr(const Json &j, const char *key)
{
    static const Json kEmpty = Json::array();
    const auto        it     = j.find(key);
    return it != j.end() && it->is_array() ? *it : kEmpty;
}

const Json &Obj(const Json &j, const char *key)
{
    static const Json kEmpty = Json::object();
    const auto        it     = j.find(key);
    return it != j.end() && it->is_object() ? *it : kEmpty;
}

template <class T>
void Put(OrderedJson &j, const char *key, const std::optional<T> &v)
{
    if (v.has_value())
    {
        j[key] = *v;
    }
}

void PutStr(OrderedJson &j, const char *key, const std::string &v)
{
    if (!v.empty())
    {
        j[key] = v;
    }
}

void Warn(Warnings *w, std::string msg)
{
    if (w != nullptr)
    {
        w->push_back(std::move(msg));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Readers
// ---------------------------------------------------------------------------

NodeStyle NodeStyleFromJson(const Json &j)
{
    NodeStyle s;
    if (!j.is_object())
    {
        return s;
    }
    s.shape         = OptStr(j, "shape");
    s.custom_path   = OptStr(j, "customPath");
    s.fill          = OptStr(j, "fill");
    s.stroke        = OptStr(j, "stroke");
    s.stroke_width  = OptNum(j, "strokeWidth");
    s.stroke_style  = OptStr(j, "strokeStyle");
    s.corner_radius = OptNum(j, "cornerRadius");
    s.text_color    = OptStr(j, "textColor");
    s.font_size     = OptNum(j, "fontSize");
    s.opacity       = OptNum(j, "opacity");
    s.shadow        = OptBool(j, "shadow");
    s.show_icon     = OptBool(j, "showIcon");
    return s;
}

EdgeStyle EdgeStyleFromJson(const Json &j)
{
    EdgeStyle s;
    if (!j.is_object())
    {
        return s;
    }
    s.color        = OptStr(j, "color");
    s.width        = OptNum(j, "width");
    s.stroke_style = OptStr(j, "strokeStyle");
    s.arrow_end    = OptStr(j, "arrowEnd");
    s.arrow_start  = OptStr(j, "arrowStart");
    s.routing      = OptStr(j, "routing");
    s.label_color  = OptStr(j, "labelColor");
    return s;
}

Node NodeFromJson(const Json &j)
{
    Node n;
    n.id       = Str(j, "id");
    n.label    = Str(j, "label");
    n.type     = Str(j, "kind", kDefaultType);
    n.subtitle = Str(j, "subtitle");
    // v1/v2 stored the accent stripe color as "color"
    n.accent = OptStr(j, "accent").value_or(Str(j, "color"));
    n.x      = Num(j, "x", 0);
    n.y      = Num(j, "y", 0);
    n.w      = Num(j, "w", kDefaultNodeW);
    n.h      = Num(j, "h", kDefaultNodeH);
    n.style  = NodeStyleFromJson(Obj(j, "style"));
    for (const auto &p : Arr(j, "ports"))
    {
        if (p.is_object())
        {
            n.ports.push_back({Str(p, "id"), Num(p, "dx", 0), Num(p, "dy", 0)});
        }
    }
    return n;
}

Edge EdgeFromJson(const Json &j)
{
    Edge e;
    e.id         = Str(j, "id");
    e.from       = Str(j, "from");
    e.to         = Str(j, "to");
    e.from_port  = Str(j, "fromPort");
    e.to_port    = Str(j, "toPort");
    e.label      = Str(j, "label");
    e.curve      = Num(j, "curve", 0);
    e.label_pos  = OptNum(j, "labelPos");
    e.label_off  = OptNum(j, "labelOff");
    e.label_size = OptNum(j, "labelSize");
    for (const auto &w : Arr(j, "waypoints"))
    {
        if (w.is_object())
        {
            e.waypoints.push_back({Num(w, "x", 0), Num(w, "y", 0)});
        }
    }
    if (const auto it = j.find("style"); it != j.end())
    {
        if (it->is_string()) // v1/v2: "solid" | "dashed"
        {
            if (it->get<std::string>() == "dashed")
            {
                e.style.stroke_style = "dashed";
            }
        }
        else
        {
            e.style = EdgeStyleFromJson(*it);
        }
    }
    if (Bool(j, "bidirectional") && !e.style.arrow_start.has_value())
    {
        e.style.arrow_start = "triangle";
    }
    return e;
}

std::optional<Step> StepFromJson(const Json &j)
{
    if (!j.is_object())
    {
        return std::nullopt;
    }
    const std::string raw_type = Str(j, "type");
    const auto        type     = StepTypeFromString(raw_type);
    if (!type.has_value())
    {
        return std::nullopt;
    }
    Step s;
    s.type         = *type;
    s.id           = Str(j, "id");
    s.start        = Num(j, "start", 0, 0, kMaxTimeMs);
    s.duration     = Num(j, "duration", 1200, 0, kMaxTimeMs);
    s.from         = Str(j, "from");
    s.to           = Str(j, "to");
    s.edge_id      = Str(j, "edgeId");
    s.variant      = Str(j, "variant", "request");
    s.packet       = Str(j, "packet", "capsule");
    s.packet_size  = Num(j, "packetSize", 1);
    s.packet_count = Int(j, "packetCount", 1, 1, 1000);
    s.easing       = EasingFromString(Str(j, "easing", "ease-in-out")).value_or(Easing::EaseInOut);
    s.trail        = Bool(j, "trail", true);
    s.node_id      = Str(j, "nodeId");
    s.label        = Str(j, "label");
    s.text         = Str(j, "text");
    s.color        = Str(j, "color");
    s.seconds      = Num(j, "seconds", 0);
    s.unit         = Str(j, "unit", "s");
    s.state        = Str(j, "state", "down");
    s.label_size   = OptNum(j, "labelSize");
    s.label_pos    = OptNum(j, "labelPos");
    s.label_off    = OptNum(j, "labelOff");
    s.x            = Num(j, "x", 0);
    s.y            = Num(j, "y", 0);
    s.anim         = Str(j, "anim", "flow");
    s.effect       = Str(j, "effect", "pulse");
    s.intensity    = Num(j, "intensity", 1);
    s.repeat       = Int(j, "repeat", 0, 0, 1000);
    return s;
}

std::optional<Marker> MarkerFromJson(const Json &j)
{
    if (!j.is_object())
    {
        return std::nullopt;
    }
    const auto time = OptNum(j, "time");
    if (!time.has_value())
    {
        return std::nullopt;
    }
    return Marker{.id = Str(j, "id"), .time = std::clamp(*time, 0.0, kMaxTimeMs), .label = Str(j, "label")};
}

std::optional<ElementType> ElementFromJson(const Json &j, Warnings *warnings)
{
    if (!j.is_object() || Str(j, "id").empty())
    {
        Warn(warnings, "element without id skipped");
        return std::nullopt;
    }
    ElementType e;
    e.id          = Str(j, "id");
    e.label       = Str(j, "label", e.id);
    e.icon        = Str(j, "icon");
    e.category    = Str(j, "category", e.category);
    e.description = Str(j, "description");
    e.accent      = Str(j, "accent", e.accent);
    e.width       = std::max(20.0, Num(j, "width", e.width));
    e.height      = std::max(20.0, Num(j, "height", e.height));
    e.style       = NodeStyleFromJson(Obj(j, "style"));
    return e;
}

std::optional<EffectDef> EffectFromJson(const Json &j, Warnings *warnings)
{
    if (!j.is_object() || Str(j, "id").empty())
    {
        Warn(warnings, "effect without id skipped");
        return std::nullopt;
    }
    EffectDef e;
    e.id          = Str(j, "id");
    e.label       = Str(j, "label", e.id);
    e.category    = Str(j, "category", e.category);
    e.description = Str(j, "description");
    e.color       = Str(j, "color", e.color);
    e.repeat      = Int(j, "repeat", 1, 1, 100);
    for (const auto &t : Arr(j, "tracks"))
    {
        const auto prop = EffectPropertyFromString(Str(t, "property"));
        if (!prop.has_value())
        {
            Warn(warnings, "effect '" + e.id + "': unknown track property '" + Str(t, "property") + "'");
            continue;
        }
        EffectTrack track;
        track.property = *prop;
        for (const auto &k : Arr(t, "keys"))
        {
            Keyframe kf;
            kf.t      = std::clamp(Num(k, "t", 0), 0.0, 1.0);
            kf.value  = Num(k, "value", DefaultValue(*prop));
            kf.easing = EasingFromString(Str(k, "easing", "linear")).value_or(Easing::Linear);
            track.keys.push_back(kf);
        }
        std::ranges::stable_sort(track.keys, {}, &Keyframe::t);
        e.tracks.push_back(std::move(track));
    }
    return e;
}

std::optional<AnimationTemplate> AnimationFromJson(const Json &j, Warnings *warnings)
{
    if (!j.is_object() || Str(j, "id").empty())
    {
        Warn(warnings, "animation without id skipped");
        return std::nullopt;
    }
    AnimationTemplate a;
    a.id          = Str(j, "id");
    a.label       = Str(j, "label", a.id);
    a.category    = Str(j, "category", a.category);
    a.description = Str(j, "description");
    for (const auto &r : Arr(j, "roles"))
    {
        if (!Str(r, "id").empty())
        {
            a.roles.push_back({Str(r, "id"), Str(r, "label", Str(r, "id"))});
        }
    }
    for (const auto &s : Arr(j, "steps"))
    {
        if (auto step = StepFromJson(s); step.has_value())
        {
            a.steps.push_back(std::move(*step));
        }
        else
        {
            Warn(warnings, "animation '" + a.id + "': invalid step skipped");
        }
    }
    return a;
}

std::optional<DesignSystem> DesignSystemFromJson(const Json &j, Warnings *warnings)
{
    if (!j.is_object() || Str(j, "id").empty())
    {
        Warn(warnings, "design system without id skipped");
        return std::nullopt;
    }
    DesignSystem d;
    d.id             = Str(j, "id");
    d.label          = Str(j, "label", d.id);
    d.category       = Str(j, "category", d.category);
    d.description    = Str(j, "description");
    d.background     = OptStr(j, "background");
    d.edge_color     = OptStr(j, "edgeColor");
    d.text_color     = OptStr(j, "textColor");
    d.grid           = OptBool(j, "grid");
    d.grid_size      = OptNum(j, "gridSize");
    d.font_family    = Str(j, "fontFamily");
    d.subtitle_color = OptStr(j, "subtitleColor");
    for (const auto &[k, v] : Obj(j, "colors").items())
    {
        if (v.is_string())
        {
            d.colors[k] = v.get<std::string>();
        }
    }
    for (const auto &[k, v] : Obj(j, "states").items())
    {
        if (v.is_object())
        {
            d.states[k] = StateColors{OptStr(v, "fill"), OptStr(v, "ring")};
        }
    }
    for (const auto &[k, v] : Obj(j, "variants").items())
    {
        if (v.is_string())
        {
            d.variants[k] = v.get<std::string>();
        }
    }
    d.node = NodeStyleFromJson(Obj(j, "node"));
    d.edge = EdgeStyleFromJson(Obj(j, "edge"));
    for (const auto &[k, v] : Obj(j, "elements").items())
    {
        if (v.is_object())
        {
            d.elements[k] = ElementOverride{OptStr(v, "accent"), NodeStyleFromJson(Obj(v, "style"))};
        }
    }
    return d;
}

LibrarySet LibraryFromJson(const Json &j, Warnings *warnings)
{
    LibrarySet set;
    if (!j.is_object())
    {
        return set;
    }
    for (const auto &e : Arr(j, "elements"))
    {
        if (auto v = ElementFromJson(e, warnings); v.has_value())
        {
            set.Upsert(*v);
        }
    }
    for (const auto &e : Arr(j, "effects"))
    {
        if (auto v = EffectFromJson(e, warnings); v.has_value())
        {
            set.Upsert(*v);
        }
    }
    for (const auto &a : Arr(j, "animations"))
    {
        if (auto v = AnimationFromJson(a, warnings); v.has_value())
        {
            set.Upsert(*v);
        }
    }
    for (const auto &d : Arr(j, "designSystems"))
    {
        if (auto v = DesignSystemFromJson(d, warnings); v.has_value())
        {
            set.Upsert(*v);
        }
    }
    return set;
}

Model ModelFromJson(const Json &j)
{
    Model       m;
    const Json &meta   = Obj(j, "meta");
    m.meta.name        = Str(meta, "name", m.meta.name);
    m.meta.description = Str(meta, "description");
    m.meta.created_at  = static_cast<int64_t>(Num(meta, "createdAt", 0));
    const Json &view   = Obj(j, "view");
    m.view             = {Num(view, "zoom", 1), Num(view, "panX", 0), Num(view, "panY", 0)};
    const Json &scene  = Obj(j, "scene");
    m.scene.background = Str(scene, "background", m.scene.background);
    m.scene.grid       = Bool(scene, "grid", m.scene.grid);
    m.scene.grid_size  = Num(scene, "gridSize", m.scene.grid_size);
    m.scene.edge_color = Str(scene, "edgeColor", m.scene.edge_color);
    m.scene.text_color = Str(scene, "textColor", m.scene.text_color);
    for (const auto &n : Arr(j, "nodes"))
    {
        if (n.is_object())
        {
            m.nodes.push_back(NodeFromJson(n));
        }
    }
    for (const auto &e : Arr(j, "edges"))
    {
        if (e.is_object())
        {
            m.edges.push_back(EdgeFromJson(e));
        }
    }
    const Json &sc           = Obj(j, "scenario");
    m.scenario.duration      = Num(sc, "duration", 0, 0, kMaxTimeMs);
    m.scenario.user_duration = Bool(sc, "userDuration");
    for (const auto &s : Arr(sc, "steps"))
    {
        if (auto step = StepFromJson(s); step.has_value())
        {
            m.scenario.steps.push_back(std::move(*step));
        }
    }
    for (const auto &mk : Arr(sc, "markers"))
    {
        if (auto marker = MarkerFromJson(mk); marker.has_value())
        {
            m.scenario.markers.push_back(std::move(*marker));
        }
    }
    m.library       = LibraryFromJson(Obj(j, "library"));
    m.design_system = Str(j, "designSystem");
    return m;
}

// ---------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------

OrderedJson ToJson(const NodeStyle &s)
{
    OrderedJson j = OrderedJson::object();
    Put(j, "shape", s.shape);
    Put(j, "customPath", s.custom_path);
    Put(j, "fill", s.fill);
    Put(j, "stroke", s.stroke);
    Put(j, "strokeWidth", s.stroke_width);
    Put(j, "strokeStyle", s.stroke_style);
    Put(j, "cornerRadius", s.corner_radius);
    Put(j, "textColor", s.text_color);
    Put(j, "fontSize", s.font_size);
    Put(j, "opacity", s.opacity);
    Put(j, "shadow", s.shadow);
    Put(j, "showIcon", s.show_icon);
    return j;
}

OrderedJson ToJson(const EdgeStyle &s)
{
    OrderedJson j = OrderedJson::object();
    Put(j, "color", s.color);
    Put(j, "width", s.width);
    Put(j, "strokeStyle", s.stroke_style);
    Put(j, "arrowEnd", s.arrow_end);
    Put(j, "arrowStart", s.arrow_start);
    Put(j, "routing", s.routing);
    Put(j, "labelColor", s.label_color);
    return j;
}

OrderedJson ToJson(const Node &n)
{
    OrderedJson j{{"id", n.id}, {"label", n.label}, {"kind", n.type}, {"x", n.x}, {"y", n.y}, {"w", n.w}, {"h", n.h}};
    PutStr(j, "subtitle", n.subtitle);
    PutStr(j, "accent", n.accent);
    if (!n.ports.empty())
    {
        OrderedJson ports = OrderedJson::array();
        for (const auto &p : n.ports)
        {
            ports.push_back({{"id", p.id}, {"dx", p.dx}, {"dy", p.dy}});
        }
        j["ports"] = std::move(ports);
    }
    if (!n.style.Empty())
    {
        j["style"] = ToJson(n.style);
    }
    return j;
}

OrderedJson ToJson(const Edge &e)
{
    OrderedJson j{{"id", e.id}, {"from", e.from}, {"to", e.to}};
    PutStr(j, "fromPort", e.from_port);
    PutStr(j, "toPort", e.to_port);
    PutStr(j, "label", e.label);
    if (e.curve != 0)
    {
        j["curve"] = e.curve;
    }
    if (!e.waypoints.empty())
    {
        OrderedJson wps = OrderedJson::array();
        for (const auto &w : e.waypoints)
        {
            wps.push_back({{"x", w.x}, {"y", w.y}});
        }
        j["waypoints"] = std::move(wps);
    }
    Put(j, "labelPos", e.label_pos);
    Put(j, "labelOff", e.label_off);
    Put(j, "labelSize", e.label_size);
    if (e.style != EdgeStyle{})
    {
        j["style"] = ToJson(e.style);
    }
    return j;
}

OrderedJson ToJson(const Step &s)
{
    OrderedJson j{{"id", s.id}, {"type", std::string(ToString(s.type))}, {"start", s.start}, {"duration", s.duration}};
    switch (s.type)
    {
    case StepType::Message:
    {
        j["from"] = s.from;
        j["to"]   = s.to;
        PutStr(j, "edgeId", s.edge_id);
        j["variant"] = s.variant;
        j["label"]   = s.label;
        PutStr(j, "color", s.color);
        const Step def;
        if (s.packet != def.packet)
        {
            j["packet"] = s.packet;
        }
        if (s.packet_size != def.packet_size)
        {
            j["packetSize"] = s.packet_size;
        }
        if (s.packet_count != def.packet_count)
        {
            j["packetCount"] = s.packet_count;
        }
        if (s.easing != def.easing)
        {
            j["easing"] = std::string(ToString(s.easing));
        }
        if (s.trail != def.trail)
        {
            j["trail"] = s.trail;
        }
        break;
    }
    case StepType::Timer:
        j["nodeId"]  = s.node_id;
        j["seconds"] = s.seconds;
        j["unit"]    = s.unit;
        j["label"]   = s.label;
        PutStr(j, "color", s.color);
        break;
    case StepType::State:
        j["nodeId"] = s.node_id;
        j["state"]  = s.state;
        PutStr(j, "label", s.label);
        PutStr(j, "color", s.color);
        Put(j, "labelSize", s.label_size);
        break;
    case StepType::Action:
        j["nodeId"] = s.node_id;
        j["text"]   = s.text;
        PutStr(j, "color", s.color);
        break;
    case StepType::Link:
        j["edgeId"] = s.edge_id;
        j["text"]   = s.text;
        PutStr(j, "color", s.color);
        j["anim"] = s.anim;
        Put(j, "labelPos", s.label_pos);
        Put(j, "labelOff", s.label_off);
        Put(j, "labelSize", s.label_size);
        break;
    case StepType::Note:
        j["text"] = s.text;
        j["x"]    = s.x;
        j["y"]    = s.y;
        PutStr(j, "color", s.color);
        break;
    case StepType::Effect:
        j["nodeId"] = s.node_id;
        j["effect"] = s.effect;
        PutStr(j, "color", s.color);
        if (s.intensity != 1)
        {
            j["intensity"] = s.intensity;
        }
        if (s.repeat != 0)
        {
            j["repeat"] = s.repeat;
        }
        break;
    }
    return j;
}

OrderedJson ToJson(const ElementType &e)
{
    OrderedJson j{{"id", e.id}, {"label", e.label}, {"icon", e.icon}, {"category", e.category}};
    PutStr(j, "description", e.description);
    j["accent"] = e.accent;
    j["width"]  = e.width;
    j["height"] = e.height;
    j["style"]  = ToJson(e.style);
    return j;
}

OrderedJson ToJson(const EffectDef &e)
{
    OrderedJson j{{"id", e.id}, {"label", e.label}, {"category", e.category}};
    PutStr(j, "description", e.description);
    j["color"]         = e.color;
    j["repeat"]        = e.repeat;
    OrderedJson tracks = OrderedJson::array();
    for (const auto &t : e.tracks)
    {
        OrderedJson keys = OrderedJson::array();
        for (const auto &k : t.keys)
        {
            OrderedJson kj{{"t", k.t}, {"value", k.value}};
            if (k.easing != Easing::Linear)
            {
                kj["easing"] = std::string(ToString(k.easing));
            }
            keys.push_back(std::move(kj));
        }
        tracks.push_back({{"property", std::string(ToString(t.property))}, {"keys", std::move(keys)}});
    }
    j["tracks"] = std::move(tracks);
    return j;
}

OrderedJson ToJson(const AnimationTemplate &a)
{
    OrderedJson j{{"id", a.id}, {"label", a.label}, {"category", a.category}};
    PutStr(j, "description", a.description);
    OrderedJson roles = OrderedJson::array();
    for (const auto &r : a.roles)
    {
        roles.push_back({{"id", r.id}, {"label", r.label}});
    }
    j["roles"]        = std::move(roles);
    OrderedJson steps = OrderedJson::array();
    for (const auto &s : a.steps)
    {
        steps.push_back(ToJson(s));
    }
    j["steps"] = std::move(steps);
    return j;
}

OrderedJson ToJson(const DesignSystem &d)
{
    OrderedJson j{{"id", d.id}, {"label", d.label}, {"category", d.category}};
    PutStr(j, "description", d.description);
    Put(j, "background", d.background);
    Put(j, "edgeColor", d.edge_color);
    Put(j, "textColor", d.text_color);
    Put(j, "grid", d.grid);
    Put(j, "gridSize", d.grid_size);
    PutStr(j, "fontFamily", d.font_family);
    Put(j, "subtitleColor", d.subtitle_color);
    if (!d.colors.empty())
    {
        OrderedJson colors = OrderedJson::object();
        for (const auto &[k, v] : d.colors)
        {
            colors[k] = v;
        }
        j["colors"] = std::move(colors);
    }
    if (!d.states.empty())
    {
        OrderedJson states = OrderedJson::object();
        for (const auto &[k, v] : d.states)
        {
            OrderedJson sj = OrderedJson::object();
            Put(sj, "fill", v.fill);
            Put(sj, "ring", v.ring);
            states[k] = std::move(sj);
        }
        j["states"] = std::move(states);
    }
    if (!d.variants.empty())
    {
        OrderedJson variants = OrderedJson::object();
        for (const auto &[k, v] : d.variants)
        {
            variants[k] = v;
        }
        j["variants"] = std::move(variants);
    }
    if (!d.node.Empty())
    {
        j["node"] = ToJson(d.node);
    }
    if (d.edge != EdgeStyle{})
    {
        j["edge"] = ToJson(d.edge);
    }
    if (!d.elements.empty())
    {
        OrderedJson elements = OrderedJson::object();
        for (const auto &[k, v] : d.elements)
        {
            OrderedJson ej = OrderedJson::object();
            Put(ej, "accent", v.accent);
            if (!v.style.Empty())
            {
                ej["style"] = ToJson(v.style);
            }
            elements[k] = std::move(ej);
        }
        j["elements"] = std::move(elements);
    }
    return j;
}

OrderedJson ToJson(const LibrarySet &l)
{
    OrderedJson j    = OrderedJson::object();
    auto        list = [](const auto &items)
    {
        OrderedJson arr = OrderedJson::array();
        for (const auto &it : items)
        {
            arr.push_back(ToJson(it));
        }
        return arr;
    };
    if (!l.elements.empty())
    {
        j["elements"] = list(l.elements);
    }
    if (!l.effects.empty())
    {
        j["effects"] = list(l.effects);
    }
    if (!l.animations.empty())
    {
        j["animations"] = list(l.animations);
    }
    if (!l.design_systems.empty())
    {
        j["designSystems"] = list(l.design_systems);
    }
    return j;
}

OrderedJson ToJson(const Marker &m) { return OrderedJson{{"id", m.id}, {"time", m.time}, {"label", m.label}}; }

OrderedJson ToJson(const Model &m)
{
    OrderedJson j;
    j["version"] = m.version;
    j["meta"]    = {{"name", m.meta.name}, {"createdAt", m.meta.created_at}};
    PutStr(j["meta"], "description", m.meta.description);
    j["view"] = {{"zoom", m.view.zoom}, {"panX", m.view.pan_x}, {"panY", m.view.pan_y}};
    if (m.scene != SceneSettings{})
    {
        j["scene"] = {{"background", m.scene.background},
                      {"grid", m.scene.grid},
                      {"gridSize", m.scene.grid_size},
                      {"edgeColor", m.scene.edge_color},
                      {"textColor", m.scene.text_color}};
    }
    OrderedJson nodes = OrderedJson::array();
    for (const auto &n : m.nodes)
    {
        nodes.push_back(ToJson(n));
    }
    j["nodes"]        = std::move(nodes);
    OrderedJson edges = OrderedJson::array();
    for (const auto &e : m.edges)
    {
        edges.push_back(ToJson(e));
    }
    j["edges"]        = std::move(edges);
    OrderedJson steps = OrderedJson::array();
    for (const auto &s : m.scenario.steps)
    {
        steps.push_back(ToJson(s));
    }
    j["scenario"] = {{"duration", m.scenario.duration}, {"steps", std::move(steps)}};
    if (m.scenario.user_duration)
    {
        j["scenario"]["userDuration"] = true;
    }
    if (!m.scenario.markers.empty())
    {
        OrderedJson markers = OrderedJson::array();
        for (const auto &mk : m.scenario.markers)
        {
            markers.push_back(ToJson(mk));
        }
        j["scenario"]["markers"] = std::move(markers);
    }
    PutStr(j, "designSystem", m.design_system);
    if (!m.library.Empty())
    {
        j["library"] = ToJson(m.library);
    }
    return j;
}

} // namespace ad::json
