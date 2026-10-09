#include "DrawioImporter.hpp"

#define ZLIB_CONST // const input pointer (z_stream::next_in)
#include <pugixml.hpp>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <memory>
#include <set>
#include <span>

#include "Common/Exceptions.hpp"
#include "Io/JsonIo.hpp"
#include "Utils/I18n.hpp"
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

/// Decompress a raw DEFLATE stream (draw.io: pako.deflateRaw) with zlib; at most `max_output` bytes.
std::string InflateRaw(std::span<const uint8_t> data, size_t max_output)
{
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
    {
        throw ParseError("zlib initialisation failed");
    }
    const std::unique_ptr<z_stream, int (*)(z_stream *)> guard(&zs, inflateEnd);

    std::string                 out;
    std::array<char, 64 * 1024> buf{};
    size_t                      pos = 0;
    while (true)
    {
        if (zs.avail_in == 0 && pos < data.size())
        {
            // zlib counts in uInt: feed large inputs in chunks
            const size_t chunk = std::min<size_t>(data.size() - pos, std::numeric_limits<uInt>::max());
            zs.next_in         = data.data() + pos;
            zs.avail_in        = static_cast<uInt>(chunk);
            pos += chunk;
        }
        zs.next_out           = reinterpret_cast<Bytef *>(buf.data());
        zs.avail_out          = static_cast<uInt>(buf.size());
        const int    rc       = inflate(&zs, Z_NO_FLUSH);
        const size_t produced = buf.size() - zs.avail_out;
        if (rc != Z_OK && rc != Z_STREAM_END)
        {
            // Z_BUF_ERROR: no progress possible -- the input ended before the end of the stream
            throw ParseError(std::string("corrupt compressed page: ") + (zs.msg != nullptr   ? zs.msg
                                                                         : rc == Z_BUF_ERROR ? "truncated data"
                                                                                             : "zlib error " + std::to_string(rc)));
        }
        if (produced > max_output - out.size())
        {
            throw ParseError("compressed page is larger than " + std::to_string(max_output) + " bytes");
        }
        out.append(buf.data(), produced);
        if (rc == Z_STREAM_END)
        {
            return out;
        }
    }
}

/// Load XML text into `doc`; throws ad::ParseError with the pugixml error and offset.
pugi::xml_node LoadXml(pugi::xml_document &doc, std::string_view text)
{
    const pugi::xml_parse_result r = doc.load_buffer(text.data(), text.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!r)
    {
        throw ParseError("XML: " + std::string(r.description()) + " at offset " + std::to_string(r.offset));
    }
    return doc.document_element();
}

/// Concatenated character data (text and CDATA) of an element.
std::string TextOf(pugi::xml_node node)
{
    std::string text;
    for (const pugi::xml_node child : node.children())
    {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
        {
            text += child.value();
        }
    }
    return text;
}

/// Locate the mxGraphModel of the requested page; decompresses compressed pages into `storage`.
pugi::xml_node PageModel(pugi::xml_node root, const DrawioImportOptions &opt, pugi::xml_document &storage, std::string &page_name)
{
    const std::string_view root_name = root.name();
    if (root_name == "mxGraphModel")
    {
        return root;
    }
    if (root_name != "mxfile")
    {
        throw ParseError("not a draw.io file (root element <" + std::string(root_name) + ">)");
    }
    std::vector<pugi::xml_node> diagrams;
    for (const pugi::xml_node d : root.children("diagram"))
    {
        diagrams.push_back(d);
    }
    if (diagrams.empty())
    {
        throw ParseError("draw.io file has no pages");
    }
    if (opt.page < 0 || static_cast<size_t>(opt.page) >= diagrams.size())
    {
        throw ParseError("page " + std::to_string(opt.page) + " does not exist (pages: " + std::to_string(diagrams.size()) + ")");
    }
    const pugi::xml_node d = diagrams[static_cast<size_t>(opt.page)];
    page_name              = d.attribute("name").as_string();
    if (const pugi::xml_node model = d.child("mxGraphModel"); !model.empty())
    {
        return model;
    }
    const std::string payload = Trim(TextOf(d));
    if (payload.empty())
    {
        throw ParseError("page '" + page_name + "' is empty");
    }
    // compressed page: base64 -> raw deflate -> URI-encoded XML
    const auto           bytes = Base64Decode(payload);
    const std::string    xml   = UrlDecode(InflateRaw(bytes, opt.max_inflated_size));
    const pugi::xml_node model = LoadXml(storage, xml);
    if (std::string_view(model.name()) != "mxGraphModel")
    {
        throw ParseError("compressed page does not contain an mxGraphModel");
    }
    return model;
}

Cell ReadCell(pugi::xml_node cell_node, pugi::xml_node wrapper)
{
    Cell c;
    c.id     = !wrapper.empty() ? wrapper.attribute("id").as_string() : cell_node.attribute("id").as_string();
    c.value  = !wrapper.empty() ? wrapper.attribute("label").as_string() : cell_node.attribute("value").as_string();
    c.parent = cell_node.attribute("parent").as_string();
    c.style  = ParseDrawioStyle(cell_node.attribute("style").as_string());
    c.vertex = std::string_view(cell_node.attribute("vertex").as_string()) == "1";
    c.edge   = std::string_view(cell_node.attribute("edge").as_string()) == "1";
    c.source = cell_node.attribute("source").as_string();
    c.target = cell_node.attribute("target").as_string();
    if (const pugi::xml_node g = cell_node.child("mxGeometry"); !g.empty())
    {
        auto num = [](pugi::xml_node n, const char *key)
        {
            return Num(n.attribute(key).as_string(), 0);
        };
        c.geo = {num(g, "x"), num(g, "y"), num(g, "width"), num(g, "height")};
        for (const pugi::xml_node arr : g.children("Array"))
        {
            if (std::string_view(arr.attribute("as").as_string()) == "points")
            {
                for (const pugi::xml_node p : arr.children("mxPoint"))
                {
                    c.points.push_back({num(p, "x"), num(p, "y")});
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

std::string DrawioDefaultName() { return Tr("document", "draw.io import"); }

std::vector<std::string> DrawioPageNames(std::string_view file_content)
{
    pugi::xml_document       doc;
    const pugi::xml_node     root = LoadXml(doc, file_content);
    std::vector<std::string> names;
    if (std::string_view(root.name()) == "mxGraphModel")
    {
        names.emplace_back("Page-1");
    }
    for (const pugi::xml_node d : root.children("diagram"))
    {
        const pugi::xml_attribute name = d.attribute("name");
        names.push_back(!name.empty() ? name.as_string() : "Page-" + std::to_string(names.size() + 1));
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
        pugi::xml_document   doc;
        pugi::xml_document   storage;
        std::string          page_name;
        const pugi::xml_node root       = LoadXml(doc, file_content);
        const pugi::xml_node gm         = PageModel(root, opt, storage, page_name);
        const pugi::xml_node cells_root = gm.child("root");
        if (cells_root.empty())
        {
            return std::unexpected(std::string("draw.io page has no <root>"));
        }

        std::vector<Cell> cells;
        for (const pugi::xml_node n : cells_root.children())
        {
            const std::string_view name = n.name();
            if (name == "mxCell")
            {
                cells.push_back(ReadCell(n, {}));
            }
            else if (name == "object" || name == "UserObject")
            {
                if (const pugi::xml_node inner = n.child("mxCell"); !inner.empty())
                {
                    cells.push_back(ReadCell(inner, n));
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
        m.meta.name = page_name.empty() ? DrawioDefaultName() : page_name;
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
