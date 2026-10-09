#ifndef _UI_MCP_HOSTS_HPP_
#define _UI_MCP_HOSTS_HPP_

#include <string>

#include "Exporter.hpp"
#include "Mcp/DocumentTools.hpp"

/// @file McpHosts.hpp
/// @brief DocumentHost implementations: the running GUI (through the Controller) and the
///        headless `--mcp` stdio mode. Both render frames and export with Qt.

namespace ad::ui
{

class AppContext;
class Controller;

/// MCP tools operate on the active tab of the editor; every change is shown live.
class AppDocumentHost final : public mcp::DocumentHost
{
public:
    explicit AppDocumentHost(AppContext &ctx) : _ctx(ctx) {}

    Document                                        &Doc() override;
    const Registry                                  &Reg() const override;
    void                                             Changed(bool structural) override;
    void                                             Replaced() override;
    [[nodiscard]] std::string                        CurrentPath() const override;
    void                                             SetCurrentPath(std::string path) override;
    std::expected<std::vector<uint8_t>, std::string> RenderPng(double time_ms, double scale) override;
    std::expected<std::string, std::string>          Export(const mcp::ExportRequest &request) override;
    std::vector<mcp::DocumentInfo>                   Documents() override;
    bool                                             SelectDocument(size_t index) override;
    void                                             BeginNewDocument() override;

private:
    [[nodiscard]] Controller &_Ctl() const;

    AppContext &_ctx;
};

/// Standalone host for `animated-diagrams --mcp` (no window).
class HeadlessDocumentHost final : public mcp::BasicDocumentHost
{
public:
    explicit HeadlessDocumentHost(const Registry &reg) : mcp::BasicDocumentHost(reg) {}

    std::expected<std::vector<uint8_t>, std::string> RenderPng(double time_ms, double scale) override;
    std::expected<std::string, std::string>          Export(const mcp::ExportRequest &request) override;
};

/// Export options for an MCP request (format by extension when not given).
std::expected<ExportOptions, std::string> ExportOptionsFor(const Model &m, const mcp::ExportRequest &request);
/// Synchronous export for the headless host.
std::expected<std::string, std::string> ExportForMcp(const Model &m, const Registry &reg, const mcp::ExportRequest &request);

} // namespace ad::ui

#endif // _UI_MCP_HOSTS_HPP_
