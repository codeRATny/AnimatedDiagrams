#include "Exporter.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>

#include <algorithm>
#include <span>

#include "Engine/Scene.hpp"
#include "Export/GifEncoder.hpp"
#include "Export/HtmlPlayer.hpp"
#include "Export/VideoEncoder.hpp"
#include "Io/JsonIo.hpp"
#include "QtRender.hpp"
#include "Utils/File.hpp"

namespace ad::ui
{

namespace
{

/// Prebuilt HTML player (player/player.html), present when built with -DAD_PLAYER_HTML.
constexpr auto kPlayerResource = ":/player/player.html";

std::span<const uint8_t> Pixels(const QImage &img) { return {img.constBits(), static_cast<size_t>(img.sizeInBytes())}; }

QString Cancelled() { return QObject::tr("Export cancelled"); }

using Result = std::expected<ExportResult, QString>;

Result ExportGif(const Model &m, const ExportOptions &o, const Registry &reg, const ExportGeometry &g, const std::vector<double> &times,
                 const std::stop_token &stop, const ProgressFn &progress)
{
    gif::GifEncoder enc(g.px_w, g.px_h, o.loop);
    const auto      delays = gif::FrameDelaysCs(static_cast<int>(times.size()), o.fps);
    for (size_t i = 0; i < times.size(); ++i)
    {
        if (stop.stop_requested())
        {
            return std::unexpected(Cancelled());
        }
        const QImage img = RenderExportFrame(m, times[i], g, o.background, reg);
        enc.AddFrame(Pixels(img), delays[i]);
        if (progress)
        {
            progress(static_cast<int>(i + 1), static_cast<int>(times.size()));
        }
    }
    const auto &bytes = enc.Finish();
    QSaveFile   f(o.output_path);
    if (!f.open(QIODevice::WriteOnly) || f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size())) < 0 ||
        !f.commit())
    {
        return std::unexpected(QObject::tr("Could not write %1: %2").arg(o.output_path, f.errorString()));
    }
    return ExportResult{o.output_path, static_cast<int>(times.size()), static_cast<qint64>(bytes.size()), {}};
}

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

Result ExportVideo(const Model &m, const ExportOptions &o, const Registry &reg, const ExportGeometry &g, const std::vector<double> &times,
                   const std::stop_token &stop, const ProgressFn &progress)
{
    video::VideoEncoder enc;
    video::VideoOptions vo;
    vo.width        = g.px_w;
    vo.height       = g.px_h;
    vo.fps          = o.fps;
    vo.container    = o.format == ExportFormat::WebM ? video::Container::WebM : video::Container::Mp4;
    vo.quality      = o.quality;
    const auto path = PathFromUtf8(Us(o.output_path));
    if (auto r = enc.Open(path, vo); !r.has_value())
    {
        return std::unexpected(QObject::tr("Video: %1").arg(Qs(r.error())));
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
            return std::unexpected(QObject::tr("Video: %1").arg(Qs(r.error())));
        }
        if (progress)
        {
            progress(static_cast<int>(i + 1), static_cast<int>(times.size()));
        }
    }
    if (auto r = enc.Finish(); !r.has_value())
    {
        return std::unexpected(QObject::tr("Video: %1").arg(Qs(r.error())));
    }
    return ExportResult{o.output_path, static_cast<int>(times.size()), QFileInfo(o.output_path).size(), Qs(enc.EncoderName())};
}

Result ExportHtml(const Model &m, const ExportOptions &o, const Registry &reg)
{
    QFile tf(QString::fromLatin1(kPlayerResource));
    if (!tf.open(QIODevice::ReadOnly))
    {
        return std::unexpected(QObject::tr("The HTML player is not included in this build"));
    }
    const QByteArray tmpl = tf.readAll();

    // as when saving: definitions from plugins are embedded, so the page renders without them
    Model doc = m;
    reg.EmbedUsedDefinitions(doc);
    doc.scene.background = Us(o.background.name());
    const auto html      = MakePlayerHtml(std::string_view(tmpl.constData(), static_cast<size_t>(tmpl.size())), SerializeModel(doc, -1),
                                          doc.meta.name, PlayerOptions{o.autoplay, o.loop});
    if (!html.has_value())
    {
        return std::unexpected(Qs(html.error()));
    }
    QSaveFile f(o.output_path);
    if (!f.open(QIODevice::WriteOnly) || f.write(html->data(), static_cast<qint64>(html->size())) < 0 || !f.commit())
    {
        return std::unexpected(QObject::tr("Could not write %1: %2").arg(o.output_path, f.errorString()));
    }
    return ExportResult{o.output_path, 0, static_cast<qint64>(html->size()), {}};
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
    case ExportFormat::Html:
        return QStringLiteral("html");
    }
    return {};
}

std::optional<ExportFormat> FormatFromId(const QString &id)
{
    for (const auto f : {ExportFormat::Gif, ExportFormat::Png, ExportFormat::WebM, ExportFormat::Mp4, ExportFormat::Html})
    {
        if (FormatId(f).compare(id, Qt::CaseInsensitive) == 0)
        {
            return f;
        }
    }
    if (id.compare(QStringLiteral("htm"), Qt::CaseInsensitive) == 0)
    {
        return ExportFormat::Html;
    }
    return std::nullopt;
}

bool HtmlPlayerAvailable() { return QFile::exists(QString::fromLatin1(kPlayerResource)); }

QStringList AvailableFormatIds()
{
    QStringList ids{FormatId(ExportFormat::Gif), FormatId(ExportFormat::Png), FormatId(ExportFormat::WebM), FormatId(ExportFormat::Mp4)};
    if (HtmlPlayerAvailable())
    {
        ids << FormatId(ExportFormat::Html);
    }
    return ids;
}

bool IsVideo(ExportFormat f) { return f == ExportFormat::WebM || f == ExportFormat::Mp4; }

QString VideoEncoderFor(ExportFormat f)
{
    if (!IsVideo(f))
    {
        return {};
    }
    const auto encoders = video::EncodersFor(f == ExportFormat::WebM ? video::Container::WebM : video::Container::Mp4);
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
    if (o.format == ExportFormat::Html)
    {
        return ExportHtml(m, o, reg); // no frames: the page renders them in the browser
    }
    const ExportGeometry g     = PlanExport(m, o, reg);
    const auto           times = ExportFrameTimes(m.scenario.duration, o.fps);
    switch (o.format)
    {
    case ExportFormat::Gif:
        return ExportGif(m, o, reg, g, times, stop, progress);
    case ExportFormat::Png:
        return ExportPng(m, o, reg, g, times, stop, progress);
    case ExportFormat::WebM:
    case ExportFormat::Mp4:
        return ExportVideo(m, o, reg, g, times, stop, progress);
    case ExportFormat::Html:
        break;
    }
    return std::unexpected(QObject::tr("Unknown format"));
}

} // namespace ad::ui
