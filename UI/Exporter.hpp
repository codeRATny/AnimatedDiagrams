#ifndef _UI_EXPORTER_HPP_
#define _UI_EXPORTER_HPP_

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <optional>
#include <stop_token>

#include "Export/ExportPlan.hpp"
#include "Export/MermaidExporter.hpp"
#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file Exporter.hpp
/// @brief Animation export: GIF / WebM / MP4 (libav, in process), PNG sequence (Qt), PowerPoint
///        presentations, HTML player (the document in a self-contained page, when the build embeds the player),
///        Mermaid text.
///        UI independent -- used by the export dialog, the CLI (--export) and the MCP server.

namespace ad::ui
{

enum class ExportFormat
{
    Gif,
    Png,
    WebM,
    Mp4,
    Pptx,   // PowerPoint presentation (see PresentationExport.hpp)
    Html,   // interactive player page (player/, embedded with -DAD_PLAYER_HTML)
    Mermaid // Mermaid text (.mmd flowchart / sequence, .md both), see Export/MermaidExporter.hpp
};

/// How a diagram becomes PowerPoint slides.
enum class PptxMode
{
    Video,    // MP4 per slide, plays automatically (PowerPoint, Keynote, Impress)
    Gif,      // animated GIF per slide (also Google Slides)
    Animated, // editable shapes + PowerPoint animations
    Morph     // key frame slides with the Morph transition (PowerPoint 2019 / 365)
};

struct PresentationOptions
{
    PptxMode mode       = PptxMode::Video;
    bool     wide       = true;  // 16:9, otherwise 4:3 (new presentations)
    bool     by_markers = true;  // a slide per scenario segment between markers
    QString  insert_into;        // existing presentation to add the slides to
    int      insert_after  = -1; // 1-based slide number (-1 -- at the end)
    double   morph_step_ms = 400;
};

enum class Framing
{
    Content,
    View
};

struct ExportOptions
{
    ExportFormat format  = ExportFormat::Gif;
    double       fps     = 15;
    double       scale   = 1;
    Framing      framing = Framing::Content;
    Rect         view_rect; // Framing::View -- the visible canvas area (world coordinates)
    QColor       background = QColor(0x0a, 0x11, 0x1f);
    bool         loop       = true; // GIF, HTML player
    bool         autoplay   = true; // HTML player
    QString      output_path;
    int          quality  = 2;  // WebM / MP4: 0 (smallest) .. 4 (best)
    double       start_ms = 0;  // exported time range
    double       end_ms   = -1; // -1 -- the end of the scenario

    PresentationOptions        presentation; // ExportFormat::Pptx
    std::optional<MermaidKind> mermaid;      // ExportFormat::Mermaid; empty -- by extension (.md: markdown, else flowchart)
};

struct ExportResult
{
    QString path;
    int     frames = 0;
    qint64  bytes  = 0;
    QString encoder; // libav encoder used (GIF / WebM / MP4)
};

using ProgressFn = std::function<void(int done, int total)>;

QString                     FormatId(ExportFormat f);
std::optional<ExportFormat> FormatFromId(const QString &id);
QString                     PptxModeId(PptxMode m); // video | gif | animated | morph
std::optional<PptxMode>     PptxModeFromId(const QString &id);
bool                        IsVideo(ExportFormat f);
/// The build embeds the HTML player (otherwise the HTML format is not offered).
bool HtmlPlayerAvailable();
/// Ids of the formats this build can export ("gif", "png", "webm", "mp4", "pptx", "html", "mermaid").
QStringList AvailableFormatIds();
/// Encoder that will be tried first for a format; empty -- the format is unavailable
/// (GIF / WebM / MP4 need libav with a suitable encoder).
QString EncoderFor(ExportFormat f);

ExportGeometry PlanExport(const Model &m, const ExportOptions &o, const Registry &reg);
QImage         RenderExportFrame(const Model &m, double t, const ExportGeometry &g, const QColor &bg, const Registry &reg);
/// PNG bytes of one frame with content framing.
QByteArray RenderPng(const Model &m, double t, double scale, const Registry &reg);

/// Full export; checks the stop token between frames. Errors are human readable.
std::expected<ExportResult, QString> RunExport(const Model &m, const ExportOptions &o, const Registry &reg, std::stop_token stop,
                                               const ProgressFn &progress = {});

} // namespace ad::ui

#endif // _UI_EXPORTER_HPP_
