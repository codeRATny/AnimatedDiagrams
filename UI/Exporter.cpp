#include "Exporter.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>

#include <algorithm>
#include <span>

#include "Engine/Scene.hpp"
#include "Export/VideoEncoder.hpp"
#include "QtRender.hpp"
#include "Utils/File.hpp"

namespace ad::ui
{

namespace
{

std::span<const uint8_t> Pixels(const QImage &img) { return {img.constBits(), static_cast<size_t>(img.sizeInBytes())}; }

QString Cancelled() { return QObject::tr("Export cancelled"); }

using Result = std::expected<ExportResult, QString>;

Result ExportPng(const Model &m, const ExportOptions &o, const Registry &reg, const ExportGeometry &g, const std::vector<double> &times,
                 const std::stop_token &stop, const ProgressFn &progress)
{
    // name.png -> name_0001.png, name_0002.png, ...
    const QFileInfo fi(o.output_path);
    const QString   base  = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
    qint64          total = 0;
    for (size_t i = 0; i < times.size(); ++i)
    {
        if (stop.stop_requested())
        {
            return std::unexpected(Cancelled());
        }
        const QString path = QStringLiteral("%1_%2.png").arg(base).arg(i + 1, 4, 10, QLatin1Char('0'));
        const QImage  img  = RenderExportFrame(m, times[i], g, o.background, reg);
        if (!img.save(path, "PNG"))
        {
            return std::unexpected(QObject::tr("Could not write %1").arg(path));
        }
        total += QFileInfo(path).size();
        if (progress)
        {
            progress(static_cast<int>(i + 1), static_cast<int>(times.size()));
        }
    }
    return ExportResult{base + QStringLiteral("_*.png"), static_cast<int>(times.size()), total, {}};
}

video::Container ContainerOf(ExportFormat f)
{
    switch (f)
    {
    case ExportFormat::Gif:
        return video::Container::Gif;
    case ExportFormat::WebM:
        return video::Container::WebM;
    case ExportFormat::Mp4:
    case ExportFormat::Png:
        break;
    }
    return video::Container::Mp4;
}

/// GIF / WebM / MP4 through libav. GIF renders the frames twice (palette, then encoding)
/// instead of keeping them in memory.
Result ExportEncoded(const Model &m, const ExportOptions &o, const Registry &reg, const ExportGeometry &g, const std::vector<double> &times,
                     const std::stop_token &stop, const ProgressFn &progress)
{
    const QString       prefix = o.format == ExportFormat::Gif ? QObject::tr("GIF: %1") : QObject::tr("Video: %1");
    video::VideoEncoder enc;
    video::VideoOptions vo;
    vo.width        = g.px_w;
    vo.height       = g.px_h;
    vo.fps          = o.fps;
    vo.container    = ContainerOf(o.format);
    vo.quality      = o.quality;
    vo.loop         = o.loop;
    const auto path = PathFromUtf8(Us(o.output_path));
    if (auto r = enc.Open(path, vo); !r.has_value())
    {
        return std::unexpected(prefix.arg(Qs(r.error())));
    }
    const int passes = video::PassCount(vo.container);
    const int total  = static_cast<int>(times.size()) * passes;
    for (int pass = 0; pass < passes; ++pass)
    {
        if (pass > 0)
        {
            if (auto r = enc.NextPass(); !r.has_value())
            {
                enc.Abort();
                QFile::remove(o.output_path);
                return std::unexpected(prefix.arg(Qs(r.error())));
            }
        }
        for (size_t i = 0; i < times.size(); ++i)
        {
            if (stop.stop_requested())
            {
                enc.Abort();
                QFile::remove(o.output_path);
                return std::unexpected(Cancelled());
            }
            const QImage img = RenderExportFrame(m, times[i], g, o.background, reg);
            if (auto r = enc.AddFrame(Pixels(img)); !r.has_value())
            {
                enc.Abort();
                return std::unexpected(prefix.arg(Qs(r.error())));
            }
            if (progress)
            {
                progress(pass * static_cast<int>(times.size()) + static_cast<int>(i + 1), total);
            }
        }
    }
    if (auto r = enc.Finish(); !r.has_value())
    {
        return std::unexpected(prefix.arg(Qs(r.error())));
    }
    return ExportResult{o.output_path, static_cast<int>(times.size()), QFileInfo(o.output_path).size(), Qs(enc.EncoderName())};
}

} // namespace

QString FormatId(ExportFormat f)
{
    switch (f)
    {
    case ExportFormat::Gif:
        return QStringLiteral("gif");
    case ExportFormat::Png:
        return QStringLiteral("png");
    case ExportFormat::WebM:
        return QStringLiteral("webm");
    case ExportFormat::Mp4:
        return QStringLiteral("mp4");
    }
    return {};
}

std::optional<ExportFormat> FormatFromId(const QString &id)
{
    for (const auto f : {ExportFormat::Gif, ExportFormat::Png, ExportFormat::WebM, ExportFormat::Mp4})
    {
        if (FormatId(f).compare(id, Qt::CaseInsensitive) == 0)
        {
            return f;
        }
    }
    return std::nullopt;
}

bool IsVideo(ExportFormat f) { return f == ExportFormat::WebM || f == ExportFormat::Mp4; }

QString EncoderFor(ExportFormat f)
{
    if (f == ExportFormat::Png)
    {
        return QStringLiteral("png"); // Qt image writer, always available
    }
    const auto encoders = video::EncodersFor(ContainerOf(f));
    return encoders.empty() ? QString() : Qs(encoders.front());
}

ExportGeometry PlanExport(const Model &m, const ExportOptions &o, const Registry &reg)
{
    const Rect world = o.framing == Framing::View ? o.view_rect : ContentBounds(m, QtTextMeasurer{}, reg).Adjusted(kExportPadding);
    return MakeExportGeometry(world, o.scale);
}

QImage RenderExportFrame(const Model &m, double t, const ExportGeometry &g, const QColor &bg, const Registry &reg)
{
    QImage img(g.px_w, g.px_h, QImage::Format_RGBA8888);
    img.fill(bg);
    QPainter     p(&img);
    const double k = std::min(g.px_w / g.world.w, g.px_h / g.world.h);
    p.translate((g.px_w - g.world.w * k) / 2, (g.px_h - g.world.h * k) / 2);
    p.scale(k, k);
    p.translate(-g.world.x, -g.world.y);
    SceneOptions opt;
    opt.editor_chrome = false;
    RenderFrame(p, BuildFrame(m, t, opt, QtTextMeasurer{}, reg));
    p.end();
    return img;
}

QByteArray RenderPng(const Model &m, double t, double scale, const Registry &reg)
{
    ExportOptions o;
    o.scale          = scale;
    o.background     = QColor(QString::fromStdString(m.scene.background));
    const QImage img = RenderExportFrame(m, t, PlanExport(m, o, reg), o.background, reg);
    QByteArray   bytes;
    QBuffer      buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

std::expected<ExportResult, QString> RunExport(const Model &m, const ExportOptions &o, const Registry &reg, std::stop_token stop,
                                               const ProgressFn &progress)
{
    if (o.output_path.isEmpty())
    {
        return std::unexpected(QObject::tr("No output file specified"));
    }
    if (o.fps <= 0)
    {
        return std::unexpected(QObject::tr("Invalid frame rate"));
    }
    const QFileInfo out(o.output_path);
    if (!QDir().mkpath(out.absolutePath()))
    {
        return std::unexpected(QObject::tr("Cannot access folder %1").arg(out.absolutePath()));
    }
    const ExportGeometry g     = PlanExport(m, o, reg);
    const auto           times = ExportFrameTimes(m.scenario.duration, o.fps);
    switch (o.format)
    {
    case ExportFormat::Png:
        return ExportPng(m, o, reg, g, times, stop, progress);
    case ExportFormat::Gif:
    case ExportFormat::WebM:
    case ExportFormat::Mp4:
        return ExportEncoded(m, o, reg, g, times, stop, progress);
    }
    return std::unexpected(QObject::tr("Unknown format"));
}

} // namespace ad::ui
