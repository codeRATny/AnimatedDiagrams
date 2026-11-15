#include "DocumentTools.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <map>
#include <memory>

#include "Common/Exceptions.hpp"
#include "Engine/Design.hpp"
#include "Engine/Engine.hpp"
#include "Engine/Layout.hpp"
#include "Engine/Templates.hpp"
#include "Export/MermaidExporter.hpp"
#include "Import/DrawioImporter.hpp"
#include "Import/MermaidImporter.hpp"
#include "Io/JsonCodec.hpp"
#include "Io/JsonIo.hpp"
#include "Model/Markers.hpp"
#include "Model/Sample.hpp"
#include "Timeline/TimelineLayout.hpp"
#include "Utils/File.hpp"
#include "Utils/Text.hpp"

namespace ad::mcp
{

DocumentHost::DocumentHost()
    : _expiry_worker(
          [this](std::stop_token stop)
          {
              std::unique_lock lock(_agents_mutex);
              while (!stop.stop_requested())
              {
                  _expiry_wait.wait_for(lock, stop, std::chrono::seconds(1),
                                        []
                                        {
                                            return false;
                                        });
                  _ExpireAgents(Clock::now());
              }
          })
{
}

std::string DocumentHost::RegisterAgent()
{
    static std::atomic<uint64_t> next_agent_id{1};
    const auto                   id       = std::to_string(next_agent_id.fetch_add(1, std::memory_order_relaxed));
    const auto                   document = ActiveDocumentKey();
    std::scoped_lock             lock(_agents_mutex);
    _agents.emplace(id, Agent{document, Clock::now()});
    return id;
}

DocumentHost::DocumentKey DocumentHost::TouchAgent(const std::string &id, Clock::time_point now)
{
    std::scoped_lock lock(_agents_mutex);
    _ExpireAgents(now);
    const auto it = _agents.find(id);
    if (it == _agents.end())
    {
        throw ToolError("unknown or expired agentId: " + id);
    }
    it->second.last_activity = now;
    return it->second.document;
}

bool DocumentHost::BindAgent(const std::string &id, DocumentKey document)
{
    std::scoped_lock lock(_agents_mutex);
    if (const auto it = _agents.find(id); it != _agents.end())
    {
        it->second.document = document;
        return true;
    }
    return false;
}

void DocumentHost::UnregisterAgent(const std::string &id)
{
    std::scoped_lock lock(_agents_mutex);
    _agents.erase(id);
}

void DocumentHost::_ExpireAgents(Clock::time_point now)
{
    std::erase_if(_agents,
                  [now](const auto &entry)
                  {
                      return now - entry.second.last_activity >= kAgentTimeout;
                  });
}

void DocumentHost::ExpireAgents(Clock::time_point now)
{
    std::scoped_lock lock(_agents_mutex);
    _ExpireAgents(now);
}

std::vector<std::string> DocumentHost::AgentsForDocument(DocumentKey document)
{
    std::scoped_lock lock(_agents_mutex);
    _ExpireAgents(Clock::now());
    std::vector<std::string> ids;
    for (const auto &[id, agent] : _agents)
    {
        if (agent.document == document)
        {
            ids.push_back(id);
        }
    }
    return ids;
}

std::vector<std::string> DocumentHost::UnregisterDocumentAgents(DocumentKey document)
{
    std::scoped_lock         lock(_agents_mutex);
    std::vector<std::string> ids;
    for (auto it = _agents.begin(); it != _agents.end();)
    {
        if (it->second.document == document)
        {
            ids.push_back(it->first);
            it = _agents.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return ids;
}

void DocumentHost::ClearAgents()
{
    std::scoped_lock lock(_agents_mutex);
    _agents.clear();
}

std::vector<DocumentInfo> DocumentHost::Documents()
{
    return {DocumentInfo{Doc().Get().meta.name, CurrentPath(), true, false, Doc().Revision()}};
}

bool DocumentHost::SelectDocument(size_t index, bool /*activate*/) { return index == 0; }

std::expected<std::vector<uint8_t>, std::string> DocumentHost::RenderPng(double /*time_ms*/, double /*scale*/)
{
    return std::unexpected(std::string("rendering is not available in this host"));
}

std::expected<std::string, std::string> DocumentHost::Export(const ExportRequest & /*request*/)
{
    return std::unexpected(std::string("export is not available in this host"));
}

std::vector<std::string> DocumentHost::ExportFormats() const { return {"gif", "png", "webm", "mp4"}; }

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

enum class AgentTarget
{
    None,
    Node,
    Edge,
    Step,
};

AgentTarget TargetForTool(std::string_view name)
{
    if (name.ends_with("_node"))
    {
        return AgentTarget::Node;
    }
    if (name.ends_with("_edge"))
    {
        return AgentTarget::Edge;
    }
    if (name.ends_with("_step") || name == "apply_animation")
    {
        return AgentTarget::Step;
    }
    return AgentTarget::None;
}

std::optional<Vec2> AgentPosition(AgentTarget target, const Model &model, const Registry &reg, const Json &id)
{
    if (!id.is_string())
    {
        return {};
    }
    const auto &key = id.get_ref<const std::string &>();
    if (target == AgentTarget::Node)
    {
        if (const auto *node = model.FindNode(key))
        {
            return node->Bounds().Center();
        }
    }
    if (target == AgentTarget::Edge)
    {
        if (const auto *edge = model.FindEdge(key))
        {
            if (const auto geometry = ComputeEdgeGeometry(model, *edge, reg); geometry && !geometry->path.Empty())
            {
                return FlatPath(geometry->path).PointAtFraction(0.5);
            }
        }
    }
    if (target == AgentTarget::Step)
    {
        if (const auto *step = model.FindStep(key))
        {
            if (step->type == StepType::Note)
            {
                return Vec2{step->x, step->y};
            }
            if (!step->edge_id.empty())
            {
                if (auto point = AgentPosition(AgentTarget::Edge, model, reg, step->edge_id))
                {
                    return point;
                }
            }
            return AgentPosition(AgentTarget::Node, model, reg, step->node_id.empty() ? step->from : step->node_id);
        }
    }
    return {};
}

class Tools : public std::enable_shared_from_this<Tools>
{
public:
    Tools(McpServer &server, DocumentHost &host) : _server(server), _host(host) {}

    void Register()
    {
        _Add("register_agent", "Register agent", "Issue a process-local agent identifier.", Schema(Json::object()), false,
             [this](const Json &)
             {
                 return ToolResult::FromJson(Json{{"agentId", _host.RegisterAgent()}});
             });

        _Add("unregister_agent", "Unregister agent", "End the agent session immediately.", Schema(Json::object(), {"agentId"}), false,
             [this](const Json &args)
             {
                 _host.UnregisterAgent(RequireString(args, "agentId"));
                 return ToolResult::FromJson(Json{{"unregistered", true}});
             });

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

        _Add("list_documents", "List documents", "List the editor tabs by index and indicate which tab is active in the GUI.",
             Schema(Json::object()), true,
             [this](const Json & /*a*/)
             {
                 Json   arr = Json::array();
                 size_t i   = 0;
                 for (const auto &d : _host.Documents())
                 {
                     arr.push_back({{"index", i++},
                                    {"name", d.name},
                                    {"path", d.path},
                                    {"active", d.active},
                                    {"modified", d.modified},
                                    {"revision", d.revision}});
                 }
                 return ToolResult::FromJson(Json{{"documents", arr}});
             });

        _Add("select_document", "Select document", "Bind the agent to this tab, or activate it in the GUI when called without an agentId.",
             Schema({{"index", Prop("integer", "Document index")}}, {"index"}), false,
             [this](const Json &a)
             {
                 if (!a.contains("index") || !a["index"].is_number_integer() || a["index"].get<int64_t>() < 0 ||
                     !_host.SelectDocument(static_cast<size_t>(a["index"].get<int64_t>()), !a.contains("agentId")))
                 {
                     throw ToolError("no document with this index (see list_documents)");
                 }
                 return ToolResult::Text(std::string(a.contains("agentId") ? "Agent document: " : "Active document: ") + _M().meta.name +
                                         "\n\n" + DocumentSummary(_M(), _host.Reg()));
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
                 _Doc().Reset(std::move(m));
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
                 _Doc().Reset(std::move(*parsed));
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
                 _Doc().Reset(std::move(*model));
                 _host.SetCurrentPath({});
                 _host.Replaced();
                 return ToolResult::Text(std::format("Imported {} nodes, {} edges, {} notes ({} cells skipped)\n\n{}", rep.nodes, rep.edges,
                                                     rep.notes, rep.skipped, DocumentSummary(_M(), _host.Reg())));
             });

        _Add("import_mermaid", "Import Mermaid",
             "Replace the document with a Mermaid diagram: a flowchart / graph (nodes, edges, shapes, colors, Mermaid's "
             "layout) and / or a sequenceDiagram (an animated scenario: participants, messages, notes, activations, par / "
             "alt / loop sections as markers). Pass the text in 'text' (a diagram or Markdown with ```mermaid blocks) or "
             "a file in 'path'. A quick way to start: write the flow as a sequenceDiagram, import it, then refine.",
             Schema({{"text", Prop("string", "Mermaid source or Markdown")},
                     {"path", Prop("string", "File path (.mmd, .mermaid, .md)")},
                     {"keepColors", Prop("boolean", "Keep style / classDef colors (default true)")}}),
             false,
             [this](const Json &a)
             {
                 if (!MermaidImportAvailable())
                 {
                     throw ToolError("this build has no Mermaid parser (WITH_MERMAID=OFF)");
                 }
                 std::string text = a.value("text", std::string{});
                 if (text.empty())
                 {
                     text = ReadFile(PathFromUtf8(RequireString(a, "path")));
                 }
                 MermaidImportOptions opt;
                 opt.keep_colors = a.value("keepColors", true);
                 MermaidImportReport rep;
                 auto                model = ImportMermaid(text, opt, &rep);
                 if (!model.has_value())
                 {
                     throw ToolError(model.error());
                 }
                 _host.BeginNewDocument();
                 _Doc().Reset(std::move(*model));
                 _host.SetCurrentPath({});
                 _host.Replaced();
                 std::string warnings;
                 for (const auto &w : rep.warnings)
                 {
                     warnings += "\nwarning: " + w;
                 }
                 return ToolResult::Text(std::format("Imported Mermaid {}: {} nodes, {} edges, {} steps, {} markers ({} skipped){}\n\n{}",
                                                     rep.kinds, rep.nodes, rep.edges, rep.steps, rep.markers, rep.skipped, warnings,
                                                     DocumentSummary(_M(), _host.Reg())));
             });

        _Add("to_mermaid", "Document as Mermaid",
             "The document as Mermaid text (no file is written): a flowchart of nodes and edges, a sequenceDiagram of the "
             "scenario, or Markdown with both. Useful to paste into a README, an issue or a chat.",
             Schema({{"kind", EnumProp({"flowchart", "sequence", "markdown"}, "Default flowchart")}}), true,
             [this](const Json &a)
             {
                 const auto kind = MermaidKindFromId(a.value("kind", std::string("flowchart")));
                 if (!kind.has_value())
                 {
                     throw ToolError("kind must be flowchart, sequence or markdown");
                 }
                 return ToolResult::Text(ExportMermaid(_M(), _host.Reg(), *kind));
             });

        _RegisterNodeTools();
        _RegisterEdgeTools();
        _RegisterStepTools();
        _RegisterMarkerTools();
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
                 _Doc().Checkpoint();
                 Model &m = _Doc().Mutable();
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
                 _Changed(true);
                 return ToolResult::Text("Scene updated");
             });

        _Add("auto_layout", "Auto layout", "Arrange nodes in layers following the edges (clears waypoints).",
             Schema({{"direction", EnumProp({"LR", "TB"}, "Left-to-right (default) or top-to-bottom")}}), false,
             [this](const Json &a)
             {
                 LayoutOptions opt;
                 opt.direction =
                     a.value("direction", std::string("LR")) == "TB" ? LayoutDirection::TopToBottom : LayoutDirection::LeftToRight;
                 _Doc().Checkpoint();
                 AutoLayout(_Doc().Mutable(), opt);
                 _Changed(true);
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
             "Export the whole scenario to GIF, PNG frames, WebM (VP9), MP4 (H.264), a PowerPoint presentation (.pptx) or "
             "(format html, when available) a self-contained interactive HTML player page for web presentations. "
             "PowerPoint modes: video (MP4 slides playing automatically), gif (also for Google Slides), animated (editable "
             "shapes with PowerPoint animations), morph (key frame slides with the Morph transition). Markers split the "
             "scenario into slides; insertInto adds the slides to an existing presentation (written to path). Format mermaid "
             "writes Mermaid text: a flowchart (.mmd), a sequence diagram of the scenario or Markdown with both (.md).",
             Schema({{"path", Prop("string", "Output file")},
                     {"format", EnumProp(_host.ExportFormats(), "Default: by extension")},
                     {"fps", Prop("number", "Frames per second (default 15)")},
                     {"scale", Prop("number", "Resolution scale (default 1)")},
                     {"quality", Prop("integer", "WebM / MP4 quality 0 (smallest) .. 4 (best), default 2")},
                     {"loop", Prop("boolean", "GIF / HTML player: loop (default true)")},
                     {"autoplay", Prop("boolean", "HTML player: start playing when opened (default true)")},
                     {"pptxMode", EnumProp({"video", "gif", "animated", "morph"}, "PowerPoint slides (default video)")},
                     {"slideSize", EnumProp({"16:9", "4:3"}, "Slide size of a new presentation (default 16:9)")},
                     {"insertInto", Prop("string", "Existing .pptx to add the slides to")},
                     {"insertAfter", Prop("integer", "Insert after this 1-based slide (default: at the end)")},
                     {"bySegments", Prop("boolean", "A slide per segment between markers (default true)")},
                     {"mermaidKind", EnumProp({"flowchart", "sequence", "markdown"}, "Mermaid: default by extension (.md: markdown)")}},
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
                 req.loop         = a.value("loop", true);
                 req.autoplay     = a.value("autoplay", true);
                 req.pptx_mode    = a.value("pptxMode", std::string("video"));
                 req.slide_size   = a.value("slideSize", std::string("16:9"));
                 req.insert_into  = a.value("insertInto", std::string{});
                 req.insert_after = a.value("insertAfter", -1);
                 req.by_markers   = a.value("bySegments", true);
                 req.mermaid_kind = a.value("mermaidKind", std::string{});
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
                 const View view = _M().view;
                 const bool ok   = _Doc().Undo();
                 if (ok)
                 {
                     _Doc().Mutable().view    = view;
                     _edit->history_traversed = true;
                     _Changed(true);
                 }
                 return ToolResult::Text(ok ? "Undone" : "Nothing to undo");
             });
        _Add("redo", "Redo", "Redo the last undone change.", Schema(Json::object()), false,
             [this](const Json &)
             {
                 const View view = _M().view;
                 const bool ok   = _Doc().Redo();
                 if (ok)
                 {
                     _Doc().Mutable().view    = view;
                     _edit->history_traversed = true;
                     _Changed(true);
                 }
                 return ToolResult::Text(ok ? "Redone" : "Nothing to redo");
             });
    }

private:
    struct StagedEdit
    {
        Document            document;
        std::optional<bool> structural;
        bool                history_traversed = false;
    };

    Document    &_Doc() { return _edit ? _edit->document : _host.Doc(); }
    const Model &_M() { return _Doc().Get(); }
    void         _Changed(bool structural) { _edit->structural = structural; }

    ToolResult _RunEdit(const ToolHandler &handler, const Json &args)
    {
        Document      &live    = _host.Doc();
        const uint64_t current = live.Revision();
        if (const auto expected = args.find("expectedRevision"); expected != args.end())
        {
            if (!expected->is_number_integer() || (!expected->is_number_unsigned() && expected->get<int64_t>() < 1) ||
                (expected->is_number_unsigned() && expected->get<uint64_t>() == 0))
            {
                throw ToolError("expectedRevision must be a positive integer");
            }
            if (expected->get<uint64_t>() != current)
            {
                auto result =
                    ToolResult::Error(std::format("document revision conflict: expected {}, current {}", expected->dump(), current));
                result.structured = {{"code", "revision_conflict"}, {"revision", current}, {"expectedRevision", *expected}};
                return result;
            }
        }

        // Edit a private copy on the GUI thread. The handlers here do not enter a nested event loop.
        // Until the result is ready, the live model and both history stacks remain untouched.
        Json edit_args = args;
        edit_args.erase("agentId");
        edit_args.erase("expectedRevision");
        StagedEdit staged{live, {}, false};
        ToolResult result;
        {
            struct ClearEdit
            {
                StagedEdit *&edit;
                ~ClearEdit() { edit = nullptr; }
            } clear{_edit};
            _edit  = &staged;
            result = handler(edit_args);
        }
        if (result.is_error)
        {
            return result;
        }
        const auto committed_revision = staged.document.Revision();
        result.structured["revision"] = committed_revision;
        if (staged.structural.has_value())
        {
            live = std::move(staged.document);
            if (staged.history_traversed)
            {
                _host.HistoryTraversed();
            }
            _host.Changed(*staged.structural);
        }
        return result;
    }

    void _Add(std::string name, std::string title, std::string description, Json schema, bool read_only, ToolHandler handler)
    {
        const bool unregister   = name == "unregister_agent";
        const bool registration = name == "register_agent";
        if (!registration)
        {
            schema["properties"]["agentId"] = Prop("string", "Registered agent ID. Include in every call made on behalf of an agent.");
        }
        const bool creates_document = name == "new_document" || name == "open_document" || name.starts_with("import_");
        const bool edits_document   = !read_only && !registration && !unregister && !creates_document && name != "select_document" &&
                                    name != "save_document" && name != "export_animation";
        const bool reports_revision = creates_document || name == "select_document" || name == "get_document" || name == "get_summary";
        if (edits_document)
        {
            schema["properties"]["expectedRevision"]            = Prop("integer", "Apply only if the current document has this revision.");
            schema["properties"]["expectedRevision"]["minimum"] = 1;
        }

        const bool  creates_node    = name == "add_node";
        const bool  adds_step       = name == "add_step";
        const auto  element         = TargetForTool(name);
        const bool  removes_element = element != AgentTarget::None && name.starts_with("remove_");
        ToolHandler keep_alive      = [this, self = shared_from_this(), h = std::move(handler), unregister, registration, element,
                                  removes_element, edits_document, reports_revision, creates_node, adds_step](const Json &args)
        {
            if (!_host.ToolsAvailable())
            {
                throw ToolError("the editor is unavailable; retry after the current operation");
            }
            if (_calling.test_and_set(std::memory_order_acquire))
            {
                throw ToolError("another MCP tool is running");
            }
            struct ClearCall
            {
                std::atomic_flag &flag;
                ~ClearCall() { flag.clear(std::memory_order_release); }
            } clear{_calling};

            if ((registration || unregister || args.contains("agentId")) && !_host.AgentsAvailable())
            {
                throw ToolError("the MCP agent sessions have been stopped");
            }
            std::string               id;
            const auto                called_at = DocumentHost::Clock::now();
            DocumentHost::DocumentKey target    = nullptr;
            if (unregister || args.contains("agentId"))
            {
                id     = RequireString(args, "agentId");
                target = _host.TouchAgent(id, called_at);
                if (registration)
                {
                    throw ToolError("register_agent does not accept an agentId");
                }
            }

            struct RestoreTarget
            {
                DocumentHost             &host;
                DocumentHost::DocumentKey previous;
                ~RestoreTarget() { host.ExchangeTarget(previous); }
            } restore{_host, _host.ExchangeTarget(target ? target : _host.ActiveDocumentKey())};

            auto run = [&]
            {
                if (edits_document)
                {
                    return _RunEdit(h, args);
                }
                if (args.contains("expectedRevision"))
                {
                    throw ToolError("expectedRevision is supported only for edits of an existing document");
                }
                auto result = h(args);
                if (reports_revision && !result.is_error)
                {
                    result.structured["revision"] = _Doc().Revision();
                }
                return result;
            };
            if (id.empty())
            {
                return run();
            }

            const bool          observe    = _host.TracksAgentActivity();
            const auto          element_id = args.value("id", Json{});
            std::optional<Vec2> position;
            if (observe && removes_element && _host.TargetDocument() && element_id.is_string())
            {
                position = AgentPosition(element, _M(), _host.Reg(), element_id);
            }

            ToolResult result;
            try
            {
                result = run();
            }
            catch (...)
            {
                if (observe && target && !unregister && _host.BindAgent(id, target))
                {
                    _host.AgentCalled({id, target, target, {}, called_at});
                }
                throw;
            }

            const auto document = result.is_error ? target : _host.TargetDocument();
            if (unregister)
            {
                if (observe && !result.is_error)
                {
                    _host.AgentCalled({id, target, nullptr, {}, called_at});
                }
            }
            else if (_host.BindAgent(id, document) && observe)
            {
                DocumentHost::AgentActivity activity{id, target, document, {}, called_at};
                if (!result.is_error && document && element != AgentTarget::None && !removes_element)
                {
                    Json edited = element_id;
                    if (result.structured.is_object())
                    {
                        edited = result.structured.value("id", edited);
                        if (const auto steps = result.structured.find("steps"); steps != result.structured.end() && !steps->empty())
                        {
                            edited = steps->front();
                        }
                    }
                    position = AgentPosition(element, _M(), _host.Reg(), edited);
                    if (creates_node && edited.is_string())
                    {
                        if (const auto *node = _M().FindNode(edited.get_ref<const std::string &>()))
                        {
                            activity.route        = DocumentHost::AgentRoute::CreateNode;
                            activity.element_type = node->type;
                            activity.created_node = node->id;
                        }
                    }
                    if (adds_step && edited.is_string())
                    {
                        if (const auto *step = _M().FindStep(edited.get_ref<const std::string &>()); step && step->type == StepType::Link)
                        {
                            activity.route = DocumentHost::AgentRoute::AddLinkStep;
                        }
                    }
                }
                activity.position = result.is_error ? std::nullopt : position;
                _host.AgentCalled(activity);
            }
            return result;
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
                 Node        &n = _Doc().AddNode(c, type);
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
                 _Changed(true);
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
                 _Doc().Checkpoint();
                 *_Doc().Mutable().FindNode(id) = std::move(updated);
                 _Changed(true);
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
                 _Doc().RemoveNode(id);
                 _Changed(true);
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
                 Edge             *e    = _Doc().AddEdge(from, to);
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
                 _Changed(true);
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
                 _Doc().Checkpoint();
                 *_Doc().Mutable().FindEdge(id) = std::move(updated);
                 _Changed(true);
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
                 _Doc().RemoveEdge(id);
                 _Changed(true);
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
                 auto base = _Doc().MakeDefaultStep(*type, a.value("start", 0.0));
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
                 _ResolveMessageEdge(*step);
                 const std::string id = _Doc().AddStep(std::move(*step)).id;
                 _Changed(true);
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
                 _ResolveMessageEdge(*step);
                 _Doc().Checkpoint();
                 *_Doc().Mutable().FindStep(id) = std::move(*step);
                 _Doc().SortSteps();
                 _Doc().UpdateDuration();
                 _Changed(true);
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
                 _Doc().RemoveStep(id);
                 _Changed(true);
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
                 const auto ids = ApplyTemplate(_Doc(), *tpl, roles, start);
                 _Changed(true);
                 return ToolResult::FromJson(Json{{"steps", ids}});
             });
    }

    // -----------------------------------------------------------------------
    // Markers
    // -----------------------------------------------------------------------
    void _RegisterMarkerTools()
    {
        _Add("add_marker", "Add marker",
             "Add a marker (chapter) on the timeline. Markers split the scenario into segments: presenter mode stops at each "
             "marker, presentation exports produce one slide per segment. Returns its id.",
             Schema({{"time", Prop("number", "Time, ms (clamped to the scene duration)")}, {"label", Prop("string", "Chapter title")}},
                    {"time"}),
             false,
             [this](const Json &a)
             {
                 if (!a.contains("time") || !a["time"].is_number())
                 {
                     throw ToolError("argument 'time' (number) is required");
                 }
                 const double  time = a["time"].get<double>();
                 const Marker *m    = _Doc().AddMarker(time, a.value("label", std::string{}));
                 if (m == nullptr)
                 {
                     throw ToolError(std::format("there already is a marker at {:.0f} ms", time));
                 }
                 const std::string id = m->id;
                 _Changed(false);
                 return ToolResult::FromJson(Json{{"id", id}, {"time", m->time}});
             });

        _Add("update_marker", "Update marker", "Move a marker and / or change its label.",
             Schema({{"id", Prop("string", "Marker id")}, {"time", Prop("number", "New time, ms")}, {"label", Prop("string", "New label")}},
                    {"id"}),
             false,
             [this](const Json &a)
             {
                 const std::string id = RequireString(a, "id");
                 const Marker     *m  = _M().FindMarker(id);
                 if (m == nullptr)
                 {
                     throw ToolError("marker '" + id + "' not found");
                 }
                 const bool has_time  = a.contains("time") && a["time"].is_number();
                 const bool has_label = a.contains("label") && a["label"].is_string();
                 if (!has_time && !has_label)
                 {
                     throw ToolError("nothing to change: pass 'time' and / or 'label'");
                 }
                 const double time = has_time ? a["time"].get<double>() : m->time;
                 if (const Marker *other = MarkerAt(_M().scenario, std::clamp(time, 0.0, _M().scenario.duration));
                     other != nullptr && other->id != id)
                 {
                     throw ToolError("marker '" + other->id + "' is already at that time");
                 }
                 // one undo step for both changes
                 _Doc().Checkpoint();
                 Model  &model = _Doc().Mutable();
                 Marker &mk    = *model.FindMarker(id);
                 mk.time       = std::clamp(time, 0.0, model.scenario.duration);
                 if (has_label)
                 {
                     mk.label = a["label"].get<std::string>();
                 }
                 NormalizeMarkers(model.scenario);
                 _Changed(false);
                 return ToolResult::Text("Marker " + id + " updated");
             });

        _Add("remove_marker", "Remove marker", "Remove a timeline marker.", Schema({{"id", Prop("string", "Marker id")}}, {"id"}), false,
             [this](const Json &a)
             {
                 const std::string id = RequireString(a, "id");
                 if (!_Doc().RemoveMarker(id))
                 {
                     throw ToolError("marker '" + id + "' not found");
                 }
                 _Changed(false);
                 return ToolResult::Text("Marker " + id + " removed");
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

    // A message flies along edge_id (the scene prefers it to from/to), so it has to connect the sender and
    // the receiver: the defaults of a new step and a changed from/to leave a stale edge there. An explicit
    // edge between the two nodes is kept (there can be several); otherwise any edge between them, or none.
    void _ResolveMessageEdge(Step &s)
    {
        if (s.type != StepType::Message)
        {
            return;
        }
        const Model &m = _M();
        if (const Edge *e = m.FindEdge(s.edge_id);
            e != nullptr && ((e->from == s.from && e->to == s.to) || (e->from == s.to && e->to == s.from)))
        {
            return;
        }
        const Edge *e = m.EdgeBetween(s.from, s.to);
        s.edge_id     = e != nullptr ? e->id : std::string{};
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
                     _Doc().UpsertElement(*e);
                 }
                 else if (kind == "effect")
                 {
                     auto e = json::EffectFromJson(def, &warnings);
                     if (!e.has_value())
                     {
                         throw ToolError("invalid effect definition");
                     }
                     id = e->id;
                     _Doc().UpsertEffect(*e);
                 }
                 else if (kind == "animation")
                 {
                     auto t = json::AnimationFromJson(def, &warnings);
                     if (!t.has_value())
                     {
                         throw ToolError("invalid animation definition");
                     }
                     id = t->id;
                     _Doc().UpsertAnimation(*t);
                 }
                 else if (kind == "design-system")
                 {
                     auto d = json::DesignSystemFromJson(def, &warnings);
                     if (!d.has_value())
                     {
                         throw ToolError("invalid design system definition");
                     }
                     id = d->id;
                     _Doc().UpsertDesignSystem(*d);
                 }
                 else
                 {
                     throw ToolError("kind must be element, effect, animation or design-system");
                 }
                 _Changed(true);
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
                 if (!_Doc().RemoveLibraryItem(id))
                 {
                     throw ToolError("'" + id + "' is not in the document library");
                 }
                 _Changed(true);
                 return ToolResult::Text("Removed " + id);
             });
    }

    McpServer       &_server;
    DocumentHost    &_host;
    StagedEdit      *_edit = nullptr;
    std::atomic_flag _calling;
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
    if (!m.scenario.markers.empty())
    {
        out += std::format("\nMarkers ({}):\n", m.scenario.markers.size());
        for (const auto &mk : m.scenario.markers)
        {
            out += std::format("- {} at {:.2f} s{}\n", mk.id, mk.time / 1000, mk.label.empty() ? "" : " \"" + mk.label + "\"");
        }
        const auto segments = ScenarioSegments(m);
        out += std::format("Segments ({}):", segments.size());
        for (const auto &seg : segments)
        {
            out += std::format(" [{:.2f}-{:.2f} s)", seg.start / 1000, seg.end / 1000);
        }
        out += "\n";
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
           "add_step or apply_animation -> optional add_marker (chapters for presenting) -> render_frame at several times to verify -> "
           "save_document / export_animation. "
           "Times are in milliseconds. Use node ids returned by add_node. "
           "For an agent session, call register_agent and pass its agentId in every subsequent call. "
           "Use select_document with that agentId to choose a tab and unregister_agent to end the session. "
           "Pass expectedRevision from the latest relevant structuredContent.revision when editing an existing document.";
}

} // namespace ad::mcp
