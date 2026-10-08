#ifndef _MCP_STDIO_TRANSPORT_HPP_
#define _MCP_STDIO_TRANSPORT_HPP_

#include <iosfwd>

#include "Mcp/McpServer.hpp"

/// @file StdioTransport.hpp
/// @brief MCP stdio transport: newline-delimited JSON-RPC messages on stdin / stdout.

namespace ad::mcp
{

/// Serve requests until EOF. Logs go to `log` (never to `out`). Returns the number of handled messages.
size_t RunStdio(McpServer &server, std::istream &in, std::ostream &out, std::ostream &log);

} // namespace ad::mcp

#endif // _MCP_STDIO_TRANSPORT_HPP_
