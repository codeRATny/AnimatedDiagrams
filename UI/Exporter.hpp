#ifndef _UI_EXPORTER_HPP_
#define _UI_EXPORTER_HPP_

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QString>

#include <expected>
#include <functional>
#include <optional>
#include <stop_token>

#include "Export/ExportPlan.hpp"
#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file Exporter.hpp
/// @brief Animation export: GIF / WebM / MP4 (libav, in process), PNG sequence (Qt).
///        UI independent -- used by the export dialog, the CLI (--export) and the MCP server.

namespace ad::ui
{

enum class ExportFormat
{
    Gif,
    Png,
    WebM,
    Mp4
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
    bool         loop       = true; // GIF only
    QString      output_path;
    int          quality = 2; // WebM / MP4: 0 (smallest) .. 4 (best)
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
bool                        IsVideo(ExportFormat f);
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
