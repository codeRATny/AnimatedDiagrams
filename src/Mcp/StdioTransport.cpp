#include "StdioTransport.hpp"

#include <istream>
#include <ostream>
#include <string>

#include "Utils/Text.hpp"

namespace ad::mcp
{

size_t RunStdio(McpServer &server, std::istream &in, std::ostream &out, std::ostream &log)
{
    size_t      handled = 0;
    std::string line;
    while (std::getline(in, line))
    {
        const std::string msg = Trim(line);
        if (msg.empty())
        {
            continue;
        }
        try
        {
            const std::string response = server.Handle(msg);
            ++handled;
            if (!response.empty())
            {
                out << response << '\n';
                out.flush();
            }
        }
        catch (const std::exception &e)
        {
            log << "[McpStdio] " << e.what() << '\n';
        }
    }
    return handled;
}

} // namespace ad::mcp
