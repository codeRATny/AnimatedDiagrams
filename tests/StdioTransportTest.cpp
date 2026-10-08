#include <gtest/gtest.h>

#include <sstream>

#include "Mcp/DocumentTools.hpp"
#include "Mcp/StdioTransport.hpp"

using namespace ad::mcp;

TEST(StdioTransportTest, LineDelimitedSession)
{
    BasicDocumentHost host;
    McpServer         server({"animated-diagrams", "Animated Diagrams", "1.0", DefaultInstructions()});
    RegisterDocumentTools(server, host);

    std::istringstream in(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}
{"jsonrpc":"2.0","method":"notifications/initialized"}

{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add_node","arguments":{"label":"API"}}}
{"jsonrpc":"2.0","id":3,"method":"tools/list"}
)");
    std::ostringstream out;
    std::ostringstream log;
    EXPECT_EQ(RunStdio(server, in, out, log), 4U);

    std::istringstream lines(out.str());
    std::string        line;
    std::vector<Json>  responses;
    while (std::getline(lines, line))
    {
        responses.push_back(Json::parse(line));
    }
    ASSERT_EQ(responses.size(), 3U); // the notification has no response
    EXPECT_EQ(responses[0]["result"]["serverInfo"]["name"], "animated-diagrams");
    EXPECT_FALSE(responses[1]["result"]["isError"].get<bool>());
    EXPECT_GT(responses[2]["result"]["tools"].size(), 20U);
    EXPECT_EQ(host.Doc().Get().nodes.size(), 1U);
    EXPECT_TRUE(log.str().empty());
}
