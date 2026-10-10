#include "MermaidExporter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "Model/Catalog.hpp"
#include "Model/Markers.hpp"

namespace ad
{

namespace
{

/// Mermaid identifier for a node id: [A-Za-z0-9_-], not a keyword, unique.
class Ids
{
public:
    const std::string &operator()(const std::string &id)
    {
        if (const auto it = _map.find(id); it != _map.end())
        {
            return it->second;
        }
        static constexpr std::array kReserved{"end",      "graph",      "flowchart", "subgraph",  "style",   "class",     "classDef",
                                              "click",    "call",       "href",      "linkStyle", "default", "direction", "participant",
                                              "actor",    "note",       "loop",      "alt",       "else",    "opt",       "par",
                                              "and",      "rect",       "critical",  "break",     "box",     "title",     "autonumber",
                                              "activate", "deactivate", "create",    "destroy"};
        std::string                 out;
        for (const char c : id)
        {
            out.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ? c : '_');
        }
        if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) != 0 ||
            std::ranges::any_of(kReserved,
                                [&](const char *r)
                                {
                                    return std::ranges::equal(out, std::string_view(r),
                                                              [](char a, char b)
                                                              {
                                                                  return std::tolower(static_cast<unsigned char>(a)) ==
                                                                         std::tolower(static_cast<unsigned char>(b));
                                                              });
                                }))
        {
            out = "n_" + out;
        }
        std::string unique = out;
        for (int i = 2; _used.contains(unique); ++i)
        {
            unique = out + "_" + std::to_string(i);
        }
        _used.insert(unique);
        return _map.emplace(id, unique).first->second;
    }

private:
    std::map<std::string, std::string> _map;
    std::set<std::string>              _used;
};

/// Text inside a quoted Mermaid label: quotes as entities, line breaks as <br>.
std::string Quoted(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"')
        {
            out += "#quot;";
        }
        else if (c == '\n')
        {
            out += "<br>";
        }
        else if (c != '\r')
        {
            out.push_back(c);
        }
    }
    return out + "\"";
}

/// Text of a sequence message / note (one line, no ';' or '#' that Mermaid would read as syntax).
std::string Line(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '\n' || c == '\r')
        {
            out += ' ';
        }
        else if (c == ';')
        {
            out += "#59;";
        }
        else if (c == '#')
        {
            out += "#35;";
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

std::string FormatNum(double v)
{
    std::string s = std::format("{:.1f}", v);
    if (s.ends_with(".0"))
    {
        s.resize(s.size() - 2);
    }
    return s;
}

/// Node declaration: id + shape brackets around the label.
std::string NodeShape(const std::string &id, std::string_view type, std::string_view shape, const std::string &label)
{
    const std::string q = Quoted(label);
    if (type == "db" || shape == "cylinder")
    {
        return id + "[(" + q + ")]";
    }
    if (type == "decision" || shape == "diamond")
    {
        return id + "{" + q + "}";
    }
    if (shape == "hexagon")
    {
        return id + "{{" + q + "}}";
    }
    if (shape == "ellipse")
    {
        return id + "((" + q + "))";
    }
    if (shape == "parallelogram")
    {
        return id + "[/" + q + "/]";
    }
    if (type == "queue" || shape == "queue")
    {
        return id + "@{ shape: h-cyl, label: " + q + " }";
    }
    if (type == "document" || shape == "document")
    {
        return id + "@{ shape: doc, label: " + q + " }";
    }
    if (type == "cloud" || shape == "cloud")
    {
        return id + "@{ shape: cloud, label: " + q + " }";
    }
    if (type == "note" || shape == "note")
    {
        return id + "@{ shape: brace-r, label: " + q + " }";
    }
    if (type == "user")
    {
        return id + "([" + q + "])";
    }
    if (shape == "rounded")
    {
        return id + "(" + q + ")";
    }
    return id + "[" + q + "]";
}

std::string NodeLabel(const Node &n)
{
    std::string label = n.label.empty() ? n.id : n.label;
    if (!n.subtitle.empty())
    {
        label += "\n" + n.subtitle;
    }
    return label;
}

/// Edge operator: line style and arrow heads.
std::string EdgeArrow(const EdgeStyle &st)
{
    const bool        dashed = st.stroke_style.has_value() && *st.stroke_style != "solid";
    const bool        thick  = !dashed && st.width.has_value() && *st.width >= 2.5;
    const std::string end    = st.arrow_end.value_or("triangle");
    const std::string start  = st.arrow_start.value_or("none");
    auto              head   = [](const std::string &a)
    {
        return a == "none" ? "" : a == "circle" ? "o" : ">";
    };
    auto tail = [](const std::string &a)
    {
        return a == "none" ? "" : a == "circle" ? "o" : "<";
    };
    const std::string h = head(end);
    const std::string t = tail(start);
    if (dashed)
    {
        return t + "-.-" + h;
    }
    if (thick)
    {
        return t + "==" + (h.empty() ? "=" : h);
    }
    return t + "--" + (h.empty() ? "-" : h);
}

std::string MessageArrow(std::string_view variant)
{
    if (variant == "response" || variant == "success")
    {
        return "-->>";
    }
    if (variant == "error")
    {
        return "-x";
    }
    if (variant == "event")
    {
        return "-)";
    }
    return "->>";
}

/// The node closest to a canvas point (notes are anchored to a participant).
const Node *Nearest(const Model &m, Vec2 p, const std::set<std::string> &among)
{
    const Node *best = nullptr;
    double      d    = 0;
    for (const Node &n : m.nodes)
    {
        if (!among.contains(n.id))
        {
            continue;
        }
        const Vec2   c  = n.Center();
        const double dd = (c.x - p.x) * (c.x - p.x) + (c.y - p.y) * (c.y - p.y);
        if (best == nullptr || dd < d)
        {
            best = &n;
            d    = dd;
        }
    }
    return best;
}

} // namespace

std::string_view MermaidKindId(MermaidKind k)
{
    switch (k)
    {
    case MermaidKind::Sequence:
        return "sequence";
    case MermaidKind::Markdown:
        return "markdown";
    case MermaidKind::Flowchart:
        break;
    }
    return "flowchart";
}

std::optional<MermaidKind> MermaidKindFromId(std::string_view id)
{
    for (const MermaidKind k : {MermaidKind::Flowchart, MermaidKind::Sequence, MermaidKind::Markdown})
    {
        if (MermaidKindId(k) == id)
        {
            return k;
        }
    }
    if (id == "md" || id == "both")
    {
        return MermaidKind::Markdown;
    }
    return std::nullopt;
}

std::string ExportMermaidFlowchart(const Model &m, const Registry &reg)
{
    // direction from the shape of the diagram
    double minx = 0;
    double maxx = 0;
    double miny = 0;
    double maxy = 0;
    for (size_t i = 0; i < m.nodes.size(); ++i)
    {
        const Node &n = m.nodes[i];
        minx          = i == 0 ? n.x : std::min(minx, n.x);
        miny          = i == 0 ? n.y : std::min(miny, n.y);
        maxx          = i == 0 ? n.x + n.w : std::max(maxx, n.x + n.w);
        maxy          = i == 0 ? n.y + n.h : std::max(maxy, n.y + n.h);
    }
    const bool wide = maxx - minx >= maxy - miny;

    Ids         ids;
    std::string out;
    if (!m.meta.name.empty())
    {
        std::string title;
        for (const char c : m.meta.name)
        {
            if (c == '"' || c == '\\')
            {
                title.push_back('\\');
            }
            if (c != '\n' && c != '\r')
            {
                title.push_back(c);
            }
        }
        out += "---\ntitle: \"" + title + "\"\n---\n";
    }
    out += wide ? "flowchart LR\n" : "flowchart TB\n";
    std::string styles;
    for (const Node &n : m.nodes)
    {
        const ElementType &et    = reg.Element(n.type, &m.library);
        const std::string  shape = n.style.shape.value_or(et.style.shape.value_or("rounded"));
        const std::string &id    = ids(n.id);
        out += "    " + NodeShape(id, n.type, shape, NodeLabel(n)) + "\n";
        std::vector<std::string> css;
        if (n.style.fill.has_value())
        {
            css.push_back("fill:" + *n.style.fill);
        }
        if (n.style.stroke.has_value())
        {
            css.push_back("stroke:" + *n.style.stroke);
        }
        if (n.style.text_color.has_value())
        {
            css.push_back("color:" + *n.style.text_color);
        }
        if (n.style.stroke_width.has_value())
        {
            css.push_back("stroke-width:" + FormatNum(*n.style.stroke_width) + "px");
        }
        if (!css.empty())
        {
            std::string line = "    style " + id + " ";
            for (size_t i = 0; i < css.size(); ++i)
            {
                line += (i > 0 ? "," : "") + css[i];
            }
            styles += line + "\n";
        }
    }
    int index = 0;
    for (const Edge &e : m.edges)
    {
        if (m.FindNode(e.from) == nullptr || m.FindNode(e.to) == nullptr)
        {
            continue;
        }
        std::string line = "    " + ids(e.from) + " " + EdgeArrow(e.style);
        if (!e.label.empty())
        {
            line += "|" + Quoted(e.label) + "|";
        }
        out += line + " " + ids(e.to) + "\n";
        std::vector<std::string> css;
        if (e.style.color.has_value())
        {
            css.push_back("stroke:" + *e.style.color);
        }
        if (e.style.width.has_value() && *e.style.width < 2.5)
        {
            css.push_back("stroke-width:" + FormatNum(*e.style.width) + "px");
        }
        if (e.style.label_color.has_value())
        {
            css.push_back("color:" + *e.style.label_color);
        }
        if (!css.empty())
        {
            std::string ls = "    linkStyle " + std::to_string(index) + " ";
            for (size_t i = 0; i < css.size(); ++i)
            {
                ls += (i > 0 ? "," : "") + css[i];
            }
            styles += ls + "\n";
        }
        ++index;
    }
    out += styles;
    // exact positions for the Mermaid import (Mermaid ignores comments)
    for (const Node &n : m.nodes)
    {
        out += "    %% ad:pos " + ids(n.id) + " " + FormatNum(n.x) + " " + FormatNum(n.y) + "\n";
    }
    return out;
}

std::string ExportMermaidSequence(const Model &m)
{
    std::vector<const Step *> steps;
    for (const Step &s : m.scenario.steps)
    {
        steps.push_back(&s);
    }
    std::ranges::stable_sort(steps, {}, &Step::start);

    // participants: nodes taking part, left to right
    std::set<std::string> involved;
    for (const Step *s : steps)
    {
        if (s->type == StepType::Message)
        {
            involved.insert(s->from);
            involved.insert(s->to);
        }
        else if (s->type == StepType::Timer || s->type == StepType::State || s->type == StepType::Action || s->type == StepType::Effect)
        {
            involved.insert(s->node_id);
        }
    }
    std::vector<const Node *> parts;
    for (const Node &n : m.nodes)
    {
        if (involved.contains(n.id))
        {
            parts.push_back(&n);
        }
    }
    std::ranges::stable_sort(parts,
                             [](const Node *a, const Node *b)
                             {
                                 return a->Center().x < b->Center().x;
                             });
    std::set<std::string> part_ids;
    for (const Node *n : parts)
    {
        part_ids.insert(n->id);
    }

    Ids         ids;
    std::string out = "sequenceDiagram\n";
    if (!m.meta.name.empty())
    {
        out += "    title " + Line(m.meta.name) + "\n";
    }
    for (const Node *n : parts)
    {
        out += std::string("    ") + (n->type == "user" ? "actor " : "participant ") + ids(n->id) + " as " +
               Line(n->label.empty() ? n->id : n->label) + "\n";
    }
    if (parts.empty())
    {
        out += "    %% the scenario has no steps between nodes\n";
        return out;
    }
    const std::string all = ids(parts.front()->id) + (parts.size() > 1 ? "," + ids(parts.back()->id) : "");

    auto step_line = [&](const Step &s) -> std::string
    {
        switch (s.type)
        {
        case StepType::Message:
            if (!part_ids.contains(s.from) || !part_ids.contains(s.to))
            {
                return {};
            }
            return ids(s.from) + " " + MessageArrow(s.variant) + " " + ids(s.to) + ": " + Line(s.label.empty() ? s.variant : s.label);
        case StepType::Action:
            return part_ids.contains(s.node_id) ? ids(s.node_id) + " ->> " + ids(s.node_id) + ": " + Line(s.text) : std::string{};
        case StepType::Timer:
        {
            const double seconds = s.seconds > 0 ? s.seconds : s.duration / 1000;
            return part_ids.contains(s.node_id)
                       ? "Note over " + ids(s.node_id) + ": ⏱ " + FormatNum(seconds) + "s" + (s.label.empty() ? "" : " " + Line(s.label))
                       : std::string{};
        }
        case StepType::State:
            return part_ids.contains(s.node_id) ? "Note over " + ids(s.node_id) + ": ● " + s.state : std::string{};
        case StepType::Note:
        {
            const Node *near = Nearest(m, {s.x, s.y}, part_ids);
            return near != nullptr ? "Note over " + ids(near->id) + ": " + Line(s.text) : std::string{};
        }
        case StepType::Effect:
            return "%% effect " + s.effect + " on " + s.node_id;
        case StepType::Link:
            return "%% edge animation " + s.anim + " on " + s.edge_id;
        }
        return {};
    };

    // markers, interleaved by time
    std::vector<const Marker *> markers;
    for (const Marker &mk : m.scenario.markers)
    {
        markers.push_back(&mk);
    }
    size_t next_marker  = 0;
    auto   emit_markers = [&](double up_to)
    {
        while (next_marker < markers.size() && markers[next_marker]->time <= up_to + 0.5)
        {
            const Marker *mk = markers[next_marker++];
            out += "    Note over " + all + ": ⚑ " + Line(mk->label.empty() ? "Chapter" : mk->label) + "\n";
        }
    };

    // overlapping messages / actions form a group; a group of several becomes `par`
    auto groupable = [](const Step *s)
    {
        return s->type == StepType::Message || s->type == StepType::Action;
    };
    for (size_t i = 0; i < steps.size();)
    {
        emit_markers(steps[i]->start);
        size_t j = i + 1;
        if (groupable(steps[i]))
        {
            double group_end = steps[i]->End();
            while (j < steps.size() && groupable(steps[j]) && steps[j]->start < group_end - 1 &&
                   (next_marker >= markers.size() || markers[next_marker]->time > steps[j]->start))
            {
                group_end = std::max(group_end, steps[j]->End());
                ++j;
            }
        }
        std::vector<std::string> lines;
        for (size_t k = i; k < j; ++k)
        {
            if (std::string l = step_line(*steps[k]); !l.empty())
            {
                lines.push_back(std::move(l));
            }
        }
        if (lines.size() > 1)
        {
            out += "    par\n";
            for (size_t k = 0; k < lines.size(); ++k)
            {
                out += (k > 0 ? "    and\n        " : "        ") + lines[k] + "\n";
            }
            out += "    end\n";
        }
        else if (!lines.empty())
        {
            out += "    " + lines.front() + "\n";
        }
        i = j;
    }
    emit_markers(m.scenario.duration);
    return out;
}

std::string ExportMermaid(const Model &m, const Registry &reg, MermaidKind kind)
{
    switch (kind)
    {
    case MermaidKind::Sequence:
        return ExportMermaidSequence(m);
    case MermaidKind::Markdown:
    {
        std::string out = "# " + (m.meta.name.empty() ? std::string("Diagram") : m.meta.name) + "\n\n";
        if (!m.meta.description.empty())
        {
            out += m.meta.description + "\n\n";
        }
        out += "```mermaid\n" + ExportMermaidFlowchart(m, reg) + "```\n";
        if (!m.scenario.steps.empty())
        {
            out += "\n```mermaid\n" + ExportMermaidSequence(m) + "```\n";
        }
        return out;
    }
    case MermaidKind::Flowchart:
        break;
    }
    return ExportMermaidFlowchart(m, reg);
}

} // namespace ad
