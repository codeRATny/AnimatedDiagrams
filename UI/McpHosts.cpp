#include "McpHosts.hpp"

#include <QColor>
#include <QFileInfo>

#include <format>

#include "AppContext.hpp"
#include "Controller.hpp"
#include "ExportManager.hpp"
#include "Exporter.hpp"
#include "Mcp/McpServer.hpp"
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

std::vector<std::string> McpExportFormats()
{
    std::vector<std::string> out;
    for (const QString &id : AvailableFormatIds())
    {
        out.push_back(Us(id));
    }
    return out;
}

std::expected<ExportOptions, std::string> ExportOptionsFor(const Model &m, const mcp::ExportRequest &request)
{
    ExportOptions o;
    o.output_path        = Qs(request.path);
    const QString fmt    = request.format.empty() ? QFileInfo(o.output_path).suffix() : Qs(request.format);
    const auto    format = FormatFromId(fmt);
    if (!format.has_value())
    {
        return std::unexpected("unknown export format '" + Us(fmt) + "' (" + Us(AvailableFormatIds().join(QStringLiteral(", "))) + ")");
    }
    o.format     = *format;
    o.fps        = request.fps;
    o.scale      = request.scale;
    o.quality    = request.quality;
    o.loop       = request.loop;
    o.autoplay   = request.autoplay;
    o.background = QColor(Qs(m.scene.background));
    if (o.format == ExportFormat::Pptx)
    {
        const auto mode = PptxModeFromId(Qs(request.pptx_mode));
        if (!mode.has_value())
        {
            return std::unexpected("unknown pptxMode '" + request.pptx_mode + "' (video, gif, animated, morph)");
        }
        auto &p        = o.presentation;
        p.mode         = *mode;
        p.wide         = request.slide_size != "4:3";
        p.insert_into  = Qs(request.insert_into);
        p.insert_after = request.insert_after;
        p.by_markers   = request.by_markers;
    }
    return o;
}

namespace
{

std::string ResultLine(const ExportResult &r)
{
    if (r.frames == 0)
    {
        return std::format("Exported the HTML player to {} ({} bytes)", Us(r.path), r.bytes);
    }
    return std::format("Exported {} frames to {} ({} bytes{})", r.frames, Us(r.path), r.bytes,
                       r.encoder.isEmpty() ? std::string() : ", encoder " + Us(r.encoder));
}

} // namespace

std::expected<std::string, std::string> ExportForMcp(const Model &m, const Registry &reg, const mcp::ExportRequest &request)
{
    const auto o = ExportOptionsFor(m, request);
    if (!o.has_value())
    {
        return std::unexpected(o.error());
    }
    const auto r = RunExport(m, *o, reg, std::stop_token{});
    if (!r.has_value())
    {
        return std::unexpected(Us(r.error()));
    }
    return ResultLine(*r);
}

// ---------------------------------------------------------------------------
// AppDocumentHost
// ---------------------------------------------------------------------------

Controller &AppDocumentHost::_Ctl() const
{
    Controller *c = _ctx.Active();
    if (c == nullptr && _ctx.GetWorkspace() != nullptr)
    {
        c = _ctx.GetWorkspace()->NewDocumentTab();
    }
    if (c == nullptr)
    {
        throw mcp::ToolError("no open document");
    }
    return *c;
}

Document       &AppDocumentHost::Doc() { return _Ctl().Doc(); }
const Registry &AppDocumentHost::Reg() const { return _ctx.Reg(); }
void            AppDocumentHost::Changed(bool structural) { _Ctl().Changed(structural); }

void AppDocumentHost::Replaced()
{
    Controller &c = _Ctl();
    c._modified   = c._path.isEmpty(); // a file opened by path is unmodified; new / imported documents are not saved yet
    c._OnReplaced();
}

std::string AppDocumentHost::CurrentPath() const { return Us(_Ctl().FilePath()); }
void        AppDocumentHost::SetCurrentPath(std::string path) { _Ctl().SetFilePath(Qs(path)); }

std::expected<std::vector<uint8_t>, std::string> AppDocumentHost::RenderPng(double time_ms, double scale)
{
    return Png(_Ctl().GetModel(), time_ms, scale, _ctx.Reg());
}

std::expected<std::string, std::string> AppDocumentHost::Export(const mcp::ExportRequest &request)
{
    // runs as a background job (listed in the exports panel); the UI stays responsive while waiting
    const Model &m = _Ctl().GetModel();
    const auto   o = ExportOptionsFor(m, request);
    if (!o.has_value())
    {
        return std::unexpected(o.error());
    }
    auto     &jobs = _ctx.Exports();
    const int id   = jobs.Start(Qs(m.meta.name) + QStringLiteral(" (MCP)"), m, _ctx.Reg(), *o);
    jobs.Wait(id);
    const ExportJobInfo *info = jobs.Info(id);
    if (info == nullptr || info->state != ExportJobInfo::State::Done)
    {
        return std::unexpected(info != nullptr ? Us(info->message) : std::string("export failed"));
    }
    return ResultLine(info->result);
}

std::vector<std::string> AppDocumentHost::ExportFormats() const { return McpExportFormats(); }

std::vector<mcp::DocumentInfo> AppDocumentHost::Documents()
{
    std::vector<mcp::DocumentInfo> out;
    if (_ctx.GetWorkspace() == nullptr)
    {
        return mcp::DocumentHost::Documents();
    }
    for (Controller *c : _ctx.GetWorkspace()->Documents())
    {
        out.push_back({c->GetModel().meta.name, Us(c->FilePath()), c == _ctx.Active(), c->IsModified()});
    }
    return out;
}

bool AppDocumentHost::SelectDocument(size_t index)
{
    if (_ctx.GetWorkspace() == nullptr)
    {
        return index == 0;
    }
    const auto docs = _ctx.GetWorkspace()->Documents();
    if (index >= docs.size())
    {
        return false;
    }
    _ctx.GetWorkspace()->Activate(docs[index]);
    return true;
}

void AppDocumentHost::BeginNewDocument()
{
    if (_ctx.GetWorkspace() != nullptr)
    {
        _ctx.GetWorkspace()->NewDocumentTab();
    }
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

std::vector<std::string> HeadlessDocumentHost::ExportFormats() const { return McpExportFormats(); }

} // namespace ad::ui
