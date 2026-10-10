#include "MermaidImporter.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>

#include <nlohmann/json.hpp>

#ifdef AD_HAVE_MERMAN
#include <merman.h>
#endif

#include "Common/Exceptions.hpp"
#include "Engine/DisplayList.hpp"
#include "Engine/Layout.hpp"
#include "Io/JsonIo.hpp"
#include "Model/Markers.hpp"
#include "Utils/I18n.hpp"
#include "Utils/Text.hpp"

namespace ad
{

namespace
{

using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// Markdown blocks
// ---------------------------------------------------------------------------

/// Replace the first "%N" of a translated message.
std::string Arg(std::string text, std::string_view placeholder, std::string_view value)
{
    if (const size_t p = text.find(placeholder); p != std::string::npos)
    {
        text.replace(p, placeholder.size(), value);
    }
    return text;
}

std::string Lower(std::string_view s)
{
    std::string r(s);
    std::ranges::transform(r, r.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    return r;
}

/// Opening fence of a ```mermaid block: the fence characters, otherwise empty.
std::string MermaidFence(std::string_view line)
{
    const std::string t = Trim(line);
    if (t.size() < 3 || (t[0] != '`' && t[0] != '~'))
    {
        return {};
    }
    const size_t n = t.find_first_not_of(t[0]);
    if (n == std::string::npos || n < 3)
    {
        return {};
    }
    const std::string info = Lower(Trim(std::string_view(t).substr(n)));
    if (!info.starts_with("mermaid"))
    {
        return {};
    }
    return t.substr(0, n);
}

// ---------------------------------------------------------------------------
// merman (Mermaid parser and layout, C ABI)
// ---------------------------------------------------------------------------

#ifdef AD_HAVE_MERMAN

/// "line L, column C" of a byte offset.
std::string Position(std::string_view text, size_t offset)
{
    offset          = std::min(offset, text.size());
    const auto line = std::ranges::count(text.substr(0, offset), '\n') + 1;
    const auto bol  = text.substr(0, offset).rfind('\n');
    const auto col  = offset - (bol == std::string_view::npos ? 0 : bol + 1) + 1;
    return Arg(Arg(Tr("mermaid", "line %1, column %2"), "%1", std::to_string(line)), "%2", std::to_string(col));
}

MermanNativeSlice Slice(std::string_view s)
{
    MermanNativeSlice slice{};
    slice.struct_size = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeSlice);
    slice.data        = reinterpret_cast<const uint8_t *>(s.data()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    slice.len         = s.size();
    return slice;
}

std::string_view View(const MermanNativeBuffer &b)
{
    return b.data != nullptr
               ? std::string_view(reinterpret_cast<const char *>(b.data), b.len) // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
               : std::string_view{};
}

/// One merman engine for the process (operations are serialized).
class Merman
{
public:
    static Merman &Instance()
    {
        static Merman m;
        return m;
    }

    Merman(const Merman &)            = delete;
    Merman &operator=(const Merman &) = delete;

    /// Result JSON of an operation on Mermaid source; error text with the position on failure.
    std::expected<Json, std::string> Run(MermanNativeOperationCode op, std::string_view source)
    {
        const std::scoped_lock lock(_mutex);
        if (!_error.empty())
        {
            return std::unexpected(_error);
        }
        MermanNativeOperationRequest request{};
        request.struct_size                     = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeOperationRequest);
        request.operation                       = op;
        request.source                          = Slice(source);
        request.uri                             = Slice({});
        request.options_json                    = Slice({});
        MermanNativeResult               result = MERMAN_NATIVE_RESULT_INIT;
        const auto                       status = _api.execute_collect(_engine, &request, &result);
        std::expected<Json, std::string> out;
        if (status == MERMAN_NATIVE_STATUS_OK)
        {
            out = Json::parse(View(result.data), nullptr, false);
            if (out->is_discarded())
            {
                out = std::unexpected(std::string("invalid JSON from the Mermaid parser"));
            }
        }
        else
        {
            out = std::unexpected(_ErrorText(View(result.metadata_or_error_json), source));
        }
        _api.result_free(&result);
        return out;
    }

private:
    Merman()
    {
        MermanNativeApiRequest discovery{};
        discovery.struct_size                           = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeApiRequest);
        discovery.expected_abi_version                  = MERMAN_NATIVE_ABI_VERSION;
        discovery.expected_minimum_prefix_layout_digest = Slice(MERMAN_NATIVE_ABI_MINIMUM_PREFIX_LAYOUT_DIGEST);
        _api.struct_size                                = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeApi);
        if (merman_get_native_api(&discovery, &_api) != MERMAN_NATIVE_STATUS_OK)
        {
            _error = "the Mermaid parser library does not match its header (merman ABI)";
            return;
        }
        MermanNativeEngineConfig config{};
        config.struct_size        = MERMAN_NATIVE_STRUCT_SIZE(MermanNativeEngineConfig);
        config.options_json       = Slice({});
        MermanNativeResult result = MERMAN_NATIVE_RESULT_INIT;
        if (_api.engine_new(&config, &_engine, &result) != MERMAN_NATIVE_STATUS_OK)
        {
            _error = "cannot start the Mermaid parser: " + std::string(View(result.metadata_or_error_json));
        }
        _api.result_free(&result);
    }

    ~Merman()
    {
        if (_engine != 0 && _api.engine_try_close != nullptr)
        {
            _api.engine_try_close(_engine);
        }
    }

    static std::string _ErrorText(std::string_view error_json, std::string_view source)
    {
        const Json e = Json::parse(error_json, nullptr, false);
        if (e.is_discarded() || !e.is_object())
        {
            return "Mermaid parse error";
        }
        std::string msg  = e.value("message", std::string("Mermaid parse error"));
        const Json *span = nullptr;
        if (const auto d = e.find("details"); d != e.end() && d->is_object())
        {
            if (const auto g = d->find("diagnostic"); g != d->end() && g->is_object())
            {
                if (const auto s = g->find("span"); s != g->end() && s->is_object())
                {
                    span = &*s;
                }
                if (g->value("code", std::string{}) == "merman.parse.unsupported_diagram")
                {
                    msg = Arg(Tr("mermaid", "unsupported Mermaid diagram type %1 (supported: flowchart / graph, sequenceDiagram)"), "%1",
                              g->value("diagram_type", std::string("?")));
                }
            }
        }
        if (span != nullptr && span->contains("start") && (*span)["start"].is_number_unsigned())
        {
            msg += " (" + Position(source, (*span)["start"].get<size_t>()) + ")";
        }
        return msg;
    }

    std::mutex              _mutex;
    MermanNativeApi         _api{};
    MermanNativeEngineToken _engine = 0;
    std::string             _error;
};

#endif // AD_HAVE_MERMAN

std::expected<Json, std::string> Parse(std::string_view source, bool layout)
{
#ifdef AD_HAVE_MERMAN
    return Merman::Instance().Run(layout ? MERMAN_NATIVE_OPERATION_LAYOUT_JSON : MERMAN_NATIVE_OPERATION_SEMANTIC_JSON, source);
#else
    (void)source;
    (void)layout;
    return std::unexpected(Tr("mermaid", "this build has no Mermaid import (WITH_MERMAID=OFF)"));
#endif
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string Str(const Json &j, const char *key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

double Num(const Json &j, const char *key, double fallback = 0)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

/// Plain text of a Mermaid label: HTML / Markdown markup dropped, Font Awesome icons removed.
std::string CleanLabel(std::string_view label)
{
    std::string s = HtmlToText(label);
    for (const std::string_view mark : {"**", "__", "`"})
    {
        for (size_t p = s.find(mark); p != std::string::npos; p = s.find(mark, p))
        {
            s.erase(p, mark.size());
        }
    }
    for (size_t p = s.find("fa:fa-"); p != std::string::npos; p = s.find("fa:fa-", p))
    {
        const size_t end = s.find_first_of(" \n", p);
        s.erase(p, end == std::string::npos ? std::string::npos : end - p + 1);
    }
    return Trim(s);
}

/// Label split into the node title and subtitle (first two lines).
void SetNodeLabel(Node &n, std::string_view label)
{
    const std::string text = CleanLabel(label);
    const size_t      nl   = text.find('\n');
    n.label                = Trim(std::string_view(text).substr(0, nl));
    if (nl != std::string::npos)
    {
        std::string rest = Trim(std::string_view(text).substr(nl + 1));
        std::ranges::replace(rest, '\n', ' ');
        n.subtitle = rest;
    }
}

/// Element type and shape override for a Mermaid node shape.
std::pair<std::string, std::optional<std::string>> MapShape(std::string_view shape)
{
    static const std::map<std::string, std::pair<std::string, std::string>, std::less<>> kShapes{
        // shape name -> (element type, shape override; empty -- the type's own)
        {"cylinder", {"db", ""}},
        {"cyl", {"db", ""}},
        {"db", {"db", ""}},
        {"database", {"db", ""}},
        {"disk", {"db", ""}},
        {"lin-cyl", {"db", ""}},
        {"lined-cylinder", {"db", ""}},
        {"h-cyl", {"queue", ""}},
        {"das", {"queue", ""}},
        {"horizontal-cylinder", {"queue", ""}},
        {"diamond", {"decision", ""}},
        {"rhombus", {"decision", ""}},
        {"diam", {"decision", ""}},
        {"question", {"decision", ""}},
        {"decision", {"decision", ""}},
        {"doc", {"document", ""}},
        {"document", {"document", ""}},
        {"docs", {"document", ""}},
        {"documents", {"document", ""}},
        {"st-doc", {"document", ""}},
        {"stacked-document", {"document", ""}},
        {"lin-doc", {"document", ""}},
        {"lined-document", {"document", ""}},
        {"tag-doc", {"document", ""}},
        {"tagged-document", {"document", ""}},
        {"cloud", {"cloud", ""}},
        {"brace", {"note", ""}},
        {"brace-l", {"note", ""}},
        {"brace-r", {"note", ""}},
        {"braces", {"note", ""}},
        {"comment", {"note", ""}},
        {"hexagon", {"service", "hexagon"}},
        {"hex", {"service", "hexagon"}},
        {"prepare", {"service", "hexagon"}},
        {"circle", {"service", "ellipse"}},
        {"circ", {"service", "ellipse"}},
        {"doublecircle", {"service", "ellipse"}},
        {"dbl-circ", {"service", "ellipse"}},
        {"double-circle", {"service", "ellipse"}},
        {"ellipse", {"service", "ellipse"}},
        {"sm-circ", {"service", "ellipse"}},
        {"small-circle", {"service", "ellipse"}},
        {"start", {"service", "ellipse"}},
        {"stop", {"service", "ellipse"}},
        {"fr-circ", {"service", "ellipse"}},
        {"framed-circle", {"service", "ellipse"}},
        {"f-circ", {"service", "ellipse"}},
        {"filled-circle", {"service", "ellipse"}},
        {"lean_right", {"service", "parallelogram"}},
        {"lean_left", {"service", "parallelogram"}},
        {"lean-r", {"service", "parallelogram"}},
        {"lean-l", {"service", "parallelogram"}},
        {"in-out", {"service", "parallelogram"}},
        {"out-in", {"service", "parallelogram"}},
        {"trapezoid", {"service", "parallelogram"}},
        {"inv_trapezoid", {"service", "parallelogram"}},
        {"trap-b", {"service", "parallelogram"}},
        {"trap-t", {"service", "parallelogram"}},
        {"priority", {"service", "parallelogram"}},
        {"manual", {"service", "parallelogram"}},
        {"stadium", {"service", "rounded"}},
        {"pill", {"service", "rounded"}},
        {"terminal", {"service", "rounded"}},
        {"rounded", {"service", "rounded"}},
        {"round", {"service", "rounded"}},
        {"event", {"service", "rounded"}},
    };
    const auto it = kShapes.find(Lower(shape));
    if (it == kShapes.end())
    {
        return {kDefaultType, std::nullopt}; // rect, square, subroutine, odd, ...: a plain service
    }
    const auto &[type, form] = it->second;
    return {type, form.empty() ? std::nullopt : std::optional<std::string>(form)};
}

std::optional<std::string> CssColor(std::string_view value)
{
    if (const auto c = Color::Parse(value); c.has_value())
    {
        return c->Hex();
    }
    return std::nullopt;
}

std::optional<double> CssLength(std::string_view value)
{
    std::string v = Trim(value);
    if (v.ends_with("px"))
    {
        v.resize(v.size() - 2);
    }
    double d = 0;
    if (std::from_chars(v.data(), v.data() + v.size(), d).ec != std::errc{} || !std::isfinite(d))
    {
        return std::nullopt;
    }
    return d;
}

/// "fill:#f96" style declarations as (property, value).
std::vector<std::pair<std::string, std::string>> CssDeclarations(const Json &list)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!list.is_array())
    {
        return out;
    }
    for (const auto &item : list)
    {
        if (!item.is_string())
        {
            continue;
        }
        const std::string decl  = item.get<std::string>();
        const size_t      colon = decl.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        std::string value = Trim(std::string_view(decl).substr(colon + 1));
        if (value.ends_with("!important"))
        {
            value = Trim(std::string_view(value).substr(0, value.size() - 10));
        }
        out.emplace_back(Lower(Trim(std::string_view(decl).substr(0, colon))), value);
    }
    return out;
}

void ApplyNodeCss(NodeStyle &st, const Json &list)
{
    for (const auto &[prop, value] : CssDeclarations(list))
    {
        if (prop == "fill" || prop == "background" || prop == "background-color")
        {
            st.fill = CssColor(value);
        }
        else if (prop == "stroke" || prop == "border-color")
        {
            st.stroke = CssColor(value);
        }
        else if (prop == "color")
        {
            st.text_color = CssColor(value);
        }
        else if (prop == "stroke-width")
        {
            if (const auto w = CssLength(value); w.has_value())
            {
                st.stroke_width = std::clamp(*w, 0.0, 12.0);
            }
        }
        else if (prop == "stroke-dasharray")
        {
            st.stroke_style = "dashed";
        }
        else if (prop == "font-size")
        {
            if (const auto f = CssLength(value); f.has_value())
            {
                st.font_size = std::clamp(*f, 6.0, 48.0);
            }
        }
    }
}

void ApplyEdgeCss(EdgeStyle &st, const Json &list)
{
    for (const auto &[prop, value] : CssDeclarations(list))
    {
        if (prop == "stroke")
        {
            st.color = CssColor(value);
        }
        else if (prop == "color")
        {
            st.label_color = CssColor(value);
        }
        else if (prop == "stroke-width")
        {
            if (const auto w = CssLength(value); w.has_value())
            {
                st.width = std::clamp(*w, 0.5, 12.0);
            }
        }
        else if (prop == "stroke-dasharray")
        {
            st.stroke_style = "dashed";
        }
    }
}

std::string ArrowHead(std::string_view kind)
{
    if (kind == "circle")
    {
        return "circle";
    }
    if (kind == "cross")
    {
        return "open";
    }
    return "triangle";
}

/// Node width for a label: the element's default, wider for long titles.
double WidthFor(const Node &n, double base)
{
    const auto chars = static_cast<double>(Utf8Length(n.label));
    return std::clamp(chars * 8.0 + 44.0, base, 300.0);
}

void SizeNode(Node &n)
{
    if (const ElementType *et = BuiltinLibrary().Element(n.type); et != nullptr)
    {
        n.w = WidthFor(n, et->width);
        n.h = et->height;
    }
}

/// "%% ad:pos <id> <x> <y>" lines written by the Mermaid export (exact positions on re-import).
std::map<std::string, Vec2> PositionComments(std::string_view block)
{
    std::map<std::string, Vec2> out;
    size_t                      pos = 0;
    while (pos <= block.size())
    {
        const size_t               eol  = block.find('\n', pos);
        const std::string          line = Trim(block.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos));
        constexpr std::string_view kTag = "%% ad:pos ";
        if (line.starts_with(kTag))
        {
            std::string_view rest(line);
            rest.remove_prefix(kTag.size());
            std::vector<std::string_view> parts;
            for (size_t p = 0; p < rest.size();)
            {
                const size_t sp = rest.find(' ', p);
                if (sp != p)
                {
                    parts.push_back(rest.substr(p, sp == std::string_view::npos ? std::string_view::npos : sp - p));
                }
                p = sp == std::string_view::npos ? rest.size() : sp + 1;
            }
            double x = 0;
            double y = 0;
            if (parts.size() == 3 && std::from_chars(parts[1].data(), parts[1].data() + parts[1].size(), x).ec == std::errc{} &&
                std::from_chars(parts[2].data(), parts[2].data() + parts[2].size(), y).ec == std::errc{})
            {
                out[std::string(parts[0])] = {x, y};
            }
        }
        if (eol == std::string_view::npos)
        {
            break;
        }
        pos = eol + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
// flowchart
// ---------------------------------------------------------------------------

void ImportFlowchart(const Json &sem, const Json *layout, std::string_view block, const MermaidImportOptions &opt, Model &m,
                     MermaidImportReport &rep)
{
    std::map<std::string, Json> class_defs;
    if (const auto it = sem.find("classDefs"); it != sem.end() && it->is_object())
    {
        for (const auto &[name, decls] : it->items())
        {
            class_defs[name] = decls;
        }
    }
    auto apply_classes = [&](const Json &obj, auto &&apply)
    {
        if (const auto d = class_defs.find("default"); d != class_defs.end())
        {
            apply(d->second);
        }
        if (const auto cl = obj.find("classes"); cl != obj.end() && cl->is_array())
        {
            for (const auto &c : *cl)
            {
                if (const auto def = c.is_string() ? class_defs.find(c.get<std::string>()) : class_defs.end(); def != class_defs.end())
                {
                    apply(def->second);
                }
            }
        }
        if (const auto st = obj.find("styles"); st != obj.end())
        {
            apply(*st);
        }
        if (const auto st = obj.find("style"); st != obj.end())
        {
            apply(*st);
        }
    };

    // nodes
    std::set<std::string> ids;
    for (const auto &jn : sem.value("nodes", Json::array()))
    {
        Node n;
        n.id = Str(jn, "id");
        if (n.id.empty() || ids.contains(n.id))
        {
            continue;
        }
        std::string shape = Str(jn, "shape");
        if (shape.empty())
        {
            shape = Str(jn, "layoutShape");
        }
        const auto [type, form] = MapShape(shape);
        n.type                  = type;
        n.style.shape           = form;
        SetNodeLabel(n, jn.contains("label") && jn["label"].is_string() ? jn["label"].get<std::string>() : n.id);
        if (n.label.empty())
        {
            n.label = n.id;
        }
        SizeNode(n);
        if (opt.keep_colors)
        {
            apply_classes(jn,
                          [&](const Json &decls)
                          {
                              ApplyNodeCss(n.style, decls);
                          });
            // Mermaid's light fills come with dark text: keep the label readable
            if (n.style.fill.has_value() && !n.style.text_color.has_value() && Color::Parse(*n.style.fill, Color{}).Lightness() > 150)
            {
                n.style.text_color = "#0b1220";
            }
        }
        ids.insert(n.id);
        m.nodes.push_back(std::move(n));
        ++rep.nodes;
    }

    // edges
    EdgeStyle defaults;
    if (opt.keep_colors)
    {
        if (const auto d = sem.find("edgeDefaults"); d != sem.end() && d->is_object())
        {
            ApplyEdgeCss(defaults, d->value("style", Json::array()));
        }
    }
    int index = 0;
    for (const auto &je : sem.value("edges", Json::array()))
    {
        Edge e;
        e.from = Str(je, "from");
        e.to   = Str(je, "to");
        if (!ids.contains(e.from) || !ids.contains(e.to))
        {
            ++rep.skipped; // an edge to a subgraph
            continue;
        }
        const std::string stroke = Str(je, "stroke");
        if (stroke == "invisible")
        {
            ++rep.skipped;
            continue;
        }
        e.id    = "e" + std::to_string(++index);
        e.label = CleanLabel(Str(je, "label"));
        std::ranges::replace(e.label, '\n', ' ');
        e.style = defaults;
        if (stroke == "dotted")
        {
            e.style.stroke_style = "dashed";
        }
        else if (stroke == "thick")
        {
            e.style.width = 3;
        }
        // arrow_point, arrow_open, arrow_circle, arrow_cross, double_arrow_point, ...
        const std::string type = Str(je, "type");
        const size_t      us   = type.rfind('_');
        const std::string head = us == std::string::npos ? "point" : type.substr(us + 1);
        if (head == "open")
        {
            e.style.arrow_end = "none";
        }
        else if (head != "point")
        {
            e.style.arrow_end = ArrowHead(head);
        }
        if (type.starts_with("double_"))
        {
            e.style.arrow_start = head == "open" ? "none" : ArrowHead(head);
        }
        if (opt.keep_colors)
        {
            apply_classes(je,
                          [&](const Json &decls)
                          {
                              ApplyEdgeCss(e.style, decls);
                          });
        }
        m.edges.push_back(std::move(e));
        ++rep.edges;
    }

    for (const auto &sg : sem.value("subgraphs", Json::array()))
    {
        ++rep.skipped;
        rep.warnings.push_back(Arg(Tr("mermaid", "subgraph \"%1\": the frame is not imported, its nodes are kept"), "%1",
                                   CleanLabel(Str(sg, "title").empty() ? Str(sg, "id") : Str(sg, "title"))));
    }

    // positions: the export's own comments, otherwise Mermaid's layout, otherwise ours
    const auto exact = PositionComments(block);
    if (!exact.empty() && std::ranges::all_of(m.nodes,
                                              [&](const Node &n)
                                              {
                                                  return exact.contains(n.id);
                                              }))
    {
        for (Node &n : m.nodes)
        {
            n.x = exact.at(n.id).x;
            n.y = exact.at(n.id).y;
        }
        return;
    }
    std::map<std::string, Json> boxes;
    if (layout != nullptr)
    {
        for (const auto &[kind, data] : layout->items())
        {
            for (const auto &ln : data.value("nodes", Json::array()))
            {
                if (!ln.value("is_cluster", false))
                {
                    boxes[Str(ln, "id")] = ln;
                }
            }
        }
    }
    if (m.nodes.empty() || !std::ranges::all_of(m.nodes,
                                                [&](const Node &n)
                                                {
                                                    return boxes.contains(n.id);
                                                }))
    {
        const std::string dir = Str(sem, "direction");
        LayoutOptions     lo;
        lo.direction = dir == "LR" || dir == "RL" ? LayoutDirection::LeftToRight : LayoutDirection::TopToBottom;
        AutoLayout(m, lo);
        return;
    }
    // Mermaid's boxes hug the labels: spread the centers so that our (larger) nodes do not overlap
    double kx = 1;
    double ky = 1;
    for (const Node &n : m.nodes)
    {
        const Json &b = boxes.at(n.id);
        kx            = std::max(kx, n.w / std::max(20.0, Num(b, "width", n.w)));
        ky            = std::max(ky, n.h / std::max(20.0, Num(b, "height", n.h)));
    }
    kx = std::min(kx, 2.5) * 1.15;
    ky = std::min(ky, 2.5) * 1.15;
    for (Node &n : m.nodes)
    {
        const Json &b = boxes.at(n.id);
        n.x           = Num(b, "x") * kx - n.w / 2;
        n.y           = Num(b, "y") * ky - n.h / 2;
    }
}

// ---------------------------------------------------------------------------
// sequenceDiagram
// ---------------------------------------------------------------------------

// Mermaid's sequence LINETYPE codes
enum Line : int
{
    kSolid          = 0,
    kDotted         = 1,
    kNote           = 2,
    kSolidCross     = 3,
    kDottedCross    = 4,
    kSolidOpen      = 5,
    kDottedOpen     = 6,
    kLoopStart      = 10,
    kAltStart       = 12,
    kAltElse        = 13,
    kOptStart       = 15,
    kActiveStart    = 17,
    kActiveEnd      = 18,
    kParStart       = 19,
    kParAnd         = 20,
    kParEnd         = 21,
    kSolidPoint     = 24,
    kDottedPoint    = 25,
    kAutonumber     = 26,
    kCriticalStart  = 27,
    kCriticalOption = 28,
    kBreakStart     = 30,
    kParOverStart   = 32,
    kBidiSolid      = 33,
    kBidiDotted     = 34,
};

bool IsMessage(int t)
{
    return (t >= kSolid && t <= kDottedOpen && t != kNote) || t == kSolidPoint || t == kDottedPoint || (t >= 33 && t <= 61);
}

bool IsDotted(int t)
{
    return t == kDotted || t == kDottedCross || t == kDottedOpen || t == kDottedPoint || t == kBidiDotted || (t >= 51 && t <= 58);
}

constexpr double kMessageMs = 1000;
constexpr double kGapMs     = 150;
constexpr double kNoteMs    = 2000;
constexpr double kActionMs  = 900;

std::string MessageVariant(int type, std::string_view label)
{
    const std::string l = Lower(Trim(label));
    if (type == kSolidCross || type == kDottedCross)
    {
        return "error";
    }
    if (l.starts_with("retry"))
    {
        return "retry";
    }
    if (type == kSolidPoint || type == kDottedPoint)
    {
        return "event";
    }
    if (IsDotted(type))
    {
        if (l.size() >= 3 && l[0] == '2' && std::isdigit(static_cast<unsigned char>(l[1])) != 0)
        {
            return "success";
        }
        if (l.size() >= 3 && (l[0] == '4' || l[0] == '5') && std::isdigit(static_cast<unsigned char>(l[1])) != 0)
        {
            return "error";
        }
        return "response";
    }
    return "request";
}

/// State id from "● down" / "● Down" (the Mermaid export's state notes).
std::optional<std::string> StateFromText(std::string_view text)
{
    const std::string t = Lower(Trim(text));
    for (const auto &s : NodeStates())
    {
        if (t == s.id || t == Lower(s.label))
        {
            return std::string(s.id);
        }
    }
    return std::nullopt;
}

class SequenceBuilder
{
public:
    SequenceBuilder(Model &m, MermaidImportReport &rep) : _m(m), _rep(rep) {}

    void Build(const Json &sem)
    {
        _Participants(sem);
        for (const auto &msg : sem.value("messages", Json::array()))
        {
            _Message(msg);
        }
        for (auto &[node, starts] : _active)
        {
            for (const double s : starts)
            {
                _State(node, "active", s, std::max(_t, s + 300) - s);
            }
        }
        _CloseStates();
        _m.scenario.duration = std::max(1000.0, std::ceil((_t + 600) / 100) * 100);
        NormalizeMarkers(_m.scenario);
        if (const std::string title = Str(sem, "title"); !title.empty())
        {
            _m.meta.name = CleanLabel(title);
        }
    }

private:
    void _Participants(const Json &sem)
    {
        const Json actors = sem.value("actors", Json::object());
        // a row below the flowchart (if any), in the participants' order
        double y = 120;
        double x = 80;
        if (!_m.nodes.empty())
        {
            double bottom = 0;
            for (const Node &n : _m.nodes)
            {
                bottom = std::max(bottom, n.y + n.h);
            }
            y = bottom + 160;
        }
        for (const auto &id_json : sem.value("actorOrder", Json::array()))
        {
            const std::string id = id_json.is_string() ? id_json.get<std::string>() : std::string{};
            if (id.empty() || _m.FindNode(id) != nullptr)
            {
                continue; // already a flowchart node
            }
            const Json a    = actors.value(id, Json::object());
            const auto kind = Str(a, "type");
            Node       n;
            n.id   = id;
            n.type = kind == "actor"                               ? "user"
                     : kind == "database" || kind == "collections" ? "db"
                     : kind == "queue"                             ? "queue"
                                                                   : kDefaultType;
            SetNodeLabel(n, Str(a, "description").empty() ? id : Str(a, "description"));
            SizeNode(n);
            n.x = x;
            n.y = y;
            x += std::max(n.w, kDefaultNodeW) + 100;
            _m.nodes.push_back(std::move(n));
            ++_rep.nodes;
        }
    }

    std::string _EdgeFor(const std::string &from, const std::string &to)
    {
        if (const Edge *e = _m.EdgeBetween(from, to); e != nullptr)
        {
            return e->id;
        }
        Edge e;
        e.id   = "e" + std::to_string(_m.edges.size() + 1);
        e.from = from;
        e.to   = to;
        while (_m.FindEdge(e.id) != nullptr)
        {
            e.id += "_";
        }
        // participants stand in a row: an edge that would cross the ones in between arcs over them
        const Vec2 a = _m.FindNode(from)->Center();
        const Vec2 b = _m.FindNode(to)->Center();
        if (_Blocked(from, to, a, b))
        {
            e.curve = a.x <= b.x ? -0.3 : 0.3; // above the row
        }
        _m.edges.push_back(e);
        ++_rep.edges;
        return e.id;
    }

    /// A node other than the ends lies on the straight line between two centers.
    [[nodiscard]] bool _Blocked(const std::string &from, const std::string &to, Vec2 a, Vec2 b) const
    {
        const double len   = std::hypot(b.x - a.x, b.y - a.y);
        const int    steps = std::max(2, static_cast<int>(len / 10));
        for (int i = 1; i < steps; ++i)
        {
            const double t = static_cast<double>(i) / steps;
            const Vec2   p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            for (const Node &n : _m.nodes)
            {
                if (n.id != from && n.id != to && p.x > n.x && p.x < n.x + n.w && p.y > n.y && p.y < n.y + n.h)
                {
                    return true;
                }
            }
        }
        return false;
    }

    Step &_Add(StepType type, double start, double duration)
    {
        Step s;
        s.id       = "s" + std::to_string(_m.scenario.steps.size() + 1);
        s.type     = type;
        s.start    = start;
        s.duration = duration;
        _m.scenario.steps.push_back(std::move(s));
        ++_rep.steps;
        return _m.scenario.steps.back();
    }

    void _Marker(std::string label)
    {
        if (!_m.scenario.markers.empty() && std::abs(_m.scenario.markers.back().time - _t) < 1)
        {
            _m.scenario.markers.back().label += " · " + label; // several blocks start at once
            return;
        }
        Marker mk;
        mk.id    = "m" + std::to_string(_m.scenario.markers.size() + 1);
        mk.time  = _t;
        mk.label = std::move(label);
        _m.scenario.markers.push_back(std::move(mk));
        ++_rep.markers;
    }

    void _State(const std::string &node, const std::string &state, double start, double duration)
    {
        Step &s   = _Add(StepType::State, start, std::max(kMinStepDuration, duration));
        s.node_id = node;
        s.state   = state;
    }

    /// States set by "● state" notes last until the node's next state note.
    void _CloseStates()
    {
        for (auto &[node, open] : _states)
        {
            _State(node, open.first, open.second, std::max(_t, open.second + 600) - open.second);
        }
        _states.clear();
    }

    void _Message(const Json &msg)
    {
        const int         type = msg.value("type", -1);
        const std::string from = Str(msg, "from");
        const std::string to   = Str(msg, "to");
        std::string       text = msg.contains("message") && msg["message"].is_string() ? CleanLabel(msg["message"].get<std::string>()) : "";
        std::ranges::replace(text, '\n', ' ');
        if (IsMessage(type))
        {
            if (from.empty() || to.empty() || _m.FindNode(from) == nullptr || _m.FindNode(to) == nullptr)
            {
                ++_rep.skipped;
                return;
            }
            if (_autonumber.has_value())
            {
                text = std::to_string(*_autonumber) + ". " + text;
                *_autonumber += _autonumber_step;
            }
            if (from == to)
            {
                Step &s   = _Add(StepType::Action, _t, kActionMs);
                s.node_id = from;
                s.text    = text;
                _t += kActionMs + kGapMs;
                return;
            }
            Step &s   = _Add(StepType::Message, _t, kMessageMs);
            s.from    = from;
            s.to      = to;
            s.edge_id = _EdgeFor(from, to);
            s.label   = text;
            s.variant = MessageVariant(type, text);
            _t += kMessageMs + kGapMs;
            return;
        }
        switch (type)
        {
        case kNote:
            _Note(from, to, msg.value("placement", 2), text);
            break;
        case kActiveStart:
            _active[from].push_back(_t);
            break;
        case kActiveEnd:
            if (auto it = _active.find(from); it != _active.end() && !it->second.empty())
            {
                const double s = it->second.back();
                it->second.pop_back();
                _State(from, "active", s, std::max(_t - s, 300.0));
            }
            break;
        case kAutonumber:
        {
            const Json &v = msg.contains("message") ? msg["message"] : Json();
            if (v.is_object() && !v.value("visible", true))
            {
                _autonumber.reset();
                break;
            }
            _autonumber      = v.is_object() ? v.value("start", 1) : 1;
            _autonumber_step = v.is_object() ? std::max(1, v.value("step", 1)) : 1;
            break;
        }
        case kLoopStart:
            _Marker(text.empty() ? "loop" : "loop: " + text);
            break;
        case kAltStart:
            _Marker(text.empty() ? "alt" : "alt: " + text);
            break;
        case kAltElse:
            _Marker(text.empty() ? "else" : "else: " + text);
            break;
        case kOptStart:
            _Marker(text.empty() ? "opt" : "opt: " + text);
            break;
        case kCriticalStart:
            _Marker(text.empty() ? "critical" : "critical: " + text);
            break;
        case kCriticalOption:
            _Marker(text.empty() ? "option" : "option: " + text);
            break;
        case kBreakStart:
            _Marker(text.empty() ? "break" : "break: " + text);
            break;
        case kParStart:
        case kParOverStart:
            if (!text.empty())
            {
                _Marker("par: " + text);
            }
            _par.push_back({_t, _t});
            break;
        case kParAnd:
            if (!_par.empty())
            {
                _par.back().second = std::max(_par.back().second, _t);
                _t                 = _par.back().first;
            }
            break;
        case kParEnd:
            if (!_par.empty())
            {
                _t = std::max(_par.back().second, _t);
                _par.pop_back();
            }
            break;
        default:
            break; // block ends, rect (background), create / destroy
        }
    }

    void _Note(const std::string &from, const std::string &to, int placement, const std::string &text)
    {
        const Node *a = _m.FindNode(from);
        const Node *b = to.empty() ? a : _m.FindNode(to);
        if (a == nullptr)
        {
            ++_rep.skipped;
            return;
        }
        b = b != nullptr ? b : a;
        // the export's encodings: "⚑ marker", "⏱ 5s label" (timer), "● down" (state)
        constexpr std::string_view kMarker = "⚑";
        constexpr std::string_view kTimer  = "⏱";
        constexpr std::string_view kState  = "●";
        if (text.starts_with(kMarker))
        {
            _Marker(Trim(std::string_view(text).substr(kMarker.size())));
            return;
        }
        if (text.starts_with(kTimer))
        {
            std::string_view rest(text);
            rest.remove_prefix(kTimer.size());
            const std::string r       = Trim(rest);
            double            seconds = 0;
            const auto [p, ec]        = std::from_chars(r.data(), r.data() + r.size(), seconds);
            if (ec == std::errc{} && seconds > 0)
            {
                std::string label = Trim(std::string_view(p, r.data() + r.size() - p));
                if (label.starts_with('s'))
                {
                    label = Trim(std::string_view(label).substr(1));
                }
                Step &s   = _Add(StepType::Timer, _t, seconds * 1000);
                s.node_id = a->id;
                s.seconds = seconds;
                s.label   = label;
                _t += kGapMs * 2;
                return;
            }
        }
        if (text.starts_with(kState))
        {
            if (const auto st = StateFromText(std::string_view(text).substr(kState.size())); st.has_value())
            {
                if (auto open = _states.find(a->id); open != _states.end())
                {
                    _State(a->id, open->second.first, open->second.second, std::max(_t - open->second.second, 300.0));
                    _states.erase(open);
                }
                if (*st != "ok")
                {
                    _states[a->id] = {*st, _t};
                }
                _t += kGapMs * 2;
                return;
            }
        }
        Step &s = _Add(StepType::Note, _t, kNoteMs);
        s.text  = text;
        // leftOf 0, rightOf 1, over 2
        const double cx = (a->Center().x + b->Center().x) / 2;
        s.x             = placement == 0 ? a->x - 190 : placement == 1 ? a->x + a->w + 30 : cx - 80;
        s.y             = placement == 2 ? std::min(a->y, b->y) - 80 : a->y;
        _t += kGapMs * 3;
    }

    Model                                                &_m;
    MermaidImportReport                                  &_rep;
    double                                                _t = 0;
    std::map<std::string, std::vector<double>>            _active;
    std::map<std::string, std::pair<std::string, double>> _states;
    std::vector<std::pair<double, double>>                _par; // (branch start, latest branch end)
    std::optional<int>                                    _autonumber;
    int                                                   _autonumber_step = 1;
};

std::string DiagramKind(const Json &sem)
{
    const std::string type = Str(sem, "type");
    if (type == "sequence" || type == "sequenceDiagram")
    {
        return "sequence";
    }
    if (type.starts_with("flowchart") || Str(sem, "keyword") == "flowchart" || Str(sem, "keyword") == "graph")
    {
        return "flowchart";
    }
    return type;
}

} // namespace

bool MermaidImportAvailable()
{
#ifdef AD_HAVE_MERMAN
    return true;
#else
    return false;
#endif
}

std::vector<std::string> MermaidBlocks(std::string_view text)
{
    std::vector<std::string> blocks;
    std::string              fence;
    std::string              current;
    size_t                   pos = 0;
    while (pos < text.size())
    {
        const size_t           eol  = text.find('\n', pos);
        const std::string_view line = text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
        pos                         = eol == std::string_view::npos ? text.size() : eol + 1;
        if (fence.empty())
        {
            fence = MermaidFence(line);
            continue;
        }
        const std::string t = Trim(line);
        if (t.size() >= fence.size() && t.find_first_not_of(fence[0]) == std::string::npos)
        {
            blocks.push_back(std::move(current));
            current.clear();
            fence.clear();
            continue;
        }
        current.append(line);
        current.push_back('\n');
    }
    if (blocks.empty())
    {
        blocks.emplace_back(text);
    }
    return blocks;
}

std::expected<Model, std::string> ImportMermaid(std::string_view text, const MermaidImportOptions &opt, MermaidImportReport *report)
{
    MermaidImportReport  local;
    MermaidImportReport &rep = report != nullptr ? *report : local;
    rep                      = {};
    try
    {
        std::optional<std::pair<Json, std::string>> flowchart; // semantic model, source
        std::optional<Json>                         sequence;
        const auto                                  blocks = MermaidBlocks(text);
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            auto sem = Parse(blocks[i], false);
            if (!sem.has_value())
            {
                if (blocks.size() == 1)
                {
                    return std::unexpected(sem.error());
                }
                rep.warnings.push_back(Arg(Arg(Tr("mermaid", "block %1: %2"), "%1", std::to_string(i + 1)), "%2", sem.error()));
                ++rep.skipped;
                continue;
            }
            const std::string kind = DiagramKind(*sem);
            if (kind == "flowchart" && !flowchart.has_value())
            {
                flowchart.emplace(std::move(*sem), blocks[i]);
            }
            else if (kind == "sequence" && !sequence.has_value())
            {
                sequence = std::move(*sem);
            }
            else
            {
                rep.warnings.push_back(Arg(
                    Arg(Tr("mermaid", "block %1: %2 diagram skipped (supported: flowchart, sequenceDiagram)"), "%1", std::to_string(i + 1)),
                    "%2", kind));
                ++rep.skipped;
            }
        }
        if (!flowchart.has_value() && !sequence.has_value())
        {
            return std::unexpected(rep.warnings.empty() ? Tr("mermaid", "no Mermaid flowchart or sequence diagram found")
                                                        : rep.warnings.front());
        }

        Model m;
        m.meta.name = Tr("document", "Mermaid import");
        if (flowchart.has_value())
        {
            const auto  layout = Parse(flowchart->second, true);
            const Json *lay    = nullptr;
            if (layout.has_value() && layout->contains("layout"))
            {
                lay = &(*layout)["layout"];
                if (const auto meta = layout->find("meta"); meta != layout->end() && meta->is_object())
                {
                    if (const std::string title = Str(*meta, "title"); !title.empty())
                    {
                        m.meta.name = CleanLabel(title);
                    }
                }
            }
            ImportFlowchart(flowchart->first, lay, flowchart->second, opt, m, rep);
            rep.kinds = "flowchart";
        }
        if (sequence.has_value())
        {
            SequenceBuilder(m, rep).Build(*sequence);
            rep.kinds = rep.kinds.empty() ? "sequence" : rep.kinds + " + sequence";
        }
        if (!sequence.has_value())
        {
            m.scenario.duration = 12000;
        }
        NormalizeModel(m);
        return m;
    }
    catch (const AdError &e)
    {
        return std::unexpected(std::string(e.what()));
    }
    catch (const Json::exception &e)
    {
        return std::unexpected(std::string("unexpected Mermaid parser output: ") + e.what());
    }
}

} // namespace ad
