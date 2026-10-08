#include "Exporter.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <span>

#include "Engine/Scene.hpp"
#include "Export/GifEncoder.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

std::span<const uint8_t> Pixels(const QImage &img) { return {img.constBits(), static_cast<size_t>(img.sizeInBytes())}; }

QString Cancelled() { return QObject::tr("Экспорт прерван"); }

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
        return std::unexpected(QObject::tr("Не удалось записать %1: %2").arg(o.output_path, f.errorString()));
    }
    return ExportResult{o.output_path, static_cast<int>(times.size()), static_cast<qint64>(bytes.size())};
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
            return std::unexpected(QObject::tr("Не удалось записать %1").arg(path));
        }
        total += QFileInfo(path).size();
        if (progress)
        {
            progress(static_cast<int>(i + 1), static_cast<int>(times.size()));
        }
    }
    return ExportResult{base + QStringLiteral("_*.png"), static_cast<int>(times.size()), total};
}

Result ExportVideo(const Model &m, const ExportOptions &o, const Registry &reg, const ExportGeometry &g, const std::vector<double> &times,
                   const std::stop_token &stop, const ProgressFn &progress)
{
    const QString ffmpeg = o.ffmpeg.isEmpty() ? FindFfmpeg() : o.ffmpeg;
    if (ffmpeg.isEmpty())
    {
        return std::unexpected(QObject::tr("Для WebM/MP4 нужен ffmpeg в PATH (sudo apt install ffmpeg / winget install ffmpeg)"));
    }
    QStringList args{"-hide_banner", "-loglevel",
                     "error",        "-y",
                     "-f",           "rawvideo",
                     "-pix_fmt",     "rgba",
                     "-s",           QStringLiteral("%1x%2").arg(g.px_w).arg(g.px_h),
                     "-framerate",   QString::number(o.fps),
                     "-i",           "-"};
    if (o.format == ExportFormat::WebM)
    {
        args << "-c:v" << "libvpx-vp9" << "-b:v" << "0" << "-crf" << "32" << "-row-mt" << "1" << "-deadline" << "good" << "-cpu-used"
             << "4";
    }
    else
    {
        args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "20" << "-movflags" << "+faststart";
    }
    args << "-pix_fmt" << "yuv420p" << o.output_path;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(15000))
    {
        return std::unexpected(QObject::tr("Не удалось запустить ffmpeg: %1").arg(proc.errorString()));
    }
    auto fail = [&](const QString &why) -> Result
    {
        proc.kill();
        proc.waitForFinished(5000);
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return std::unexpected(err.isEmpty() ? why : why + QStringLiteral("\n") + err.right(800));
    };
    for (size_t i = 0; i < times.size(); ++i)
    {
        if (stop.stop_requested())
        {
            proc.kill();
            proc.waitForFinished(5000);
            QFile::remove(o.output_path);
            return std::unexpected(Cancelled());
        }
        const QImage img = RenderExportFrame(m, times[i], g, o.background, reg);
        const auto   px  = Pixels(img);
        if (proc.write(reinterpret_cast<const char *>(px.data()), static_cast<qint64>(px.size())) < 0)
        {
            return fail(QObject::tr("ffmpeg: ошибка записи кадра"));
        }
        while (proc.bytesToWrite() > 0)
        {
            if (!proc.waitForBytesWritten(60000))
            {
                return fail(QObject::tr("ffmpeg не принимает данные"));
            }
        }
        if (progress)
        {
            progress(static_cast<int>(i + 1), static_cast<int>(times.size()));
        }
    }
    proc.closeWriteChannel();
    if (!proc.waitForFinished(-1) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        return fail(QObject::tr("ffmpeg завершился с ошибкой (код %1)").arg(proc.exitCode()));
    }
    return ExportResult{o.output_path, static_cast<int>(times.size()), QFileInfo(o.output_path).size()};
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

bool NeedsFfmpeg(ExportFormat f) { return f == ExportFormat::WebM || f == ExportFormat::Mp4; }

QString FindFfmpeg() { return QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); }

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
        return std::unexpected(QObject::tr("Не указан файл для сохранения"));
    }
    if (o.fps <= 0)
    {
        return std::unexpected(QObject::tr("Некорректная частота кадров"));
    }
    const QFileInfo out(o.output_path);
    if (!QDir().mkpath(out.absolutePath()))
    {
        return std::unexpected(QObject::tr("Нет доступа к каталогу %1").arg(out.absolutePath()));
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
    }
    return std::unexpected(QObject::tr("Неизвестный формат"));
}

} // namespace ad::ui
