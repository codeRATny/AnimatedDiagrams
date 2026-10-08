#include <gtest/gtest.h>

#include "Mcp/McpServer.hpp"

using namespace ad::mcp;

namespace
{

McpServer MakeServer()
{
    McpServer server({"test-server", "Test", "1.2.3", "Use the echo tool."});
    server.AddTool({"echo", "Echo", "Returns its text argument", Json{{"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}},
                    true, [](const Json &args)
                    {
                        if (!args.contains("text"))
                        {
                            throw ToolError("text is required");
                        }
                        return ToolResult::Text(args["text"].get<std::string>());
                    }});
    return server;
}

Json Call(McpServer &s, const Json &msg) { return Json::parse(s.Handle(msg.dump())); }

} // namespace

TEST(McpServerTest, InitializeNegotiatesVersion)
{
    McpServer  s = MakeServer();
    const Json r = Call(s, {{"jsonrpc", "2.0"},
                            {"id", 1},
                            {"method", "initialize"},
                            {"params", {{"protocolVersion", "2025-03-26"}, {"capabilities", Json::object()}}}});
    EXPECT_EQ(r["id"], 1);
    EXPECT_EQ(r["result"]["protocolVersion"], "2025-03-26");
    EXPECT_EQ(r["result"]["serverInfo"]["name"], "test-server");
    EXPECT_EQ(r["result"]["serverInfo"]["version"], "1.2.3");
    EXPECT_TRUE(r["result"]["capabilities"].contains("tools"));
    EXPECT_EQ(r["result"]["instructions"], "Use the echo tool.");

    const Json future = Call(s, {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "initialize"}, {"params", {{"protocolVersion", "2099-01-01"}}}});
    EXPECT_EQ(future["result"]["protocolVersion"], std::string(McpServer::kProtocolVersions.front()));
}

TEST(McpServerTest, NotificationsGetNoResponse)
{
    McpServer s = MakeServer();
    EXPECT_TRUE(s.Handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
    EXPECT_TRUE(s.Handle(R"({"jsonrpc":"2.0","id":5,"result":{}})").empty()); // client response
}

TEST(McpServerTest, ToolsListAndCall)
{
    McpServer  s     = MakeServer();
    const Json list  = Call(s, {{"jsonrpc", "2.0"}, {"id", "a"}, {"method", "tools/list"}});
    const Json tools = list["result"]["tools"];
    ASSERT_EQ(tools.size(), 1U);
    EXPECT_EQ(tools[0]["name"], "echo");
    EXPECT_EQ(tools[0]["inputSchema"]["type"], "object");
    EXPECT_TRUE(tools[0]["annotations"]["readOnlyHint"].get<bool>());

    const Json ok =
        Call(s, {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"}, {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}});
    EXPECT_FALSE(ok["result"]["isError"].get<bool>());
    EXPECT_EQ(ok["result"]["content"][0]["text"], "hi");

    const Json bad = Call(s, {{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"}, {"params", {{"name", "echo"}}}});
    EXPECT_TRUE(bad["result"]["isError"].get<bool>()); // tool errors are results, not protocol errors
    EXPECT_EQ(bad["result"]["content"][0]["text"], "text is required");

    const Json unknown = Call(s, {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "tools/call"}, {"params", {{"name", "nope"}}}});
    EXPECT_EQ(unknown["error"]["code"], kInvalidParams);
}

TEST(McpServerTest, ProtocolErrors)
{
    McpServer s = MakeServer();
    EXPECT_EQ(Json::parse(s.Handle("{not json"))["error"]["code"], kParseError);
    EXPECT_EQ(Call(s, {{"id", 1}, {"method", "ping"}})["error"]["code"], kInvalidRequest);
    EXPECT_EQ(Call(s, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "nope/nope"}})["error"]["code"], kMethodNotFound);
    EXPECT_TRUE(Call(s, {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "ping"}})["result"].empty());
}

TEST(McpServerTest, Batch)
{
    McpServer  s = MakeServer();
    const Json r = Json::parse(s.Handle(R"([{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/x"},
                                          {"jsonrpc":"2.0","id":2,"method":"tools/list"}])"));
    ASSERT_TRUE(r.is_array());
    EXPECT_EQ(r.size(), 2U);
}

TEST(McpServerTest, ResultHelpers)
{
    const ToolResult j = ToolResult::FromJson(Json{{"id", "x"}});
    EXPECT_EQ(j.structured["id"], "x");
    const ToolResult img = ToolResult::Image("AAAA", "image/png");
    EXPECT_EQ(img.content[0].type, "image");
    EXPECT_TRUE(ToolResult::Error("e").is_error);
}
