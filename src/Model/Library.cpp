#include "Library.hpp"

#include <algorithm>
#include <array>

#include "Utils/I18n.hpp"

namespace ad
{

// ---------------------------------------------------------------------------
// NodeStyle
// ---------------------------------------------------------------------------

namespace
{

template <class T>
void Override(std::optional<T> &dst, const std::optional<T> &src)
{
    if (src.has_value())
    {
        dst = src;
    }
}

} // namespace

NodeStyle NodeStyle::Merged(const NodeStyle &over) const
{
    NodeStyle out = *this;
    Override(out.shape, over.shape);
    Override(out.custom_path, over.custom_path);
    Override(out.fill, over.fill);
    Override(out.stroke, over.stroke);
    Override(out.stroke_width, over.stroke_width);
    Override(out.stroke_style, over.stroke_style);
    Override(out.corner_radius, over.corner_radius);
    Override(out.text_color, over.text_color);
    Override(out.font_size, over.font_size);
    Override(out.opacity, over.opacity);
    Override(out.shadow, over.shadow);
    Override(out.show_icon, over.show_icon);
    return out;
}

bool NodeStyle::Empty() const { return *this == NodeStyle{}; }

EdgeStyle EdgeStyle::Merged(const EdgeStyle &over) const
{
    EdgeStyle out = *this;
    Override(out.color, over.color);
    Override(out.width, over.width);
    Override(out.stroke_style, over.stroke_style);
    Override(out.arrow_end, over.arrow_end);
    Override(out.arrow_start, over.arrow_start);
    Override(out.routing, over.routing);
    Override(out.label_color, over.label_color);
    return out;
}

// ---------------------------------------------------------------------------
// Design system tokens
// ---------------------------------------------------------------------------

const std::map<std::string, std::string> &DefaultTokens()
{
    static const std::map<std::string, std::string> kTokens{
        {"surface", "#3b4a63"}, {"surface-alt", "#1e293b"}, {"border", "#5a6b86"},    {"text", "#f2f6ff"},
        {"muted", "#b7c6e4"},   {"primary", "#4f8cff"},     {"secondary", "#a855f7"}, {"success", "#22c55e"},
        {"warning", "#f59e0b"}, {"danger", "#ef4444"},      {"info", "#38bdf8"},      {"accent", "#22d3ee"},
    };
    return kTokens;
}

std::string ResolveColorToken(std::string_view value, const DesignSystem *ds)
{
    if (!value.starts_with('$'))
    {
        return std::string(value);
    }
    const std::string name(value.substr(1));
    if (ds != nullptr)
    {
        if (const auto it = ds->colors.find(name); it != ds->colors.end())
        {
            return it->second;
        }
    }
    const auto &defs = DefaultTokens();
    const auto  it   = defs.find(name);
    return it != defs.end() ? it->second : std::string();
}

// ---------------------------------------------------------------------------
// EffectProperty
// ---------------------------------------------------------------------------

namespace
{

struct PropertyInfo
{
    EffectProperty   property;
    std::string_view id;
    double           def;
};

constexpr std::array kProperties{
    PropertyInfo{EffectProperty::Opacity, "opacity", 1},  PropertyInfo{EffectProperty::Scale, "scale", 1},
    PropertyInfo{EffectProperty::Rotate, "rotate", 0},    PropertyInfo{EffectProperty::OffsetX, "offset_x", 0},
    PropertyInfo{EffectProperty::OffsetY, "offset_y", 0}, PropertyInfo{EffectProperty::Glow, "glow", 0},
    PropertyInfo{EffectProperty::Tint, "tint", 0},
};

constexpr std::array kPropertyList{EffectProperty::Opacity, EffectProperty::Scale, EffectProperty::Rotate, EffectProperty::OffsetX,
                                   EffectProperty::OffsetY, EffectProperty::Glow,  EffectProperty::Tint};

} // namespace

std::string_view ToString(EffectProperty p) { return std::ranges::find(kProperties, p, &PropertyInfo::property)->id; }

std::optional<EffectProperty> EffectPropertyFromString(std::string_view s)
{
    const auto it = std::ranges::find(kProperties, s, &PropertyInfo::id);
    if (it == kProperties.end())
    {
        return std::nullopt;
    }
    return it->property;
}

double DefaultValue(EffectProperty p) { return std::ranges::find(kProperties, p, &PropertyInfo::property)->def; }

std::span<const EffectProperty> EffectProperties() { return kPropertyList; }

// ---------------------------------------------------------------------------
// LibrarySet
// ---------------------------------------------------------------------------

namespace
{

template <class Vec>
auto FindById(Vec &v, std::string_view id) -> decltype(&v.front())
{
    const auto it = std::ranges::find_if(v,
                                         [id](const auto &e)
                                         {
                                             return e.id == id;
                                         });
    return it != v.end() ? &*it : nullptr;
}

template <class Vec, class T>
void UpsertById(Vec &v, const T &item)
{
    if (auto *existing = FindById(v, item.id); existing != nullptr)
    {
        *existing = item;
        return;
    }
    v.push_back(item);
}

} // namespace

const ElementType       *LibrarySet::Element(std::string_view id) const { return FindById(elements, id); }
const EffectDef         *LibrarySet::Effect(std::string_view id) const { return FindById(effects, id); }
const AnimationTemplate *LibrarySet::Animation(std::string_view id) const { return FindById(animations, id); }
void                     LibrarySet::Upsert(const ElementType &e) { UpsertById(elements, e); }
void                     LibrarySet::Upsert(const EffectDef &e) { UpsertById(effects, e); }
void                     LibrarySet::Upsert(const AnimationTemplate &a) { UpsertById(animations, a); }
const DesignSystem      *LibrarySet::Design(std::string_view id) const { return FindById(design_systems, id); }
void                     LibrarySet::Upsert(const DesignSystem &d) { UpsertById(design_systems, d); }

// ---------------------------------------------------------------------------
// Built-in library
// ---------------------------------------------------------------------------

namespace
{

ElementType MakeElement(std::string id, std::string label, std::string icon, std::string category, std::string accent, std::string shape,
                        double w = 140, double h = 64)
{
    ElementType e;
    e.id          = std::move(id);
    e.label       = std::move(label);
    e.icon        = std::move(icon);
    e.category    = std::move(category);
    e.accent      = std::move(accent);
    e.width       = w;
    e.height      = h;
    e.style.shape = std::move(shape);
    return e;
}

EffectTrack Track(EffectProperty p, std::initializer_list<Keyframe> keys) { return {p, std::vector<Keyframe>(keys)}; }

EffectDef MakeEffect(std::string id, std::string label, std::string description, int repeat, std::vector<EffectTrack> tracks,
                     std::string color = "#22d3ee")
{
    EffectDef e;
    e.id          = std::move(id);
    e.label       = std::move(label);
    e.description = std::move(description);
    e.repeat      = repeat;
    e.tracks      = std::move(tracks);
    e.color       = std::move(color);
    return e;
}

Step Msg(std::string from, std::string to, std::string variant, std::string label, double start, double duration)
{
    Step s;
    s.type     = StepType::Message;
    s.from     = std::move(from);
    s.to       = std::move(to);
    s.variant  = std::move(variant);
    s.label    = std::move(label);
    s.start    = start;
    s.duration = duration;
    return s;
}

Step OnNode(StepType type, std::string node, double start, double duration)
{
    Step s;
    s.type     = type;
    s.node_id  = std::move(node);
    s.start    = start;
    s.duration = duration;
    return s;
}

Step Action(std::string node, std::string text, double start, double duration)
{
    Step s = OnNode(StepType::Action, std::move(node), start, duration);
    s.text = std::move(text);
    return s;
}

Step Timer(std::string node, double seconds, std::string label, double start, double duration)
{
    Step s    = OnNode(StepType::Timer, std::move(node), start, duration);
    s.seconds = seconds;
    s.label   = std::move(label);
    return s;
}

Step Effect(std::string node, std::string effect, double start, double duration)
{
    Step s   = OnNode(StepType::Effect, std::move(node), start, duration);
    s.effect = std::move(effect);
    return s;
}

Step State(std::string node, std::string state, double start, double duration)
{
    Step s  = OnNode(StepType::State, std::move(node), start, duration);
    s.state = std::move(state);
    return s;
}

AnimationTemplate MakeTemplate(std::string id, std::string label, std::string description, std::vector<AnimationRole> roles,
                               std::vector<Step> steps)
{
    AnimationTemplate a;
    a.id          = std::move(id);
    a.label       = std::move(label);
    a.description = std::move(description);
    a.category    = Tr("library", "Scenarios");
    a.roles       = std::move(roles);
    a.steps       = std::move(steps);
    for (size_t i = 0; i < a.steps.size(); ++i)
    {
        a.steps[i].id = "t" + std::to_string(i + 1);
    }
    return a;
}

std::vector<DesignSystem> MakeDesignSystems()
{
    std::vector<DesignSystem> out;

    DesignSystem dark;
    dark.id          = "dark";
    dark.label       = Tr("library", "Dark (default)");
    dark.category    = Tr("library", "Built-in");
    dark.description = Tr("library", "The original editor look: dark canvas, soft accents");
    dark.background  = "#0a111f";
    dark.edge_color  = "#5f7196";
    dark.text_color  = "#f2f6ff";
    dark.grid        = true;
    dark.grid_size   = 26;
    dark.colors      = DefaultTokens();
    out.push_back(dark);

    DesignSystem light;
    light.id               = "light";
    light.label            = Tr("library", "Light");
    light.category         = Tr("library", "Built-in");
    light.description      = Tr("library", "White canvas for documentation and presentations");
    light.background       = "#f6f8fc";
    light.edge_color       = "#94a3b8";
    light.text_color       = "#0f172a";
    light.grid             = true;
    light.grid_size        = 26;
    light.subtitle_color   = "#64748b";
    light.colors           = {{"surface", "#ffffff"}, {"surface-alt", "#f1f5f9"}, {"border", "#cbd5e1"},    {"text", "#0f172a"},
                              {"muted", "#64748b"},   {"primary", "#2563eb"},     {"secondary", "#7c3aed"}, {"success", "#16a34a"},
                              {"warning", "#d97706"}, {"danger", "#dc2626"},      {"info", "#0284c7"},      {"accent", "#0891b2"}};
    light.states           = {{"ok", {"#ffffff", "#cbd5e1"}},      {"active", {"#dbeafe", "#3b82f6"}}, {"busy", {"#fef3c7", "#f59e0b"}},
                              {"warn", {"#ffedd5", "#f97316"}},    {"down", {"#fee2e2", "#ef4444"}},   {"success", {"#dcfce7", "#22c55e"}},
                              {"disabled", {"#f1f5f9", "#cbd5e1"}}};
    light.variants         = {{"request", "#2563eb"}, {"response", "#059669"}, {"retry", "#d97706"},
                              {"error", "#dc2626"},   {"success", "#16a34a"},  {"event", "#9333ea"}};
    light.node.text_color  = "#0f172a";
    light.edge.label_color = "#475569";
    out.push_back(light);

    DesignSystem blueprint;
    blueprint.id             = "blueprint";
    blueprint.label          = Tr("library", "Blueprint");
    blueprint.category       = Tr("library", "Built-in");
    blueprint.description    = Tr("library", "Blue background, thin lines, monospaced font, orthogonal edges");
    blueprint.background     = "#0b3a6e";
    blueprint.edge_color     = "#cfe8ff";
    blueprint.text_color     = "#ffffff";
    blueprint.grid           = true;
    blueprint.grid_size      = 20;
    blueprint.font_family    = "DejaVu Sans Mono";
    blueprint.subtitle_color = "#a9d1ff";
    blueprint.colors         = {{"surface", "#0f4c8a"}, {"surface-alt", "#0b3a6e"}, {"border", "#e6f3ff"},    {"text", "#ffffff"},
                                {"muted", "#a9d1ff"},   {"primary", "#7cc4ff"},     {"secondary", "#c4b5fd"}, {"success", "#86efac"},
                                {"warning", "#fde68a"}, {"danger", "#fca5a5"},      {"info", "#7dd3fc"},      {"accent", "#ffffff"}};
    blueprint.states         = {{"ok", {"#0f4c8a", "#e6f3ff"}},      {"active", {"#1d6fbf", "#ffffff"}}, {"busy", {"#5b4a12", "#fde68a"}},
                                {"warn", {"#6b3a12", "#fdba74"}},    {"down", {"#6b1d1d", "#fca5a5"}},   {"success", {"#14532d", "#86efac"}},
                                {"disabled", {"#0b3a6e", "#7aa7d6"}}};
    blueprint.node.corner_radius = 2;
    blueprint.node.stroke_width  = 1.5;
    blueprint.node.shadow        = false;
    blueprint.edge.width         = 1.5;
    blueprint.edge.routing       = "orthogonal";
    blueprint.edge.arrow_end     = "open";
    blueprint.edge.label_color   = "#cfe8ff";
    out.push_back(blueprint);

    DesignSystem contrast;
    contrast.id                = "high-contrast";
    contrast.label             = Tr("library", "High contrast");
    contrast.category          = Tr("library", "Built-in");
    contrast.description       = Tr("library", "Maximum legibility: black background, thick white lines, bright states");
    contrast.background        = "#000000";
    contrast.edge_color        = "#ffffff";
    contrast.text_color        = "#ffffff";
    contrast.grid              = false;
    contrast.subtitle_color    = "#e5e5e5";
    contrast.colors            = {{"surface", "#000000"}, {"surface-alt", "#1a1a1a"}, {"border", "#ffffff"},    {"text", "#ffffff"},
                                  {"muted", "#e5e5e5"},   {"primary", "#4da3ff"},     {"secondary", "#d08cff"}, {"success", "#33ff77"},
                                  {"warning", "#ffd400"}, {"danger", "#ff3b3b"},      {"info", "#33ccff"},      {"accent", "#00ffff"}};
    contrast.states            = {{"ok", {"#000000", "#ffffff"}},      {"active", {"#003a8c", "#4da3ff"}}, {"busy", {"#5c4400", "#ffd400"}},
                                  {"warn", {"#663300", "#ff9900"}},    {"down", {"#7a0000", "#ff3b3b"}},   {"success", {"#004d1a", "#33ff77"}},
                                  {"disabled", {"#1a1a1a", "#808080"}}};
    contrast.variants          = {{"request", "#4da3ff"}, {"response", "#33ff77"}, {"retry", "#ffd400"},
                                  {"error", "#ff3b3b"},   {"success", "#33ff77"},  {"event", "#d08cff"}};
    contrast.node.stroke_width = 3;
    contrast.node.shadow       = false;
    contrast.node.font_size    = 16;
    contrast.edge.width        = 3;
    contrast.edge.label_color  = "#ffffff";
    out.push_back(contrast);
    return out;
}

LibrarySet MakeBuiltins()
{
    using P = EffectProperty;
    using E = Easing;
    LibrarySet set;

    set.elements = {
        MakeElement("service", Tr("library", "Service"), "▢", Tr("library", "Architecture"), "#4f8cff", "rounded"),
        MakeElement("client", Tr("library", "Client"), "◎", Tr("library", "Architecture"), "#22c55e", "rounded"),
        MakeElement("gateway", Tr("library", "Gateway"), "◇", Tr("library", "Architecture"), "#06b6d4", "rounded"),
        MakeElement("balancer", Tr("library", "Load balancer"), "⇉", Tr("library", "Architecture"), "#14b8a6", "parallelogram", 150, 60),
        MakeElement("external", Tr("library", "External"), "◈", Tr("library", "Architecture"), "#94a3b8", "rounded"),
        MakeElement("user", Tr("library", "User"), "☺", Tr("library", "Architecture"), "#f472b6", "ellipse", 130, 70),
        MakeElement("db", Tr("library", "DB"), "◫", Tr("library", "Data"), "#a855f7", "cylinder", 140, 74),
        MakeElement("queue", Tr("library", "Queue"), "≣", Tr("library", "Data"), "#f59e0b", "queue"),
        MakeElement("cache", Tr("library", "Cache"), "⚡", Tr("library", "Data"), "#ef4444", "hexagon", 140, 66),
        MakeElement("document", Tr("library", "Document"), "▤", Tr("library", "Data"), "#64748b", "document", 130, 74),
        MakeElement("cloud", Tr("library", "Cloud"), "☁", Tr("library", "Other"), "#38bdf8", "cloud", 160, 84),
        MakeElement("decision", Tr("library", "Decision"), "◆", Tr("library", "Logic"), "#eab308", "diamond", 140, 84),
        MakeElement("note", Tr("library", "Note"), "✎", Tr("library", "Other"), "#fbbf24", "note", 150, 70),
    };
    set.elements[4].style.stroke_style = "dashed"; // external
    set.design_systems                 = MakeDesignSystems();

    set.effects = {
        MakeEffect("pulse", Tr("library", "Pulse"), Tr("library", "Pulsing with a glow"), 2,
                   {Track(P::Scale, {{0, 1}, {0.5, 1.06, E::EaseInOut}, {1, 1, E::EaseInOut}}),
                    Track(P::Glow, {{0, 0.35}, {0.5, 0.9, E::EaseInOut}, {1, 0.35, E::EaseInOut}})}),
        MakeEffect("glow", Tr("library", "Glow"), Tr("library", "A halo that fades in and out"), 1,
                   {Track(P::Glow, {{0, 0}, {0.2, 1, E::EaseOut}, {0.8, 1}, {1, 0, E::EaseIn}})}),
        MakeEffect("shake", Tr("library", "Shake"), Tr("library", "Horizontal shake (error)"), 1,
                   {Track(P::OffsetX, {{0, 0}, {0.1, -7}, {0.3, 7}, {0.5, -5}, {0.7, 5}, {0.9, -2}, {1, 0}}),
                    Track(P::Tint, {{0, 0}, {0.2, 0.4}, {1, 0}})},
                   "#ef4444"),
        MakeEffect("blink", Tr("library", "Blink"), Tr("library", "Opacity on/off"), 3,
                   {Track(P::Opacity, {{0, 1}, {0.5, 0.15, E::Step}, {1, 1, E::Step}})}),
        MakeEffect("bounce", Tr("library", "Bounce"), Tr("library", "A jump up with a bounce"), 2,
                   {Track(P::OffsetY, {{0, 0}, {0.4, -14, E::EaseOut}, {1, 0, E::BounceOut}})}),
        MakeEffect("fade-in", Tr("library", "Fade in"), Tr("library", "Smooth fade in"), 1,
                   {Track(P::Opacity, {{0, 0}, {1, 1, E::EaseOut}})}),
        MakeEffect("fade-out", Tr("library", "Fade out"), Tr("library", "Smooth fade out"), 1,
                   {Track(P::Opacity, {{0, 1}, {1, 0, E::EaseIn}})}),
        MakeEffect("pop", Tr("library", "Pop in"), Tr("library", "Scale with an overshoot"), 1,
                   {Track(P::Scale, {{0, 0.7}, {0.6, 1.12, E::BackOut}, {1, 1, E::EaseOut}}),
                    Track(P::Opacity, {{0, 0}, {0.3, 1, E::EaseOut}, {1, 1}})}),
        MakeEffect("spin", Tr("library", "Spin"), Tr("library", "A full turn"), 1, {Track(P::Rotate, {{0, 0}, {1, 360, E::EaseInOut}})}),
        MakeEffect("wobble", Tr("library", "Wobble"), Tr("library", "Swaying from side to side"), 1,
                   {Track(P::Rotate, {{0, 0}, {0.2, -6}, {0.4, 5}, {0.6, -4}, {0.8, 2}, {1, 0}})}),
        MakeEffect("highlight", Tr("library", "Highlight"), Tr("library", "Tint and glow in the accent color"), 1,
                   {Track(P::Tint, {{0, 0}, {0.2, 0.55, E::EaseOut}, {0.8, 0.55}, {1, 0, E::EaseIn}}),
                    Track(P::Glow, {{0, 0}, {0.2, 0.8, E::EaseOut}, {0.8, 0.8}, {1, 0, E::EaseIn}})},
                   "#facc15"),
    };
    for (auto &e : set.effects)
    {
        e.category = Tr("library", "Basic");
    }

    set.animations = {
        MakeTemplate("request-response", Tr("library", "Request → response"),
                     Tr("library", "The client sends a request, the server processes it and responds"),
                     {{"client", Tr("library", "Client")}, {"server", Tr("library", "Server")}},
                     {Msg("client", "server", "request", Tr("library", "request"), 0, 1000),
                      Action("server", Tr("library", "Processing"), 1000, 800), Msg("server", "client", "success", "200 OK", 1800, 1000)}),
        MakeTemplate("retry-backoff", Tr("library", "Retries with backoff"), Tr("library", "503 errors and retries with a growing pause"),
                     {{"client", Tr("library", "Client")}, {"server", Tr("library", "Server")}},
                     {Msg("client", "server", "request", Tr("library", "request"), 0, 900),
                      Msg("server", "client", "error", "503", 1000, 700), Timer("client", 1, "backoff", 1700, 1000),
                      Msg("client", "server", "retry", "retry 1", 2700, 900), Msg("server", "client", "error", "503", 3700, 700),
                      Timer("client", 2, "backoff", 4400, 2000), Msg("client", "server", "retry", "retry 2", 6400, 900),
                      Msg("server", "client", "success", "200 OK", 7400, 900)}),
        MakeTemplate("timeout-fallback", Tr("library", "Timeout and fallback"),
                     Tr("library", "The primary service is down — on timeout the client switches to the fallback"),
                     {{"client", Tr("library", "Client")}, {"primary", Tr("library", "Primary")}, {"fallback", Tr("library", "Fallback")}},
                     {Msg("client", "primary", "request", Tr("library", "request"), 0, 1000), State("primary", "down", 1000, 6500),
                      Timer("client", 3, "timeout", 1000, 3000), Effect("client", "shake", 4000, 600),
                      Msg("client", "fallback", "request", Tr("library", "request"), 4600, 1000),
                      Action("fallback", Tr("library", "Processing"), 5600, 800),
                      Msg("fallback", "client", "success", "200 OK", 6400, 1000)}),
        MakeTemplate(
            "pub-sub", Tr("library", "Publish / subscribe"), Tr("library", "An event through the broker to the subscriber"),
            {{"publisher", Tr("library", "Publisher")}, {"broker", Tr("library", "Broker")}, {"subscriber", Tr("library", "Subscriber")}},
            {Msg("publisher", "broker", "event", "event", 0, 900), Action("broker", Tr("library", "Routing"), 900, 700),
             Msg("broker", "subscriber", "event", "event", 1600, 900), Effect("subscriber", "pop", 2500, 500)}),
        MakeTemplate("cache-aside", "Cache-aside", Tr("library", "Cache miss, read from the DB and write to the cache"),
                     {{"app", Tr("library", "Application")}, {"cache", Tr("library", "Cache")}, {"db", Tr("library", "DB")}},
                     {Msg("app", "cache", "request", "GET", 0, 800), Msg("cache", "app", "error", "miss", 800, 700),
                      Msg("app", "db", "request", "SELECT", 1500, 900), Action("db", Tr("library", "Query"), 2400, 600),
                      Msg("db", "app", "response", "rows", 3000, 900), Msg("app", "cache", "request", "SET", 3900, 800),
                      Effect("cache", "glow", 4700, 600)}),
    };
    return set;
}

} // namespace

const LibrarySet &BuiltinLibrary()
{
    static const LibrarySet kBuiltins = MakeBuiltins();
    return kBuiltins;
}

} // namespace ad
