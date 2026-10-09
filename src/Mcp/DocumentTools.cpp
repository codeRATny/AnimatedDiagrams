#include "DocumentTools.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <memory>

#include "Common/Exceptions.hpp"
#include "Engine/Design.hpp"
#include "Engine/Layout.hpp"
#include "Engine/Templates.hpp"
#include "Import/DrawioImporter.hpp"
#include "Io/JsonCodec.hpp"
#include "Io/JsonIo.hpp"
#include "Model/Sample.hpp"
#include "Timeline/TimelineLayout.hpp"
#include "Utils/File.hpp"
#include "Utils/Text.hpp"

namespace ad::mcp
{

std::vector<DocumentInfo> DocumentHost::Documents() { return {DocumentInfo{Doc().Get().meta.name, CurrentPath(), true, false}}; }

bool DocumentHost::SelectDocument(size_t index) { return index == 0; }

std::expected<std::vector<uint8_t>, std::string> DocumentHost::RenderPng(double /*time_ms*/, double /*scale*/)
{
    return std::unexpected(std::string("rendering is not available in this host"));
}

std::expected<std::string, std::string> DocumentHost::Export(const ExportRequest & /*request*/)
{
    return std::unexpected(std::string("export is not available in this host"));
}

namespace
{

// ---------------------------------------------------------------------------
// Schema helpers
// ---------------------------------------------------------------------------

Json Prop(const char *type, const char *description) { return Json{{"type", type}, {"description", description}}; }

Json EnumProp(std::vector<std::string> values, const char *description)
{
    return Json{{"type", "string"}, {"enum", std::move(values)}, {"description", description}};
}

Json Schema(Json properties, std::vector<std::string> required = {})
{
    Json s{{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty())
    {
        s["required"] = std::move(required);
    }
    return s;
}

std::string RequireString(const Json &args, const char *key)
{
    if (!args.contains(key) || !args[key].is_string() || args[key].get<std::string>().empty())
    {
        throw ToolError(std::string("argument '") + key + "' (string) is required");
    }
    return args[key].get<std::string>();
}

Json ToPlain(const json::OrderedJson &j) { return Json::parse(j.dump()); }

std::vector<std::string> OptionIds(std::span<const OptionInfo> options)
{
    std::vector<std::string> out;
    for (const auto &o : options)
    {
        out.emplace_back(o.id);
    }
    return out;
}

Json NodeStyleSchema()
{
    return Json{{"type", "object"},
                {"description", "Style overrides: shape, customPath, fill, stroke, strokeWidth, strokeStyle, cornerRadius, textColor, "
                                "fontSize, opacity, shadow, showIcon"},
                {"properties",
                 {{"shape", EnumProp(OptionIds(Shapes()), "Node outline")},
                  {"customPath", Prop("string", "SVG path data in the unit square (shape=custom)")},
                  {"fill", Prop("string", "Body color #rrggbb")},
                  {"stroke", Prop("string", "Border color #rrggbb")},
                  {"strokeWidth", Prop("number", "Border width")},
                  {"strokeStyle", EnumProp(OptionIds(StrokeStyles()), "Border style")},
                  {"cornerRadius", Prop("number", "Corner radius")},
                  {"textColor", Prop("string", "Title color #rrggbb")},
                  {"fontSize", Prop("number", "Title font size")},
                  {"opacity", Prop("number", "0..1")},
                  {"shadow", Prop("boolean", "Drop shadow")},
                  {"showIcon", Prop("boolean", "Show the type icon")}}}};
}

Json EdgeStyleSchema()
{
    return Json{{"type", "object"},
                {"properties",
                 {{"color", Prop("string", "#rrggbb")},
                  {"width", Prop("number", "Line width")},
                  {"strokeStyle", EnumProp(OptionIds(StrokeStyles()), "Line style")},
                  {"arrowEnd", EnumProp(OptionIds(ArrowHeads()), "Arrow at the target")},
                  {"arrowStart", EnumProp(OptionIds(ArrowHeads()), "Arrow at the source")},
                  {"routing", EnumProp(OptionIds(Routings()), "Line routing")},
                  {"labelColor", Prop("string", "#rrggbb")}}}};
}

/// Accept friendly aliases used by agents (type -> kind, width -> w, height -> h, ...).
Json NormalizeNodeArgs(Json args)
{
    auto alias = [&](const char *from, const char *to)
    {
        if (args.contains(from) && !args.contains(to))
        {
            args[to] = args[from];
        }
        args.erase(from);
    };
    alias("type", "kind");
    alias("width", "w");
    alias("height", "h");
    return args;
}

Json NormalizeStepArgs(Json args)
{
    if (args.contains("node") && !args.contains("nodeId"))
    {
        args["nodeId"] = args["node"];
    }
    if (args.contains("edge") && !args.contains("edgeId"))
    {
        args["edgeId"] = args["edge"];
    }
    args.erase("node");
    args.erase("edge");
    return args;
}

class Tools : public std::enable_shared_from_this<Tools>
{
public:
    Tools(McpServer &server, DocumentHost &host) : _server(server), _host(host) {}

    void Register()
    {
        _Add("get_summary", "Document overview",
             "Short text overview of the current diagram: nodes with ids, edges, scenario steps and the library. Call this first.",
             Schema(Json::object()), true,
             [this](const Json &)
             {
                 return ToolResult::Text(DocumentSummary(_M(), _host.Reg()));
             });

        _Add("get_document", "Document JSON", "Full document in the native JSON format (same as .json files).",
             Schema({{"includeLibrary", Prop("boolean", "Include the embedded library (default true)")}}), true,
             [this](const Json &a)
             {
                 Json doc = ToPlain(json::ToJson(_M()));
                 if (!a.value("includeLibrary", true))
                 {
                     doc.erase("library");
                 }
                 return ToolResult::FromJson(doc);
             });

        _Add("list_library", "List library",
             "Element types (for add_node 'type'), effects (for effect steps), animation templates (for apply_animation), design "
             "systems (for set_scene designSystem) and the fixed catalogs (shapes, states, message variants...).",
             Schema({{"kind",
                      EnumProp({"elements", "effects", "animations", "designSystems", "catalogs", "all"}, "What to list (default all)")}}),
             true,
             [this](const Json &a)
             {
                 return ToolResult::FromJson(_Library(a.value("kind", std::string("all"))));
             });

        _Add("list_documents", "List documents", "Documents open in the editor (tabs) with their index; tools work on the active one.",
             Schema(Json::object()), true,
             [this](const Json & /*a*/)
             {
                 Json   arr = Json::array();
                 size_t i   = 0;
                 for (const auto &d : _host.Documents())
                 {
                     arr.push_back({{"index", i++}, {"name", d.name}, {"path", d.path}, {"active", d.active}, {"modified", d.modified}});
                 }
                 return ToolResult::FromJson(Json{{"documents", arr}});
             });

        _Add("select_document", "Select document", "Make the document with this index (see list_documents) the active one.",
             Schema({{"index", Prop("integer", "Document index")}}, {"index"}), false,
             [this](const Json &a)
             {
                 if (!a.contains("index") || !a["index"].is_number_integer() || a["index"].get<int64_t>() < 0 ||
                     !_host.SelectDocument(static_cast<size_t>(a["index"].get<int64_t>())))
                 {
                     throw ToolError("no document with this index (see list_documents)");
                 }
                 return ToolResult::Text("Active document: " + _M().meta.name + "\n\n" + DocumentSummary(_M(), _host.Reg()));
             });

        _Add("new_document", "New document", "Create an empty document (or the bundled example). In the editor it opens in a new tab.",
             Schema({{"name", Prop("string", "Diagram name")}, {"example", Prop("boolean", "Load the bundled example instead")}}), false,
             [this](const Json &a)
             {
                 Model m = a.value("example", false) ? SampleModel() : EmptyModel();
                 if (a.contains("name") && a["name"].is_string())
                 {
                     m.meta.name = a["name"].get<std::string>();
                 }
                 _host.BeginNewDocument();
                 _host.Doc().Reset(std::move(m));
                 _host.SetCurrentPath({});
                 _host.Replaced();
                 return ToolResult::Text("New document created");
             });

        _Add("open_document", "Open file", "Open a diagram .json file from disk (in the editor: in a new tab).",
             Schema({{"path", Prop("string", "File path")}}, {"path"}), false,
             [this](const Json &a)
             {
                 const std::string path   = RequireString(a, "path");
                 auto              parsed = ParseModel(ReadFile(PathFromUtf8(path)));
                 if (!parsed.has_value())
                 {
                     throw ToolError(parsed.error());
                 }
                 _host.BeginNewDocument();
                 _host.Doc().Reset(std::move(*parsed));
                 _host.SetCurrentPath(path);
                 _host.Replaced();
                 return ToolResult::Text("Opened " + path + "\n\n" + DocumentSummary(_M(), _host.Reg()));
             });

        _Add("save_document", "Save file",
             "Save the diagram as .json. Plugin definitions used by the diagram are embedded so the file renders anywhere.",
             Schema({{"path", Prop("string", "Target path (default: the current file)")}}), false,
             [this](const Json &a)
             {
                 std::string path = a.value("path", _host.CurrentPath());
                 if (path.empty())
                 {
                     throw ToolError("no current file: pass 'path'");
                 }
                 Model copy = _M();
                 _host.Reg().EmbedUsedDefinitions(copy);
                 WriteFile(PathFromUtf8(path), SerializeModel(copy));
                 _host.SetCurrentPath(path);
                 return ToolResult::Text("Saved " + path);
             });

        _Add("import_drawio", "Import draw.io",
             "Replace the document with a page of a draw.io / diagrams.net file (.drawio, .xml). Pass 'path' or the file content in "
             "'xml'.",
             Schema({{"path", Prop("string", "File path")},
                     {"xml", Prop("string", "File content")},
                     {"page", Prop("integer", "Page index (default 0)")},
                     {"keepColors", Prop("boolean", "Keep draw.io colors (default true)")}}),
             false,
             [this](const Json &a)
             {
                 std::string content = a.value("xml", std::string{});
                 if (content.empty())
                 {
                     content = ReadFile(PathFromUtf8(RequireString(a, "path")));
                 }
                 DrawioImportOptions opt;
                 opt.page        = a.value("page", 0);
                 opt.keep_colors = a.value("keepColors", true);
                 DrawioImportReport rep;
                 auto               model = ImportDrawio(content, opt, &rep);
                 if (!model.has_value())
                 {
                     throw ToolError(model.error());
                 }
                 _host.BeginNewDocument();
                 _host.Doc().Reset(std::move(*model));
                 _host.SetCurrentPath({});
                 _host.Replaced();
                 return ToolResult::Text(std::format("Imported {} nodes, {} edges, {} notes ({} cells skipped)\n\n{}", rep.nodes, rep.edges,
                                                     rep.notes, rep.skipped, DocumentSummary(_M(), _host.Reg())));
             });

        _RegisterNodeTools();
        _RegisterEdgeTools();
        _RegisterStepTools();
        _RegisterLibraryTools();

        _Add("set_scene", "Scene settings",
             "Change document name, scene duration, canvas colors and the design system. A design system is applied first, "
             "explicit colors in the same call win.",
             Schema({{"name", Prop("string", "Diagram name")},
                     {"designSystem", Prop("string", "Design system id (see list_library), empty string -- detach")},
                     {"description", Prop("string", "Diagram description")},
                     {"durationMs", Prop("number", "Scene duration in ms (marks it as user-defined)")},
                     {"background", Prop("string", "Canvas / export background #rrggbb")},
                     {"grid", Prop("boolean", "Show grid")},
                     {"edgeColor", Prop("string", "Default edge color")},
                     {"textColor", Prop("string", "Default node text color")}}),
             false,
             [this](const Json &a)
             {
                 const DesignSystem *ds = nullptr;
                 if (a.contains("designSystem") && a["designSystem"].is_string() && !a["designSystem"].get<std::string>().empty())
                 {
                     ds = _host.Reg().FindDesignSystem(a["designSystem"].get<std::string>(), &_M().library);
                     if (ds == nullptr)
                     {
                         throw ToolError("design system '" + a["designSystem"].get<std::string>() + "' not found (see list_library)");
                     }
                 }
                 const DesignSystem ds_copy = ds != nullptr ? *ds : DesignSystem{}; // the registry entry may live in the document
                 _host.Doc().Checkpoint();
                 Model &m = _host.Doc().Mutable();
                 if (ds != nullptr)
                 {
                     ApplyDesignSystem(m, ds_copy);
                 }
                 else if (a.contains("designSystem") && a["designSystem"].is_string())
                 {
                     ClearDesignSystem(m);
                 }
                 if (a.contains("name") && a["name"].is_string())
                 {
                     m.meta.name = a["name"].get<std::string>();
                 }
                 if (a.contains("description") && a["description"].is_string())
                 {
                     m.meta.description = a["description"].get<std::string>();
                 }
                 if (a.contains("durationMs") && a["durationMs"].is_number())
                 {
                     m.scenario.duration      = std::clamp(a["durationMs"].get<double>(), 1000.0, 24.0 * 3600 * 1000);
                     m.scenario.user_duration = true;
                 }
                 auto color = [&](const char *key, std::string &dst)
                 {
                     if (a.contains(key) && a[key].is_string())
                     {
                         if (!Color::Parse(a[key].get<std::string>()).has_value())
                         {
                             throw ToolError(std::string(key) + " must be #rrggbb");
                         }
                         dst = a[key].get<std::string>();
                     }
                 };
                 color("background", m.scene.background);
                 color("edgeColor", m.scene.edge_color);
                 color("textColor", m.scene.text_color);
                 if (a.contains("grid") && a["grid"].is_boolean())
                 {
                     m.scene.grid = a["grid"].get<bool>();
                 }
                 _host.Changed(true);
                 return ToolResult::Text("Scene updated");
             });

        _Add("auto_layout", "Auto layout", "Arrange nodes in layers following the edges (clears waypoints).",
             Schema({{"direction", EnumProp({"LR", "TB"}, "Left-to-right (default) or top-to-bottom")}}), false,
             [this](const Json &a)
             {
                 LayoutOptions opt;
                 opt.direction =
                     a.value("direction", std::string("LR")) == "TB" ? LayoutDirection::TopToBottom : LayoutDirection::LeftToRight;
                 _host.Doc().Checkpoint();
                 AutoLayout(_host.Doc().Mutable(), opt);
                 _host.Changed(true);
                 return ToolResult::Text("Layout applied");
             });

        _Add("render_frame", "Render frame",
             "Render the diagram at a moment of the scenario as a PNG image -- use it to check the result visually.",
             Schema({{"timeMs", Prop("number", "Moment of the scenario in ms (default 0)")},
                     {"scale", Prop("number", "Resolution scale (default 1)")}}),
             true,
             [this](const Json &a)
             {
                 const double t     = std::clamp(a.value("timeMs", 0.0), 0.0, _M().scenario.duration);
                 const double scale = std::clamp(a.value("scale", 1.0), 0.25, 4.0);
                 auto         png   = _host.RenderPng(t, scale);
                 if (!png.has_value())
                 {
                     throw ToolError(png.error());
                 }
                 ToolResult r = ToolResult::Image(Base64Encode(*png), "image/png");
                 r.content.push_back({"text", std::format("Frame at {:.0f} ms", t), {}, {}});
                 return r;
             });

        _Add("export_animation", "Export animation",
             "Export the whole scenario to GIF, PNG frames, WebM (VP9), MP4 (H.264) or a PowerPoint presentation (.pptx). "
             "PowerPoint modes: video (MP4 slides playing automatically), gif (also for Google Slides), animated (editable "
             "shapes with PowerPoint animations), morph (key frame slides with the Morph transition). Markers split the "
             "scenario into slides; insertInto adds the slides to an existing presentation (written to path).",
             Schema({{"path", Prop("string", "Output file")},
                     {"format", EnumProp({"gif", "png", "webm", "mp4", "pptx"}, "Default: by extension")},
                     {"fps", Prop("number", "Frames per second (default 15)")},
                     {"scale", Prop("number", "Resolution scale (default 1)")},
                     {"quality", Prop("integer", "WebM / MP4 quality 0 (smallest) .. 4 (best), default 2")},
                     {"pptxMode", EnumProp({"video", "gif", "animated", "morph"}, "PowerPoint slides (default video)")},
                     {"slideSize", EnumProp({"16:9", "4:3"}, "Slide size of a new presentation (default 16:9)")},
                     {"insertInto", Prop("string", "Existing .pptx to add the slides to")},
                     {"insertAfter", Prop("integer", "Insert after this 1-based slide (default: at the end)")},
                     {"bySegments", Prop("boolean", "A slide per segment between markers (default true)")}},
                    {"path"}),
             false,
             [this](const Json &a)
             {
                 ExportRequest req;
                 req.path         = RequireString(a, "path");
                 req.format       = a.value("format", std::string{});
                 req.fps          = std::clamp(a.value("fps", 15.0), 1.0, 60.0);
                 req.scale        = std::clamp(a.value("scale", 1.0), 0.25, 8.0);
                 req.quality      = std::clamp(a.value("quality", 2), 0, 4);
                 req.pptx_mode    = a.value("pptxMode", std::string("video"));
                 req.slide_size   = a.value("slideSize", std::string("16:9"));
                 req.insert_into  = a.value("insertInto", std::string{});
                 req.insert_after = a.value("insertAfter", -1);
                 req.by_markers   = a.value("bySegments", true);
                 auto res         = _host.Export(req);
                 if (!res.has_value())
                 {
                     throw ToolError(res.error());
                 }
                 return ToolResult::Text(*res);
             });

        _Add("undo", "Undo", "Undo the last change.", Schema(Json::object()), false,
             [this](const Json &)
             {
                 const bool ok = _host.Doc().Undo();
                 _host.Changed(true);
                 return ToolResult::Text(ok ? "Undone" : "Nothing to undo");
             });
        _Add("redo", "Redo", "Redo the last undone change.", Schema(Json::object()), false,
             [this](const Json &)
             {
                 const bool ok = _host.Doc().Redo();
                 _host.Changed(true);
                 return ToolResult::Text(ok ? "Redone" : "Nothing to redo");
             });
    }

private:
    const Model &_M() { return _host.Doc().Get(); }

    void _Add(std::string name, std::string title, std::string description, Json schema, bool read_only, ToolHandler handler)
    {
        // handlers capture `this`; every handler keeps the Tools object alive
        ToolHandler keep_alive = [self = shared_from_this(), h = std::move(handler)](const Json &args)
        {
            return h(args);
        };
        _server.AddTool(
            ToolSpec{std::move(name), std::move(title), std::move(description), std::move(schema), read_only, std::move(keep_alive)});
    }

    // -----------------------------------------------------------------------
    // Nodes
    // -----------------------------------------------------------------------
    void _RegisterNodeTools()
    {
        const Json node_props{{"label", Prop("string", "Title")},
                              {"type", Prop("string", "Element type id (see list_library), default 'service'")},
                              {"x", Prop("number", "Left, px (default: right of the existing nodes)")},
                              {"y", Prop("number", "Top, px")},
                              {"width", Prop("number", "Width, px (default from the type)")},
                              {"height", Prop("number", "Height, px")},
                              {"subtitle", Prop("string", "Second line shown in the default state")},
                              {"accent", Prop("string", "Accent stripe color #rrggbb")},
                              {"style", NodeStyleSchema()}};

        _Add("add_node", "Add node", "Add a node (service, database, queue, ...). Returns its id.", Schema(node_props, {"label"}), false,
             [this](const Json &raw)
             {
                 const Json         a     = NormalizeNodeArgs(raw);
                 const std::string  label = RequireString(a, "label"); // validate before mutating
                 const std::string  kind  = a.value("kind", std::string(kDefaultType));
                 const ElementType &type  = _host.Reg().Element(kind, &_M().library);
                 double             x     = 80;
                 double             y     = 120;
                 for (const auto &n : _M().nodes)
                 {
                     x = std::max(x, n.x + n.w + 80);
                     y = n.y;
                 }
                 const double w = a.value("w", type.width);
                 const double h = a.value("h", type.height);
                 const Vec2   c{a.value("x", x) + w / 2, a.value("y", y) + h / 2};
                 Node        &n = _host.Doc().AddNode(c, type);
                 n.type         = kind; // keep unknown plugin types as given
                 n.w            = std::max(20.0, w);
                 n.h            = std::max(20.0, h);
                 n.x            = c.x - n.w / 2;
                 n.y            = c.y - n.h / 2;
                 n.label        = label;
                 n.subtitle     = a.value("subtitle", std::string{});
                 n.accent       = a.value("accent", std::string{});
                 if (a.contains("style"))
                 {
                     n.style = json::NodeStyleFromJson(a["style"]);
                 }
                 const std::string id = n.id;
                 _host.Changed(true);
                 return ToolResult::FromJson(Json{{"id", id}});
             });

        Json update_props  = node_props;
        update_props["id"] = Prop("string", "Node id");
        _Add("update_node", "Update node", "Change node fields; 'style' is merged (null removes a style field).",
             Schema(update_props, {"id"}), false,
             [this](const Json &raw)
             {
                 const std::string id = RequireString(raw, "id");
                 const Node       *n  = _M().FindNode(id);
                 if (n == nullptr)
                 {
                     throw ToolError("node '" + id + "' not found");
                 }
                 Json patch = NormalizeNodeArgs(raw);
                 patch.erase("id");
                 Json cur = ToPlain(json::ToJson(*n));
                 cur.merge_patch(patch);
                 Node updated = json::NodeFromJson(cur);
                 updated.id   = id;
                 _host.Doc().Checkpoint();
                 *_host.Doc().Mutable().FindNode(id) = std::move(updated);
                 _host.Changed(true);
                 return ToolResult::Text("Node " + id + " updated");
             });

        _Add("remove_node", "Remove node", "Remove a node with its edges and steps.", Schema({{"id", Prop("string", "Node id")}}, {"id"}),
             false,
             [this](const Json &a)
             {
                 const std::string id = RequireString(a, "id");
                 if (_M().FindNode(id) == nullptr)
                 {
                     throw ToolError("node '" + id + "' not found");
                 }
                 _host.Doc().RemoveNode(id);
                 _host.Changed(true);
                 return ToolResult::Text("Node " + id + " removed");
             });
    }

    // -----------------------------------------------------------------------
    // Edges
    // -----------------------------------------------------------------------
    void _RegisterEdgeTools()
    {
        const Json edge_props{{"from", Prop("string", "Source node id")},
                              {"to", Prop("string", "Target node id")},
                              {"label", Prop("string", "Label")},
                              {"curve", Prop("number", "Curvature -0.5..0.5 (routing=curved)")},
                              {"style", EdgeStyleSchema()}};
        _Add("add_edge", "Add edge", "Connect two nodes. Messages fly along edges. Returns the edge id.",
             Schema(edge_props, {"from", "to"}), false,
             [this](const Json &a)
             {
                 const std::string from = RequireString(a, "from");
                 const std::string to   = RequireString(a, "to");
                 Edge             *e    = _host.Doc().AddEdge(from, to);
                 if (e == nullptr)
                 {
                     throw ToolError("cannot connect '" + from + "' and '" + to + "' (missing node or the same node)");
                 }
                 e->label = a.value("label", std::string{});
                 if (a.contains("curve") && a["curve"].is_number())
                 {
                     e->curve = std::clamp(a["curve"].get<double>(), -0.5, 0.5);
                 }
                 if (a.contains("style"))
                 {
                     e->style = json::EdgeStyleFromJson(a["style"]);
                 }
                 const std::string id = e->id;
                 _host.Changed(true);
                 return ToolResult::FromJson(Json{{"id", id}});
             });

        Json update_props  = edge_props;
        update_props["id"] = Prop("string", "Edge id");
        _Add("update_edge", "Update edge", "Change edge fields; 'style' is merged.", Schema(update_props, {"id"}), false,
             [this](const Json &a)
             {
                 const std::string id = RequireString(a, "id");
                 const Edge       *e  = _M().FindEdge(id);
                 if (e == nullptr)
                 {
                     throw ToolError("edge '" + id + "' not found");
                 }
                 Json patch = a;
                 patch.erase("id");
                 Json cur = ToPlain(json::ToJson(*e));
                 cur.merge_patch(patch);
                 Edge updated = json::EdgeFromJson(cur);
                 updated.id   = id;
                 if (_M().FindNode(updated.from) == nullptr || _M().FindNode(updated.to) == nullptr || updated.from == updated.to)
                 {
                     throw ToolError("edge endpoints must be two existing nodes");
                 }
                 _host.Doc().Checkpoint();
                 *_host.Doc().Mutable().FindEdge(id) = std::move(updated);
                 _host.Changed(true);
                 return ToolResult::Text("Edge " + id + " updated");
             });

        _Add("remove_edge", "Remove edge", "Remove an edge (link steps on it are removed too).",
             Schema({{"id", Prop("string", "Edge id")}}, {"id"}), false,
             [this](const Json &a)
             {
                 const std::string id = RequireString(a, "id");
                 if (_M().FindEdge(id) == nullptr)
                 {
                     throw ToolError("edge '" + id + "' not found");
                 }
                 _host.Doc().RemoveEdge(id);
                 _host.Changed(true);
                 return ToolResult::Text("Edge " + id + " removed");
             });
    }

    // -----------------------------------------------------------------------
    // Steps
    // -----------------------------------------------------------------------
    void _RegisterStepTools()
    {
        const Json step_props{{"type", EnumProp({"message", "timer", "state", "action", "link", "note", "effect"}, "Step type")},
                              {"start", Prop("number", "Start, ms")},
                              {"duration", Prop("number", "Duration, ms")},
                              {"from", Prop("string", "message: source node id")},
                              {"to", Prop("string", "message: target node id")},
                              {"edgeId", Prop("string", "message: edge to fly along (default: any edge between from/to); link: the edge")},
                              {"variant", Prop("string", "message: request | response | retry | error | success | event")},
                              {"label", Prop("string", "message / timer / state label")},
                              {"packet", EnumProp(OptionIds(PacketShapes()), "message: packet shape")},
                              {"packetSize", Prop("number", "message: packet scale (default 1)")},
                              {"packetCount", Prop("integer", "message: number of packets in a stream (1..10)")},
                              {"easing", Prop("string", "message: easing (linear, ease-in-out, back-out, bounce-out, ...)")},
                              {"trail", Prop("boolean", "message: highlight the path")},
                              {"nodeId", Prop("string", "timer / state / action / effect: target node id")},
                              {"seconds", Prop("number", "timer: countdown value")},
                              {"unit", Prop("string", "timer: s | m | h | d")},
                              {"state", Prop("string", "state: ok | active | busy | warn | down | success | disabled")},
                              {"text", Prop("string", "action / link / note text")},
                              {"color", Prop("string", "Color override #rrggbb")},
                              {"anim", Prop("string", "link: flow | dash | solid | pulse")},
                              {"x", Prop("number", "note: left, px")},
                              {"y", Prop("number", "note: top, px")},
                              {"effect", Prop("string", "effect: effect id (see list_library)")},
                              {"intensity", Prop("number", "effect: amplitude multiplier")},
                              {"repeat", Prop("integer", "effect: repetitions (0 = effect default)")},
                              {"labelSize", Prop("number", "state / link label font size")},
                              {"labelPos", Prop("number", "link: label position 0..1")},
                              {"labelOff", Prop("number", "link: label offset, px")}};

        _Add("add_step", "Add scenario step",
             "Add a step to the timeline: message between nodes, timer, state change, action, link animation, note or effect. "
             "Returns its id.",
             Schema(step_props, {"type"}), false,
             [this](const Json &raw)
             {
                 Json a = NormalizeStepArgs(raw);
                 a.erase("id");
                 const auto type = StepTypeFromString(a.value("type", std::string{}));
                 if (!type.has_value())
                 {
                     throw ToolError("unknown step type");
                 }
                 // start from the defaults for the type, then apply the arguments
                 auto base = _host.Doc().MakeDefaultStep(*type, a.value("start", 0.0));
                 if (!base.has_value())
                 {
                     throw ToolError("the document has no nodes / edges for this step type");
                 }
                 Json merged = ToPlain(json::ToJson(*base));
                 merged.merge_patch(a);
                 auto step = json::StepFromJson(merged);
                 if (!step.has_value())
                 {
                     throw ToolError("invalid step");
                 }
                 _ValidateStep(*step);
                 if (step->type == StepType::Message && step->edge_id.empty())
                 {
                     if (const Edge *e = _M().EdgeBetween(step->from, step->to); e != nullptr)
                     {
                         step->edge_id = e->id;
                     }
                 }
                 const std::string id = _host.Doc().AddStep(std::move(*step)).id;
                 _host.Changed(true);
                 return ToolResult::FromJson(Json{{"id", id}});
             });

        Json update_props  = step_props;
        update_props["id"] = Prop("string", "Step id");
        _Add("update_step", "Update step", "Change step fields (any field of add_step).", Schema(update_props, {"id"}), false,
             [this](const Json &raw)
             {
                 const std::string id = RequireString(raw, "id");
                 const Step       *s  = _M().FindStep(id);
                 if (s == nullptr)
                 {
                     throw ToolError("step '" + id + "' not found");
                 }
                 Json patch = NormalizeStepArgs(raw);
                 patch.erase("id");
                 Json cur = ToPlain(json::ToJson(*s));
                 cur.merge_patch(patch);
                 auto step = json::StepFromJson(cur);
                 if (!step.has_value())
                 {
                     throw ToolError("invalid step");
                 }
                 step->id       = id;
                 step->duration = std::max(kMinStepDuration, step->duration);
                 _ValidateStep(*step);
                 _host.Doc().Checkpoint();
                 *_host.Doc().Mutable().FindStep(id) = std::move(*step);
                 _host.Doc().SortSteps();
                 _host.Doc().UpdateDuration();
                 _host.Changed(true);
                 return ToolResult::Text("Step " + id + " updated");
             });

        _Add("remove_step", "Remove step", "Remove a scenario step.", Schema({{"id", Prop("string", "Step id")}}, {"id"}), false,
             [this](const Json &a)
             {
                 const std::string id = RequireString(a, "id");
                 if (_M().FindStep(id) == nullptr)
                 {
                     throw ToolError("step '" + id + "' not found");
                 }
                 _host.Doc().RemoveStep(id);
                 _host.Changed(true);
                 return ToolResult::Text("Step " + id + " removed");
             });

        _Add("apply_animation", "Insert animation template",
             "Insert an animation template (see list_library) into the scenario, mapping its roles to node ids.",
             Schema({{"template", Prop("string", "Template id")},
                     {"roles", Json{{"type", "object"}, {"description", "role id -> node id"}}},
                     {"start", Prop("number", "Insertion time, ms (default: end of the scenario)")}},
                    {"template", "roles"}),
             false,
             [this](const Json &a)
             {
                 const std::string        tid = RequireString(a, "template");
                 const AnimationTemplate *tpl = _host.Reg().FindAnimation(tid, &_M().library);
                 if (tpl == nullptr)
                 {
                     throw ToolError("animation template '" + tid + "' not found");
                 }
                 std::map<std::string, std::string> roles;
                 if (a.contains("roles") && a["roles"].is_object())
                 {
                     for (const auto &[k, v] : a["roles"].items())
                     {
                         if (v.is_string())
                         {
                             roles[k] = v.get<std::string>();
                         }
                     }
                 }
                 double start = 0;
                 for (const auto &s : _M().scenario.steps)
                 {
                     start = std::max(start, s.End());
                 }
                 start          = a.value("start", start);
                 const auto ids = ApplyTemplate(_host.Doc(), *tpl, roles, start);
                 _host.Changed(true);
                 return ToolResult::FromJson(Json{{"steps", ids}});
             });
    }

    void _ValidateStep(const Step &s)
    {
        const Model &m = _M();
        switch (s.type)
        {
        case StepType::Message:
            if (m.FindNode(s.from) == nullptr || m.FindNode(s.to) == nullptr)
            {
                throw ToolError("message: 'from' and 'to' must be existing node ids");
            }
            break;
        case StepType::Link:
            if (m.FindEdge(s.edge_id) == nullptr)
            {
                throw ToolError("link: 'edgeId' must be an existing edge id");
            }
            break;
        case StepType::Note:
            break;
        default:
            if (m.FindNode(s.node_id) == nullptr)
            {
                throw ToolError(std::string(ToString(s.type)) + ": 'nodeId' must be an existing node id");
            }
        }
        if (s.type == StepType::Effect && _host.Reg().FindEffect(s.effect, &m.library) == nullptr)
        {
            throw ToolError("effect '" + s.effect + "' not found (see list_library)");
        }
    }

    // -----------------------------------------------------------------------
    // Library
    // -----------------------------------------------------------------------
    Json _Library(const std::string &kind)
    {
        Json        out = Json::object();
        const auto &reg = _host.Reg();
        const auto *doc = &_M().library;
        if (kind == "elements" || kind == "all")
        {
            Json arr = Json::array();
            for (const auto &e : reg.Elements(doc))
            {
                arr.push_back({{"id", e.def->id},
                               {"label", e.def->label},
                               {"category", e.def->category},
                               {"shape", e.def->style.shape.value_or("rounded")},
                               {"source", e.source}});
            }
            out["elements"] = std::move(arr);
        }
        if (kind == "effects" || kind == "all")
        {
            Json arr = Json::array();
            for (const auto &e : reg.Effects(doc))
            {
                arr.push_back({{"id", e.def->id}, {"label", e.def->label}, {"description", e.def->description}, {"source", e.source}});
            }
            out["effects"] = std::move(arr);
        }
        if (kind == "animations" || kind == "all")
        {
            Json arr = Json::array();
            for (const auto &a : reg.Animations(doc))
            {
                Json roles = Json::array();
                for (const auto &r : a.def->roles)
                {
                    roles.push_back({{"id", r.id}, {"label", r.label}});
                }
                arr.push_back({{"id", a.def->id},
                               {"label", a.def->label},
                               {"description", a.def->description},
                               {"roles", roles},
                               {"source", a.source}});
            }
            out["animations"] = std::move(arr);
        }
        if (kind == "designSystems" || kind == "all")
        {
            Json arr = Json::array();
            for (const auto &d : reg.DesignSystems(doc))
            {
                Json tokens = Json::object();
                for (const auto &[k, v] : d.def->colors)
                {
                    tokens[k] = v;
                }
                arr.push_back({{"id", d.def->id},
                               {"label", d.def->label},
                               {"description", d.def->description},
                               {"tokens", tokens},
                               {"source", d.source}});
            }
            out["designSystems"]      = std::move(arr);
            out["activeDesignSystem"] = _M().design_system;
        }
        if (kind == "catalogs" || kind == "all")
        {
            Json states = Json::array();
            for (const auto &s : NodeStates())
            {
                states.push_back(s.id);
            }
            Json variants = Json::array();
            for (const auto &v : MsgVariants())
            {
                variants.push_back(v.id);
            }
            Json easings = Json::array();
            for (const auto &e : Easings())
            {
                easings.push_back(e.id);
            }
            out["catalogs"] = {{"shapes", OptionIds(Shapes())},
                               {"arrowHeads", OptionIds(ArrowHeads())},
                               {"strokeStyles", OptionIds(StrokeStyles())},
                               {"packetShapes", OptionIds(PacketShapes())},
                               {"routings", OptionIds(Routings())},
                               {"states", states},
                               {"messageVariants", variants},
                               {"easings", easings}};
        }
        return out;
    }

    void _RegisterLibraryTools()
    {
        _Add("upsert_library_item", "Create library item",
             "Create or replace a custom element type, effect, animation template or design system in the document library. "
             "'definition' uses the plugin JSON format (see the animated-diagrams skill / docs/plugins.md).",
             Schema({{"kind", EnumProp({"element", "effect", "animation", "design-system"}, "Definition kind")},
                     {"definition", Json{{"type", "object"}, {"description", "Definition JSON with an 'id'"}}}},
                    {"kind", "definition"}),
             false,
             [this](const Json &a)
             {
                 const std::string kind = RequireString(a, "kind");
                 const Json        def  = a.contains("definition") ? a["definition"] : Json();
                 json::Warnings    warnings;
                 std::string       id;
                 if (kind == "element")
                 {
                     auto e = json::ElementFromJson(def, &warnings);
                     if (!e.has_value())
                     {
                         throw ToolError("invalid element definition");
                     }
                     id = e->id;
                     _host.Doc().UpsertElement(*e);
                 }
                 else if (kind == "effect")
                 {
                     auto e = json::EffectFromJson(def, &warnings);
                     if (!e.has_value())
                     {
                         throw ToolError("invalid effect definition");
                     }
                     id = e->id;
                     _host.Doc().UpsertEffect(*e);
                 }
                 else if (kind == "animation")
                 {
                     auto t = json::AnimationFromJson(def, &warnings);
                     if (!t.has_value())
                     {
                         throw ToolError("invalid animation definition");
                     }
                     id = t->id;
                     _host.Doc().UpsertAnimation(*t);
                 }
                 else if (kind == "design-system")
                 {
                     auto d = json::DesignSystemFromJson(def, &warnings);
                     if (!d.has_value())
                     {
                         throw ToolError("invalid design system definition");
                     }
                     id = d->id;
                     _host.Doc().UpsertDesignSystem(*d);
                 }
                 else
                 {
                     throw ToolError("kind must be element, effect, animation or design-system");
                 }
                 _host.Changed(true);
                 Json res{{"id", id}};
                 if (!warnings.empty())
                 {
                     res["warnings"] = warnings;
                 }
                 return ToolResult::FromJson(res);
             });

        _Add("remove_library_item", "Remove library item", "Remove a definition from the document library.",
             Schema({{"id", Prop("string", "Definition id")}}, {"id"}), false,
             [this](const Json &a)
             {
                 const std::string id = RequireString(a, "id");
                 if (!_host.Doc().RemoveLibraryItem(id))
                 {
                     throw ToolError("'" + id + "' is not in the document library");
                 }
                 _host.Changed(true);
                 return ToolResult::Text("Removed " + id);
             });
    }

    McpServer    &_server;
    DocumentHost &_host;
};

} // namespace

std::string DocumentSummary(const Model &m, const Registry &reg)
{
    std::string out = std::format("Diagram \"{}\" -- scene {:.1f} s\n", m.meta.name, m.scenario.duration / 1000);
    out += std::format("\nNodes ({}):\n", m.nodes.size());
    for (const auto &n : m.nodes)
    {
        out += std::format("- {} \"{}\" [{}] at ({:.0f}, {:.0f}) {:.0f}x{:.0f}\n", n.id, n.label, n.type, n.x, n.y, n.w, n.h);
    }
    out += std::format("\nEdges ({}):\n", m.edges.size());
    for (const auto &e : m.edges)
    {
        out += std::format("- {}: {} -> {}{}\n", e.id, e.from, e.to, e.label.empty() ? "" : " \"" + e.label + "\"");
    }
    out += std::format("\nSteps ({}):\n", m.scenario.steps.size());
    for (const auto &s : m.scenario.steps)
    {
        out += std::format("- {} [{}] {:.2f}-{:.2f} s: {}\n", s.id, ToString(s.type), s.start / 1000, s.End() / 1000, StepTitle(m, s, reg));
    }
    out += std::format("\nDocument library: {} elements, {} effects, {} animations, {} design systems\n", m.library.elements.size(),
                       m.library.effects.size(), m.library.animations.size(), m.library.design_systems.size());
    if (const DesignSystem *ds = reg.DesignOf(m); ds != nullptr)
    {
        out += std::format("Design system: {} ({})\n", ds->id, ds->label);
    }
    return out;
}

void RegisterDocumentTools(McpServer &server, DocumentHost &host) { std::make_shared<Tools>(server, host)->Register(); }

std::string DefaultInstructions()
{
    return "Animated Diagrams: an editor of animated architecture diagrams. A document has nodes (services, databases, queues...), "
           "edges between them and a scenario -- timed steps: messages flying along edges, timers, state changes (e.g. down), "
           "actions, link animations, notes and keyframe effects. Workflow: get_summary -> list_library -> add_node / add_edge -> "
           "add_step or apply_animation -> render_frame at several times to verify -> save_document / export_animation. "
           "Times are in milliseconds. Use node ids returned by add_node.";
}

} // namespace ad::mcp
