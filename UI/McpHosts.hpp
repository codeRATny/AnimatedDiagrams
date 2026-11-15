#ifndef _UI_MCP_HOSTS_HPP_
#define _UI_MCP_HOSTS_HPP_

#include <string>

#include "Mcp/DocumentTools.hpp"

/// @file McpHosts.hpp
/// @brief DocumentHost for the running GUI.

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
    void                                             HistoryTraversed() override;
    void                                             Replaced() override;
    [[nodiscard]] std::string                        CurrentPath() const override;
    void                                             SetCurrentPath(std::string path) override;
    std::expected<std::vector<uint8_t>, std::string> RenderPng(double time_ms, double scale) override;
    std::expected<std::string, std::string>          Export(const mcp::ExportRequest &request) override;
    [[nodiscard]] std::vector<std::string>           ExportFormats() const override;
    std::vector<mcp::DocumentInfo>                   Documents() override;
    bool                                             SelectDocument(size_t index, bool activate) override;
    void                                             BeginNewDocument() override;
    DocumentKey                                      ActiveDocumentKey() override;
    bool                                             ToolsAvailable() const override;
    bool                                             TracksAgentActivity() const override { return true; }
    bool                                             AgentsAvailable() const override;
    void                                             AgentCalled(const AgentActivity &activity) override;

private:
    [[nodiscard]] Controller &_Ctl() const;

    AppContext &_ctx;
};

} // namespace ad::ui

#endif // _UI_MCP_HOSTS_HPP_
