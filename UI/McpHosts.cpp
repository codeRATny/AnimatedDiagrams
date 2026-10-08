#include "McpHosts.hpp"

#include <QColor>
#include <QFileInfo>

#include <format>

#include "Controller.hpp"
#include "Exporter.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

std::expected<std::vector<uint8_t>, std::string> Png(const Model &m, double t, double scale, const Registry &reg)
{
    const QByteArray bytes = RenderPng(m, t, scale, reg);
    if (bytes.isEmpty())
    {
        return std::unexpected(std::string("failed to render the frame"));
    }
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

} // namespace

std::expected<std::string, std::string> ExportForMcp(const Model &m, const Registry &reg, const mcp::ExportRequest &request)
{
    ExportOptions o;
    o.output_path        = Qs(request.path);
    const QString fmt    = request.format.empty() ? QFileInfo(o.output_path).suffix() : Qs(request.format);
    const auto    format = FormatFromId(fmt);
    if (!format.has_value())
    {
        return std::unexpected("unknown export format '" + Us(fmt) + "' (gif, png, webm, mp4)");
    }
    o.format     = *format;
    o.fps        = request.fps;
    o.scale      = request.scale;
    o.background = QColor(Qs(m.scene.background));
    const auto r = RunExport(m, o, reg, std::stop_token{});
    if (!r.has_value())
    {
        return std::unexpected(Us(r.error()));
    }
    return std::format("Exported {} frames to {} ({} bytes)", r->frames, Us(r->path), r->bytes);
}

// ---------------------------------------------------------------------------
// AppDocumentHost
// ---------------------------------------------------------------------------

Document       &AppDocumentHost::Doc() { return _ctl.Doc(); }
const Registry &AppDocumentHost::Reg() const { return _ctl.Reg(); }
void            AppDocumentHost::Changed(bool structural) { _ctl.Changed(structural); }
void            AppDocumentHost::Replaced()
{
    _ctl._modified = _ctl._path.isEmpty(); // a file opened by path is unmodified; new / imported documents are not saved yet
    _ctl._OnReplaced();
}
std::string AppDocumentHost::CurrentPath() const { return Us(_ctl.FilePath()); }
void        AppDocumentHost::SetCurrentPath(std::string path) { _ctl.SetFilePath(Qs(path)); }

std::expected<std::vector<uint8_t>, std::string> AppDocumentHost::RenderPng(double time_ms, double scale)
{
    return Png(_ctl.GetModel(), time_ms, scale, _ctl.Reg());
}

std::expected<std::string, std::string> AppDocumentHost::Export(const mcp::ExportRequest &request)
{
    return ExportForMcp(_ctl.GetModel(), _ctl.Reg(), request);
}

// ---------------------------------------------------------------------------
// HeadlessDocumentHost
// ---------------------------------------------------------------------------

std::expected<std::vector<uint8_t>, std::string> HeadlessDocumentHost::RenderPng(double time_ms, double scale)
{
    return Png(Doc().Get(), time_ms, scale, Reg());
}

std::expected<std::string, std::string> HeadlessDocumentHost::Export(const mcp::ExportRequest &request)
{
    return ExportForMcp(Doc().Get(), Reg(), request);
}

} // namespace ad::ui
