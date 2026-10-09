#include "McpServer.hpp"

#include <algorithm>

#include "Utils/CrashHandler.hpp"

namespace ad::mcp
{

// ---------------------------------------------------------------------------
// ToolResult helpers
// ---------------------------------------------------------------------------

ToolResult ToolResult::Text(std::string text)
{
    ToolResult r;
    r.content.push_back({"text", std::move(text), {}, {}});
    return r;
}

ToolResult ToolResult::FromJson(const Json &value)
{
    ToolResult r = Text(value.dump(2, ' ', false, mcp::Json::error_handler_t::replace));
    if (value.is_object())
    {
        r.structured = value;
    }
    return r;
}

ToolResult ToolResult::Error(std::string message)
{
    ToolResult r = Text(std::move(message));
    r.is_error   = true;
    return r;
}

ToolResult ToolResult::Image(std::string base64, std::string mime_type)
{
    ToolResult r;
    r.content.push_back({"image", {}, std::move(base64), std::move(mime_type)});
    return r;
}

namespace
{

Json ErrorResponse(const Json &id, int code, const std::string &message)
{
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

Json ResultResponse(const Json &id, Json result) { return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}; }

Json ContentToJson(const ToolContent &c)
{
    if (c.type == "image")
    {
        return Json{{"type", "image"}, {"data", c.data}, {"mimeType", c.mime_type}};
    }
    return Json{{"type", "text"}, {"text", c.text}};
}

} // namespace

// ---------------------------------------------------------------------------
// McpServer
// ---------------------------------------------------------------------------

McpServer::McpServer(ServerInfo info) : _info(std::move(info)) {}

void McpServer::AddTool(ToolSpec spec)
{
    std::erase_if(_tools,
                  [&](const ToolSpec &t)
                  {
                      return t.name == spec.name;
                  });
    _tools.push_back(std::move(spec));
}

std::string McpServer::Handle(std::string_view message)
{
    const Json msg = Json::parse(message, nullptr, /*allow_exceptions=*/false);
    Json       response;
    if (msg.is_discarded())
    {
        response = ErrorResponse(nullptr, kParseError, "Parse error");
    }
    else
    {
        response = HandleJson(msg);
    }
    if (response.is_null())
    {
        return {};
    }
    return response.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Json McpServer::HandleJson(const Json &message)
{
    if (message.is_array())
    {
        if (message.empty())
        {
            return ErrorResponse(nullptr, kInvalidRequest, "Empty batch");
        }
        Json out = Json::array();
        for (const auto &m : message)
        {
            Json r = _HandleOne(m);
            if (!r.is_null())
            {
                out.push_back(std::move(r));
            }
        }
        return out.empty() ? Json() : out;
    }
    return _HandleOne(message);
}

Json McpServer::_HandleOne(const Json &msg)
{
    if (!msg.is_object() || msg.value("jsonrpc", "") != "2.0" || !msg.contains("method") || !msg["method"].is_string())
    {
        // responses from the client (e.g. to pings) carry no method -- nothing to answer
        if (msg.is_object() && (msg.contains("result") || msg.contains("error")))
        {
            return {};
        }
        return ErrorResponse(msg.is_object() && msg.contains("id") ? msg["id"] : Json(), kInvalidRequest, "Invalid Request");
    }
    const std::string method = msg["method"].get<std::string>();
    const bool        notify = !msg.contains("id");
    const Json        id     = notify ? Json() : msg["id"];
    const Json        params = msg.contains("params") && msg["params"].is_object() ? msg["params"] : Json::object();
    if (notify)
    {
        return {}; // notifications/initialized, notifications/cancelled, ...
    }
    try
    {
        if (method == "initialize")
        {
            crash::Breadcrumb("MCP > initialize " + params.value("clientInfo", Json::object()).dump().substr(0, 120));
            return ResultResponse(id, _Initialize(params));
        }
        if (method == "ping")
        {
            return ResultResponse(id, Json::object());
        }
        if (method == "tools/list")
        {
            return ResultResponse(id, _ToolsList());
        }
        if (method == "tools/call")
        {
            if (!params.contains("name") || !params["name"].is_string())
            {
                return ErrorResponse(id, kInvalidParams, "tools/call requires a tool name");
            }
            const std::string name  = params["name"].get<std::string>();
            const bool        known = std::ranges::any_of(_tools,
                                                          [&](const ToolSpec &t)
                                                          {
                                                       return t.name == name;
                                                   });
            if (!known)
            {
                return ErrorResponse(id, kInvalidParams, "Unknown tool: " + name);
            }
            // crash reports show what the agent was doing
            crash::Breadcrumb("MCP > " + name + " " + params.value("arguments", Json::object()).dump().substr(0, 160));
            Json result = _ToolsCall(params);
            crash::Breadcrumb("MCP < " + name + (result.value("isError", false) ? " error" : " ok"));
            return ResultResponse(id, std::move(result));
        }
        if (method == "resources/list")
        {
            return ResultResponse(id, Json{{"resources", Json::array()}});
        }
        if (method == "prompts/list")
        {
            return ResultResponse(id, Json{{"prompts", Json::array()}});
        }
        return ErrorResponse(id, kMethodNotFound, "Method not found: " + method);
    }
    catch (const std::exception &e)
    {
        crash::Breadcrumb("MCP ! " + method + ": " + e.what());
        return ErrorResponse(id, kInternalError, e.what());
    }
}

Json McpServer::_Initialize(const Json &params)
{
    std::string version(kProtocolVersions.front());
    if (params.contains("protocolVersion") && params["protocolVersion"].is_string())
    {
        const std::string requested = params["protocolVersion"].get<std::string>();
        if (std::ranges::find(kProtocolVersions, requested) != kProtocolVersions.end())
        {
            version = requested;
        }
    }
    Json result{{"protocolVersion", version},
                {"capabilities", {{"tools", {{"listChanged", false}}}}},
                {"serverInfo", {{"name", _info.name}, {"title", _info.title}, {"version", _info.version}}}};
    if (!_info.instructions.empty())
    {
        result["instructions"] = _info.instructions;
    }
    return result;
}

Json McpServer::_ToolsList() const
{
    Json tools = Json::array();
    for (const auto &t : _tools)
    {
        Json schema = t.input_schema.is_object() ? t.input_schema : Json::object();
        if (!schema.contains("type"))
        {
            schema["type"] = "object";
        }
        Json tool{{"name", t.name}, {"description", t.description}, {"inputSchema", schema}};
        if (!t.title.empty())
        {
            tool["title"] = t.title;
        }
        if (t.read_only)
        {
            tool["annotations"] = {{"readOnlyHint", true}};
        }
        tools.push_back(std::move(tool));
    }
    return Json{{"tools", std::move(tools)}};
}

ToolResult McpServer::CallTool(std::string_view name, const Json &arguments)
{
    const auto it = std::ranges::find(_tools, name, &ToolSpec::name);
    if (it == _tools.end())
    {
        return ToolResult::Error("Unknown tool: " + std::string(name));
    }
    try
    {
        return it->handler(arguments.is_object() ? arguments : Json::object());
    }
    catch (const std::exception &e)
    {
        return ToolResult::Error(e.what());
    }
}

Json McpServer::_ToolsCall(const Json &params)
{
    const ToolResult r       = CallTool(params["name"].get<std::string>(), params.value("arguments", Json::object()));
    Json             content = Json::array();
    for (const auto &c : r.content)
    {
        content.push_back(ContentToJson(c));
    }
    Json result{{"content", std::move(content)}, {"isError", r.is_error}};
    if (!r.structured.is_null())
    {
        result["structuredContent"] = r.structured;
    }
    return result;
}

} // namespace ad::mcp
