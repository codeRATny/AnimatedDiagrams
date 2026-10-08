#include "DrawioImporter.hpp"

#include <algorithm>
#include <charconv>
#include <set>

#include "Common/Exceptions.hpp"
#include "Import/Inflate.hpp"
#include "Import/XmlReader.hpp"
#include "Io/JsonIo.hpp"
#include "Utils/Text.hpp"

namespace ad
{

namespace
{

struct Cell
{
    std::string                        id;
    std::string                        parent;
    std::string                        value;
    std::map<std::string, std::string> style;
    bool                               vertex = false;
    bool                               edge   = false;
    std::string                        source;
    std::string                        target;
    Rect                               geo;
    std::vector<Vec2>                  points;
};

double Num(std::string_view s, double def)
{
    double v = 0;
    if (!s.empty() && s.front() == '+')
    {
        s.remove_prefix(1);
    }
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc{} && ptr == s.data() + s.size() ? v : def;
}

std::string StyleValue(const Cell &c, const std::string &key, const std::string &def = {})
{
    const auto it = c.style.find(key);
    return it != c.style.end() ? it->second : def;
}

bool StyleFlag(const Cell &c, const std::string &key) { return StyleValue(c, key) == "1"; }

std::optional<std::string> StyleColor(const Cell &c, const std::string &key)
{
    const auto color = Color::Parse(StyleValue(c, key));
    if (!color.has_value())
    {
        return std::nullopt;
    }
    return color->Hex();
}

/// Locate the mxGraphModel of the requested page; decompresses compressed pages into `storage`.
const XmlNode &PageModel(const XmlNode &root, int page, XmlNode &storage, std::string &page_name)
{
    if (root.name == "mxGraphModel")
    {
        return root;
    }
    if (root.name != "mxfile")
    {
        throw ParseError("not a draw.io file (root element <" + root.name + ">)");
    }
    const auto diagrams = root.ChildrenNamed("diagram");
    if (diagrams.empty())
    {
        throw ParseError("draw.io file has no pages");
    }
    if (page < 0 || static_cast<size_t>(page) >= diagrams.size())
    {
        throw ParseError("page " + std::to_string(page) + " does not exist (pages: " + std::to_string(diagrams.size()) + ")");
    }
    const XmlNode *d = diagrams[static_cast<size_t>(page)];
    page_name        = d->AttrOr("name");
    if (const XmlNode *model = d->Child("mxGraphModel"); model != nullptr)
    {
        return *model;
    }
    const std::string payload = Trim(d->text);
    if (payload.empty())
    {
        throw ParseError("page '" + page_name + "' is empty");
    }
    // compressed page: base64 -> raw deflate -> URI-encoded XML
    const auto        bytes    = Base64Decode(payload);
    const auto        inflated = Inflate(bytes);
    const std::string xml      = UrlDecode(std::string_view(reinterpret_cast<const char *>(inflated.data()), inflated.size()));
    storage                    = ParseXml(xml);
    if (storage.name != "mxGraphModel")
    {
        throw ParseError("compressed page does not contain an mxGraphModel");
    }
    return storage;
}

Cell ReadCell(const XmlNode &cell_node, const XmlNode *wrapper)
{
    Cell c;
    c.id     = wrapper != nullptr ? wrapper->AttrOr("id") : cell_node.AttrOr("id");
    c.value  = wrapper != nullptr ? wrapper->AttrOr("label") : cell_node.AttrOr("value");
    c.parent = cell_node.AttrOr("parent");
    c.style  = ParseDrawioStyle(cell_node.AttrOr("style"));
    c.vertex = cell_node.AttrOr("vertex") == "1";
    c.edge   = cell_node.AttrOr("edge") == "1";
    c.source = cell_node.AttrOr("source");
    c.target = cell_node.AttrOr("target");
    if (const XmlNode *g = cell_node.Child("mxGeometry"); g != nullptr)
    {
        c.geo = {Num(g->AttrOr("x"), 0), Num(g->AttrOr("y"), 0), Num(g->AttrOr("width"), 0), Num(g->AttrOr("height"), 0)};
        for (const XmlNode &arr : g->children)
        {
            if (arr.name == "Array" && arr.AttrOr("as") == "points")
            {
                for (const XmlNode &p : arr.children)
                {
                    if (p.name == "mxPoint")
                    {
                        c.points.push_back({Num(p.AttrOr("x"), 0), Num(p.AttrOr("y"), 0)});
                    }
                }
            }
        }
    }
    return c;
}

std::string LabelText(const Cell &c)
{
    if (StyleFlag(c, "html") || c.value.find('<') != std::string::npos)
    {
        return HtmlToText(c.value);
    }
    return Trim(c.value);
}

std::string MapArrow(const std::string &arrow)
{
    if (arrow == "none")
    {
        return "none";
    }
    if (arrow == "open" || arrow == "openThin" || arrow == "openAsync")
    {
        return "open";
    }
    if (arrow == "diamond" || arrow == "diamondThin" || arrow == "ERmandOne")
    {
        return "diamond";
    }
    if (arrow == "oval" || arrow == "circle" || arrow == "circlePlus")
    {
        return "circle";
    }
    return "triangle";
}

/// Element type + shape override for a vertex style.
std::pair<std::string, std::optional<std::string>> MapShape(const Cell &c)
{
    const std::string shape = StyleValue(c, "shape");
    auto              has   = [&](std::string_view token)
    {
        return shape.find(token) != std::string::npos;
    };
    if (has("cylinder") || has("datastore") || has("database"))
    {
        return {"db", std::nullopt};
    }
    if (has("rhombus") || has("decision"))
    {
        return {"decision", std::nullopt};
    }
    if (has("cloud"))
    {
        return {"cloud", std::nullopt};
    }
    if (has("document"))
    {
        return {"document", std::nullopt};
    }
    if (has("note"))
    {
        return {"note", std::nullopt};
    }
    if (has("umlActor") || shape == "actor")
    {
        return {"user", std::nullopt};
    }
    if (has("ellipse") || has("doubleEllipse"))
    {
        return {"service", "ellipse"};
    }
    if (has("hexagon"))
    {
        return {"service", "hexagon"};
    }
    if (has("parallelogram"))
    {
        return {"service", "parallelogram"};
    }
    if (StyleFlag(c, "rounded") || has("mxgraph."))
    {
        return {"service", "rounded"};
    }
    return {"service", "rect"};
}

} // namespace

std::map<std::string, std::string> ParseDrawioStyle(std::string_view style)
{
    std::map<std::string, std::string> out;
    std::string                        first_bare;
    size_t                             pos = 0;
    while (pos <= style.size())
    {
        const size_t           end   = std::min(style.find(';', pos), style.size());
        const std::string_view token = style.substr(pos, end - pos);
        if (!token.empty())
        {
            const size_t eq = token.find('=');
            if (eq == std::string_view::npos)
            {
                out[std::string(token)] = "1";
                if (first_bare.empty())
                {
                    first_bare = std::string(token);
                }
            }
            else
            {
                out[std::string(token.substr(0, eq))] = std::string(token.substr(eq + 1));
            }
        }
        pos = end + 1;
    }
    if (!first_bare.empty() && !out.contains("shape"))
    {
        out["shape"] = first_bare;
    }
    return out;
}

std::vector<std::string> DrawioPageNames(std::string_view file_content)
{
    const XmlNode            root = ParseXml(file_content);
    std::vector<std::string> names;
    if (root.name == "mxGraphModel")
    {
        names.emplace_back("Page-1");
    }
    for (const XmlNode *d : root.ChildrenNamed("diagram"))
    {
        names.push_back(d->AttrOr("name", "Page-" + std::to_string(names.size() + 1)));
    }
    return names;
}

std::expected<Model, std::string> ImportDrawio(std::string_view file_content, const DrawioImportOptions &opt, DrawioImportReport *report)
{
    DrawioImportReport  local;
    DrawioImportReport &rep = report != nullptr ? *report : local;
    rep                     = {};
    try
    {
        const XmlNode  root = ParseXml(file_content);
        XmlNode        storage;
        std::string    page_name;
        const XmlNode &gm         = PageModel(root, opt.page, storage, page_name);
        const XmlNode *cells_root = gm.Child("root");
        if (cells_root == nullptr)
        {
            return std::unexpected(std::string("draw.io page has no <root>"));
        }

        std::vector<Cell> cells;
        for (const XmlNode &n : cells_root->children)
        {
            if (n.name == "mxCell")
            {
                cells.push_back(ReadCell(n, nullptr));
            }
            else if (n.name == "object" || n.name == "UserObject")
            {
                if (const XmlNode *inner = n.Child("mxCell"); inner != nullptr)
                {
                    cells.push_back(ReadCell(*inner, &n));
                }
            }
        }
        std::map<std::string, const Cell *> by_id;
        for (const auto &c : cells)
        {
            by_id[c.id] = &c;
        }
        auto is_layer = [&](const Cell &c)
        {
            return !c.vertex && !c.edge;
        };
        // absolute offset of a cell's coordinate system (sum of ancestor vertex positions)
        auto offset_of = [&](const std::string &parent_id)
        {
            Vec2        off;
            std::string cur = parent_id;
            for (int guard = 0; guard < 64 && !cur.empty(); ++guard)
            {
                const auto it = by_id.find(cur);
                if (it == by_id.end() || is_layer(*it->second))
                {
                    break;
                }
                off = off + Vec2{it->second->geo.x, it->second->geo.y};
                cur = it->second->parent;
            }
            return off;
        };

        Model m;
        m.meta.name = page_name.empty() ? "Импорт draw.io" : page_name;
        std::set<std::string>              node_ids;
        std::map<std::string, std::string> edge_labels; // edge id -> text from child label cells

        for (const auto &c : cells)
        {
            if (!c.vertex)
            {
                continue;
            }
            const auto parent = by_id.find(c.parent);
            if (parent != by_id.end() && parent->second->edge)
            {
                edge_labels[c.parent] = LabelText(c); // label attached to an edge
                continue;
            }
            const std::string shape = StyleValue(c, "shape");
            if (shape == "group" || StyleFlag(c, "container") || shape.starts_with("swimlane") || shape == "table")
            {
                ++rep.skipped; // containers only provide coordinates for their children
                continue;
            }
            const Vec2        off   = offset_of(c.parent);
            const std::string label = LabelText(c);
            if (shape == "text" || shape == "edgeLabel")
            {
                if (label.empty())
                {
                    continue;
                }
                Step note;
                note.type = StepType::Note;
                note.text = label;
                std::ranges::replace(note.text, '\n', ' ');
                note.x        = off.x + c.geo.x;
                note.y        = off.y + c.geo.y;
                note.start    = 0;
                note.duration = 12000;
                m.scenario.steps.push_back(std::move(note));
                ++rep.notes;
                continue;
            }

            Node n;
            n.id                    = c.id;
            const auto [type, form] = MapShape(c);
            n.type                  = type;
            if (form.has_value())
            {
                n.style.shape = form;
            }
            const size_t nl = label.find('\n');
            n.label         = label.substr(0, nl);
            if (nl != std::string::npos)
            {
                n.subtitle = label.substr(nl + 1);
                std::ranges::replace(n.subtitle, '\n', ' ');
            }
            n.x = off.x + c.geo.x;
            n.y = off.y + c.geo.y;
            n.w = std::max(20.0, c.geo.w > 0 ? c.geo.w : 120.0);
            n.h = std::max(20.0, c.geo.h > 0 ? c.geo.h : 60.0);
            if (opt.keep_colors)
            {
                n.style.fill       = StyleColor(c, "fillColor");
                n.style.stroke     = StyleColor(c, "strokeColor");
                n.style.text_color = StyleColor(c, "fontColor");
                if (n.style.fill.has_value() && !n.style.text_color.has_value() && Color::Parse(*n.style.fill)->Lightness() > 150)
                {
                    n.style.text_color = "#1f2937"; // light draw.io fills keep their dark text
                }
            }
            if (const double fs = Num(StyleValue(c, "fontSize"), 0); fs > 0)
            {
                n.style.font_size = std::clamp(fs, 6.0, 48.0);
            }
            if (StyleFlag(c, "dashed"))
            {
                n.style.stroke_style = "dashed";
            }
            if (const double sw = Num(StyleValue(c, "strokeWidth"), 0); sw > 0)
            {
                n.style.stroke_width = std::clamp(sw, 0.5, 12.0);
            }
            if (const double op = Num(StyleValue(c, "opacity"), -1); op >= 0)
            {
                n.style.opacity = std::clamp(op / 100.0, 0.05, 1.0);
            }
            if (StyleFlag(c, "shadow"))
            {
                n.style.shadow = true;
            }
            node_ids.insert(n.id);
            m.nodes.push_back(std::move(n));
            ++rep.nodes;
        }

        for (const auto &c : cells)
        {
            if (!c.edge)
            {
                continue;
            }
            if (!node_ids.contains(c.source) || !node_ids.contains(c.target) || c.source == c.target)
            {
                ++rep.skipped;
                rep.warnings.push_back("edge '" + c.id + "' skipped: not connected to two imported shapes");
                continue;
            }
            Edge e;
            e.id    = c.id;
            e.from  = c.source;
            e.to    = c.target;
            e.label = LabelText(c);
            if (e.label.empty() && edge_labels.contains(c.id))
            {
                e.label = edge_labels[c.id];
            }
            std::ranges::replace(e.label, '\n', ' ');
            const Vec2 off = offset_of(c.parent);
            for (const Vec2 p : c.points)
            {
                e.waypoints.push_back(off + p);
            }
            const std::string edge_style = StyleValue(c, "edgeStyle");
            if (edge_style.find("rthogonal") != std::string::npos || edge_style.find("elbow") != std::string::npos ||
                edge_style.find("isometric") != std::string::npos)
            {
                e.style.routing = "orthogonal";
            }
            else if (StyleFlag(c, "curved"))
            {
                e.style.routing = "curved";
            }
            else
            {
                e.style.routing = "straight";
            }
            const std::string end_arrow = StyleValue(c, "endArrow", "classic");
            if (MapArrow(end_arrow) != "triangle")
            {
                e.style.arrow_end = MapArrow(end_arrow);
            }
            if (const std::string start_arrow = StyleValue(c, "startArrow", "none"); start_arrow != "none")
            {
                e.style.arrow_start = MapArrow(start_arrow);
            }
            if (StyleFlag(c, "dashed"))
            {
                e.style.stroke_style = "dashed";
            }
            if (const double sw = Num(StyleValue(c, "strokeWidth"), 0); sw > 0)
            {
                e.style.width = std::clamp(sw, 0.5, 12.0);
            }
            if (opt.keep_colors)
            {
                e.style.color       = StyleColor(c, "strokeColor");
                e.style.label_color = StyleColor(c, "fontColor");
            }
            m.edges.push_back(std::move(e));
            ++rep.edges;
        }

        m.scenario.duration = 12000;
        NormalizeModel(m);
        return m;
    }
    catch (const AdError &e)
    {
        return std::unexpected(std::string(e.what()));
    }
}

} // namespace ad
