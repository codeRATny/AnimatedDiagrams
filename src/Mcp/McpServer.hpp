#ifndef _MCP_MCP_SERVER_HPP_
#define _MCP_MCP_SERVER_HPP_

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

/// @file McpServer.hpp
/// @brief Transport-independent Model Context Protocol server (JSON-RPC 2.0).
///
/// Implements initialize, ping, tools/list, tools/call and ignores notifications.
/// Transports (stdio, HTTP) feed raw messages into Handle() and send back the result.

namespace ad::mcp
{

using Json = nlohmann::json;

struct ToolContent
{
    std::string type = "text"; // "text" | "image"
    std::string text;
    std::string data; // base64 for images
    std::string mime_type;
};

struct ToolResult
{
    std::vector<ToolContent> content;
    bool                     is_error = false;
    Json                     structured; // optional structuredContent (null when absent)

    static ToolResult Text(std::string text);
    static ToolResult FromJson(const Json &value); // pretty JSON text + structuredContent for objects
    static ToolResult Error(std::string message);
    static ToolResult Image(std::string base64, std::string mime_type);
};

using ToolHandler = std::function<ToolResult(const Json &arguments)>;

struct ToolSpec
{
    std::string name;
    std::string title;
    std::string description;
    Json        input_schema = Json::object(); // JSON Schema of the arguments
    bool        read_only    = false;
    ToolHandler handler;
};

struct ServerInfo
{
    std::string name    = "animated-diagrams";
    std::string title   = "Animated Diagrams";
    std::string version = "0.0.0";
    std::string instructions;
};

/// Exception a tool handler may throw to report invalid arguments (becomes an isError result).
class ToolError : public std::runtime_error
{
public:
    explicit ToolError(const std::string &msg) : std::runtime_error(msg) {}
};

class McpServer
{
public:
    static constexpr std::array<std::string_view, 3> kProtocolVersions{"2025-06-18", "2025-03-26", "2024-11-05"};

    explicit McpServer(ServerInfo info);

    void                                       AddTool(ToolSpec spec);
    [[nodiscard]] const std::vector<ToolSpec> &Tools() const { return _tools; }
    [[nodiscard]] const ServerInfo            &Info() const { return _info; }

    /// Handle one message or a batch. Returns the serialized response,
    /// or an empty string when nothing must be sent (notifications only).
    std::string Handle(std::string_view message);

    /// Same on parsed JSON; returns null when there is no response.
    Json HandleJson(const Json &message);

    /// Call a tool directly (used by tests and the GUI).
    ToolResult CallTool(std::string_view name, const Json &arguments);

private:
    Json _HandleOne(const Json &msg);
    Json _Initialize(const Json &params);
    Json _ToolsList() const;
    Json _ToolsCall(const Json &params);

    ServerInfo            _info;
    std::vector<ToolSpec> _tools;
};

/// JSON-RPC error codes.
inline constexpr int kParseError     = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams  = -32602;
inline constexpr int kInternalError  = -32603;

} // namespace ad::mcp

#endif // _MCP_MCP_SERVER_HPP_
