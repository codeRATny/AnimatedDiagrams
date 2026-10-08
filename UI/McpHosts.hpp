#ifndef _UI_MCP_HOSTS_HPP_
#define _UI_MCP_HOSTS_HPP_

#include <string>

#include "Mcp/DocumentTools.hpp"

/// @file McpHosts.hpp
/// @brief DocumentHost implementations: the running GUI (through the Controller) and the
///        headless `--mcp` stdio mode. Both render frames and export with Qt.

namespace ad::ui
{

class Controller;

/// MCP tools operate on the document open in the editor; every change is shown live.
class AppDocumentHost final : public mcp::DocumentHost
{
public:
    explicit AppDocumentHost(Controller &ctl) : _ctl(ctl) {}

    Document                                        &Doc() override;
    const Registry                                  &Reg() const override;
    void                                             Changed(bool structural) override;
    void                                             Replaced() override;
    [[nodiscard]] std::string                        CurrentPath() const override;
    void                                             SetCurrentPath(std::string path) override;
    std::expected<std::vector<uint8_t>, std::string> RenderPng(double time_ms, double scale) override;
    std::expected<std::string, std::string>          Export(const mcp::ExportRequest &request) override;

private:
    Controller &_ctl;
};

/// Standalone host for `animated-diagrams --mcp` (no window).
class HeadlessDocumentHost final : public mcp::BasicDocumentHost
{
public:
    explicit HeadlessDocumentHost(const Registry &reg) : mcp::BasicDocumentHost(reg) {}

    std::expected<std::vector<uint8_t>, std::string> RenderPng(double time_ms, double scale) override;
    std::expected<std::string, std::string>          Export(const mcp::ExportRequest &request) override;
};

/// Shared export implementation for both hosts.
std::expected<std::string, std::string> ExportForMcp(const Model &m, const Registry &reg, const mcp::ExportRequest &request);

} // namespace ad::ui

#endif // _UI_MCP_HOSTS_HPP_
