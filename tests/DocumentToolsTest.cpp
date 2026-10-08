#include <gtest/gtest.h>

#include <filesystem>
#include <random>

#include "Io/JsonIo.hpp"
#include "Mcp/DocumentTools.hpp"
#include "Utils/File.hpp"

using namespace ad;
using namespace ad::mcp;

namespace
{

class ToolsFixture : public ::testing::Test
{
protected:
    ToolsFixture() : server({"t", "t", "0", DefaultInstructions()}) { RegisterDocumentTools(server, host); }

    ToolResult Ok(const char *tool, const Json &args = Json::object())
    {
        ToolResult r = server.CallTool(tool, args);
        EXPECT_FALSE(r.is_error) << tool << ": " << (r.content.empty() ? "" : r.content[0].text);
        return r;
    }

    std::string NewNode(const std::string &label, const char *type = "service")
    {
        return Ok("add_node", {{"label", label}, {"type", type}}).structured["id"].get<std::string>();
    }

    const Model &M() { return host.Doc().Get(); }

    BasicDocumentHost host;
    McpServer         server;
};

} // namespace

TEST_F(ToolsFixture, RegistersAllTools)
{
    for (const char *name : {"get_summary",
                             "get_document",
                             "list_library",
                             "new_document",
                             "open_document",
                             "save_document",
                             "import_drawio",
                             "add_node",
                             "update_node",
                             "remove_node",
                             "add_edge",
                             "update_edge",
                             "remove_edge",
                             "add_step",
                             "update_step",
                             "remove_step",
                             "apply_animation",
                             "upsert_library_item",
                             "remove_library_item",
                             "set_scene",
                             "auto_layout",
                             "render_frame",
                             "export_animation",
                             "undo",
                             "redo"})
    {
        EXPECT_TRUE(std::ranges::any_of(server.Tools(),
                                        [&](const ToolSpec &t)
                                        {
                                            return t.name == name;
                                        }))
            << name;
    }
}

TEST_F(ToolsFixture, BuildDiagramWithNodesEdgesAndSteps)
{
    const std::string a = NewNode("API", "gateway");
    const std::string b = NewNode("Orders DB", "db");
    EXPECT_EQ(M().FindNode(b)->type, "db");
    EXPECT_GT(M().FindNode(b)->x, M().FindNode(a)->x); // auto placement to the right

    const std::string e = Ok("add_edge", {{"from", a}, {"to", b}, {"label", "SQL"}, {"style", {{"routing", "orthogonal"}}}})
                              .structured["id"]
                              .get<std::string>();
    EXPECT_EQ(M().FindEdge(e)->style.routing, "orthogonal");

    const std::string s =
        Ok("add_step", {{"type", "message"}, {"from", a}, {"to", b}, {"start", 500}, {"label", "SELECT"}, {"packet", "envelope"}})
            .structured["id"]
            .get<std::string>();
    const Step *step = M().FindStep(s);
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->edge_id, e);
    EXPECT_EQ(step->packet, "envelope");
    EXPECT_DOUBLE_EQ(step->start, 500);

    Ok("add_step", {{"type", "effect"}, {"node", b}, {"effect", "shake"}, {"start", 1600}});
    Ok("add_step", {{"type", "state"}, {"nodeId", b}, {"state", "down"}, {"start", 2000}, {"duration", 1000}});
    EXPECT_EQ(M().scenario.steps.size(), 3U);

    const std::string summary = Ok("get_summary").content[0].text;
    EXPECT_NE(summary.find("Orders DB"), std::string::npos);
    EXPECT_NE(summary.find("SELECT"), std::string::npos);
}

TEST_F(ToolsFixture, UpdateAndRemove)
{
    const std::string a = NewNode("A");
    const std::string b = NewNode("B");
    Ok("update_node", {{"id", a}, {"label", "Renamed"}, {"width", 200}, {"style", {{"fill", "#112233"}, {"shape", "ellipse"}}}});
    EXPECT_EQ(M().FindNode(a)->label, "Renamed");
    EXPECT_DOUBLE_EQ(M().FindNode(a)->w, 200);
    EXPECT_EQ(M().FindNode(a)->style.fill, "#112233");
    Ok("update_node", {{"id", a}, {"style", {{"fill", nullptr}}}}); // merge-patch removal
    EXPECT_FALSE(M().FindNode(a)->style.fill.has_value());
    EXPECT_EQ(M().FindNode(a)->style.shape, "ellipse");

    const std::string e = Ok("add_edge", {{"from", a}, {"to", b}}).structured["id"].get<std::string>();
    Ok("update_edge", {{"id", e}, {"label", "HTTP"}, {"style", {{"arrowEnd", "diamond"}}}});
    EXPECT_EQ(M().FindEdge(e)->style.arrow_end, "diamond");
    EXPECT_TRUE(server.CallTool("update_edge", {{"id", e}, {"to", a}}).is_error); // self loop

    const std::string s = Ok("add_step", {{"type", "timer"}, {"nodeId", a}}).structured["id"].get<std::string>();
    Ok("update_step", {{"id", s}, {"seconds", 9}, {"start", 4000}});
    EXPECT_DOUBLE_EQ(M().FindStep(s)->seconds, 9);
    Ok("remove_step", {{"id", s}});
    Ok("remove_edge", {{"id", e}});
    Ok("remove_node", {{"id", b}});
    EXPECT_EQ(M().nodes.size(), 1U);
    EXPECT_TRUE(server.CallTool("remove_node", {{"id", "ghost"}}).is_error);

    Ok("undo");
    EXPECT_EQ(M().nodes.size(), 2U);
    Ok("redo");
    EXPECT_EQ(M().nodes.size(), 1U);
}

TEST_F(ToolsFixture, ValidationErrors)
{
    EXPECT_TRUE(server.CallTool("add_node", Json::object()).is_error);
    EXPECT_TRUE(server.CallTool("add_edge", {{"from", "x"}, {"to", "y"}}).is_error);
    EXPECT_TRUE(server.CallTool("add_step", {{"type", "message"}}).is_error); // no nodes
    const std::string a = NewNode("A");
    EXPECT_TRUE(server.CallTool("add_step", {{"type", "effect"}, {"nodeId", a}, {"effect", "nope"}}).is_error);
    EXPECT_TRUE(server.CallTool("add_step", {{"type", "teleport"}}).is_error);
    EXPECT_TRUE(server.CallTool("render_frame", Json::object()).is_error); // basic host cannot render
    EXPECT_TRUE(server.CallTool("set_scene", {{"background", "red"}}).is_error);
}

TEST_F(ToolsFixture, LibraryAndTemplates)
{
    const Json lib = Ok("list_library").structured;
    EXPECT_FALSE(lib["elements"].empty());
    EXPECT_FALSE(lib["effects"].empty());
    EXPECT_FALSE(lib["animations"].empty());
    EXPECT_TRUE(lib["catalogs"].contains("shapes"));

    Ok("upsert_library_item",
       {{"kind", "element"}, {"definition", {{"id", "k8s-pod"}, {"label", "Pod"}, {"style", {{"shape", "hexagon"}}}}}});
    Ok("upsert_library_item",
       {{"kind", "effect"},
        {"definition", {{"id", "sparkle"}, {"tracks", {{{"property", "glow"}, {"keys", {{{"t", 0}, {"value", 1}}}}}}}}}});
    EXPECT_NE(M().library.Element("k8s-pod"), nullptr);
    const std::string pod = NewNode("Pod 1", "k8s-pod");
    EXPECT_EQ(M().FindNode(pod)->type, "k8s-pod");
    Ok("add_step", {{"type", "effect"}, {"nodeId", pod}, {"effect", "sparkle"}});

    const std::string a = NewNode("Client");
    const Json res      = Ok("apply_animation", {{"template", "request-response"}, {"roles", {{"client", a}, {"server", pod}}}}).structured;
    EXPECT_EQ(res["steps"].size(), 3U);
    EXPECT_TRUE(server.CallTool("apply_animation", {{"template", "request-response"}, {"roles", {{"client", a}}}}).is_error);

    Ok("remove_library_item", {{"id", "sparkle"}});
    EXPECT_TRUE(server.CallTool("remove_library_item", {{"id", "sparkle"}}).is_error);
}

TEST_F(ToolsFixture, SceneLayoutAndDocumentIo)
{
    NewNode("A");
    NewNode("B");
    Ok("set_scene", {{"name", "Agents"}, {"durationMs", 30000}, {"background", "#ffffff"}});
    EXPECT_EQ(M().meta.name, "Agents");
    EXPECT_TRUE(M().scenario.user_duration);
    Ok("auto_layout", {{"direction", "TB"}});

    const auto path = std::filesystem::temp_directory_path() / ("ad-tools-" + std::to_string(std::random_device{}()) + ".json");
    Ok("save_document", {{"path", PathToUtf8(path)}});
    EXPECT_EQ(host.CurrentPath(), PathToUtf8(path));
    Ok("new_document", {{"example", true}});
    EXPECT_EQ(M().nodes.size(), 3U);
    Ok("open_document", {{"path", PathToUtf8(path)}});
    EXPECT_EQ(M().meta.name, "Agents");
    std::filesystem::remove(path);

    const Json doc = Ok("get_document", {{"includeLibrary", false}}).structured;
    EXPECT_EQ(doc["meta"]["name"], "Agents");
    EXPECT_FALSE(doc.contains("library"));
}

TEST_F(ToolsFixture, ImportDrawio)
{
    const std::string xml =
        R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
        R"(<mxCell id="a" value="A" vertex="1" parent="1"><mxGeometry x="0" y="0" width="100" height="50" as="geometry"/></mxCell>)"
        R"(<mxCell id="b" value="B" vertex="1" parent="1"><mxGeometry x="200" y="0" width="100" height="50" as="geometry"/></mxCell>)"
        R"(<mxCell id="e" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"/></mxCell></root></mxGraphModel>)";
    const std::string text = Ok("import_drawio", {{"xml", xml}}).content[0].text;
    EXPECT_NE(text.find("Imported 2 nodes, 1 edges"), std::string::npos);
    EXPECT_EQ(M().edges.size(), 1U);
}
