#ifndef _MCP_DOCUMENT_TOOLS_HPP_
#define _MCP_DOCUMENT_TOOLS_HPP_

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "Mcp/McpServer.hpp"
#include "Model/Document.hpp"
#include "Model/Registry.hpp"

/// @file DocumentTools.hpp
/// @brief MCP tools operating on the current document (nodes, edges, steps, library,
///        templates, draw.io import, rendering and export).

namespace ad::mcp
{

struct ExportRequest
{
    std::string path;
    std::string format; // gif | png | webm | mp4 | html (empty -- by extension)
    double      fps      = 15;
    double      scale    = 1;
    int         quality  = 2;    // WebM / MP4: 0 (smallest) .. 4 (best)
    bool        loop     = true; // GIF, HTML player
    bool        autoplay = true; // HTML player
};

/// One open document (a GUI tab).
struct DocumentInfo
{
    std::string name;
    std::string path;
    bool        active   = false;
    bool        modified = false;
};

/// What the tools operate on. The GUI implements it on top of its controller,
/// the headless `--mcp` mode uses BasicDocumentHost with an offscreen renderer.
class DocumentHost
{
public:
    virtual ~DocumentHost() = default;

    virtual Document       &Doc()       = 0;
    virtual const Registry &Reg() const = 0;
    /// Called after every change made by a tool.
    virtual void Changed(bool structural) = 0;
    /// Called after the whole document was replaced (new / open / import).
    virtual void Replaced() = 0;

    [[nodiscard]] virtual std::string CurrentPath() const              = 0;
    virtual void                      SetCurrentPath(std::string path) = 0;

    /// PNG bytes of the frame at `time_ms` (content framing).
    virtual std::expected<std::vector<uint8_t>, std::string> RenderPng(double time_ms, double scale);
    /// Export the animation; returns a human readable result line.
    virtual std::expected<std::string, std::string> Export(const ExportRequest &request);
    /// Formats Export() supports (the export_animation schema); "html" only when the build has the player.
    [[nodiscard]] virtual std::vector<std::string> ExportFormats() const;

    /// Open documents; a single-document host returns just the current one.
    virtual std::vector<DocumentInfo> Documents();
    /// Make document `index` the target of the tools; false when out of range.
    virtual bool SelectDocument(size_t index);
    /// Called right before new_document / open_document / import_drawio replace the
    /// document: the GUI opens a new tab here instead of overwriting the current one.
    virtual void BeginNewDocument() {}
};

/// Host owning its document; rendering / export are unsupported unless overridden.
class BasicDocumentHost : public DocumentHost
{
public:
    explicit BasicDocumentHost(const Registry &reg = Registry::Default()) : _reg(reg) {}

    Document                 &Doc() override { return _doc; }
    const Registry           &Reg() const override { return _reg; }
    void                      Changed(bool /*structural*/) override {}
    void                      Replaced() override {}
    [[nodiscard]] std::string CurrentPath() const override { return _path; }
    void                      SetCurrentPath(std::string path) override { _path = std::move(path); }

private:
    Document        _doc;
    const Registry &_reg;
    std::string     _path;
};

/// Human readable overview of the document (nodes, edges, steps, library).
std::string DocumentSummary(const Model &m, const Registry &reg);

/// Register all document tools on the server. `host` must outlive the server.
void RegisterDocumentTools(McpServer &server, DocumentHost &host);

/// Instructions sent to clients in the initialize response.
std::string DefaultInstructions();

} // namespace ad::mcp

#endif // _MCP_DOCUMENT_TOOLS_HPP_
