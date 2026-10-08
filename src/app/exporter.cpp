#include "exporter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <span>

#include "ad/gif.hpp"
#include "ad/scene.hpp"
#include "qt_render.hpp"

namespace app {

namespace {

std::span<const std::uint8_t> pixels(const QImage& img) {
    return {img.constBits(), static_cast<std::size_t>(img.sizeInBytes())};
}

QString cancelled() { return QObject::tr("Экспорт прерван"); }

std::expected<ExportResult, QString> exportGif(const ad::Model& m, const ExportOptions& o, const ad::ExportGeometry& g,
                                               const std::vector<double>& times, std::stop_token stop,
                                               const ProgressFn& progress) {
    ad::gif::Encoder enc(g.pxW, g.pxH, o.loop);
    const auto delays = ad::gif::frameDelaysCs(static_cast<int>(times.size()), o.fps);
    for (std::size_t i = 0; i < times.size(); ++i) {
        if (stop.stop_requested()) return std::unexpected(cancelled());
        const QImage img = renderExportFrame(m, times[i], g, o.background);
        enc.addFrame(pixels(img), delays[i]);
        if (progress) progress(static_cast<int>(i + 1), static_cast<int>(times.size()));
    }
    const auto& bytes = enc.finish();
    QSaveFile f(o.outputPath);
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) < 0 || !f.commit())
        return std::unexpected(QObject::tr("Не удалось записать %1: %2").arg(o.outputPath, f.errorString()));
    return ExportResult{o.outputPath, static_cast<int>(times.size()), static_cast<qint64>(bytes.size())};
}

std::expected<ExportResult, QString> exportPng(const ad::Model& m, const ExportOptions& o, const ad::ExportGeometry& g,
                                               const std::vector<double>& times, std::stop_token stop,
                                               const ProgressFn& progress) {
    // name.png → name_0001.png, name_0002.png, ...
    const QFileInfo fi(o.outputPath);
    const QString base = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
    qint64 total = 0;
    for (std::size_t i = 0; i < times.size(); ++i) {
        if (stop.stop_requested()) return std::unexpected(cancelled());
        const QString path = QStringLiteral("%1_%2.png").arg(base).arg(i + 1, 4, 10, QLatin1Char('0'));
        const QImage img = renderExportFrame(m, times[i], g, o.background);
        if (!img.save(path, "PNG")) return std::unexpected(QObject::tr("Не удалось записать %1").arg(path));
        total += QFileInfo(path).size();
        if (progress) progress(static_cast<int>(i + 1), static_cast<int>(times.size()));
    }
    return ExportResult{base + QStringLiteral("_*.png"), static_cast<int>(times.size()), total};
}

std::expected<ExportResult, QString> exportVideo(const ad::Model& m, const ExportOptions& o, const ad::ExportGeometry& g,
                                                 const std::vector<double>& times, std::stop_token stop,
                                                 const ProgressFn& progress) {
    const QString ffmpeg = o.ffmpeg.isEmpty() ? findFfmpeg() : o.ffmpeg;
    if (ffmpeg.isEmpty())
        return std::unexpected(QObject::tr("Для WebM/MP4 нужен ffmpeg в PATH (sudo apt install ffmpeg / winget install ffmpeg)"));

    QStringList args{"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba",
                     "-s", QStringLiteral("%1x%2").arg(g.pxW).arg(g.pxH), "-framerate", QString::number(o.fps), "-i", "-"};
    if (o.format == ExportFormat::WebM)
        args << "-c:v" << "libvpx-vp9" << "-b:v" << "0" << "-crf" << "32" << "-row-mt" << "1" << "-deadline" << "good"
             << "-cpu-used" << "4";
    else
        args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "20" << "-movflags" << "+faststart";
    args << "-pix_fmt" << "yuv420p" << o.outputPath;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(15000)) return std::unexpected(QObject::tr("Не удалось запустить ffmpeg: %1").arg(proc.errorString()));

    auto fail = [&](const QString& why) -> std::expected<ExportResult, QString> {
        proc.kill();
        proc.waitForFinished(5000);
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return std::unexpected(err.isEmpty() ? why : why + QStringLiteral("\n") + err.right(800));
    };

    for (std::size_t i = 0; i < times.size(); ++i) {
        if (stop.stop_requested()) {
            proc.kill();
            proc.waitForFinished(5000);
            QFile::remove(o.outputPath);
            return std::unexpected(cancelled());
        }
        const QImage img = renderExportFrame(m, times[i], g, o.background);
        const auto px = pixels(img);
        if (proc.write(reinterpret_cast<const char*>(px.data()), static_cast<qint64>(px.size())) < 0)
            return fail(QObject::tr("ffmpeg: ошибка записи кадра"));
        while (proc.bytesToWrite() > 0)
            if (!proc.waitForBytesWritten(60000)) return fail(QObject::tr("ffmpeg не принимает данные"));
        if (progress) progress(static_cast<int>(i + 1), static_cast<int>(times.size()));
    }
    proc.closeWriteChannel();
    if (!proc.waitForFinished(-1) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return fail(QObject::tr("ffmpeg завершился с ошибкой (код %1)").arg(proc.exitCode()));
    return ExportResult{o.outputPath, static_cast<int>(times.size()), QFileInfo(o.outputPath).size()};
}

}  // namespace

QString formatId(ExportFormat f) {
    switch (f) {
        case ExportFormat::Gif: return QStringLiteral("gif");
        case ExportFormat::Png: return QStringLiteral("png");
        case ExportFormat::WebM: return QStringLiteral("webm");
        case ExportFormat::Mp4: return QStringLiteral("mp4");
    }
    return {};
}

QString formatExtension(ExportFormat f) { return formatId(f); }

std::optional<ExportFormat> formatFromId(const QString& id) {
    for (const auto f : {ExportFormat::Gif, ExportFormat::Png, ExportFormat::WebM, ExportFormat::Mp4})
        if (formatId(f).compare(id, Qt::CaseInsensitive) == 0) return f;
    return std::nullopt;
}

bool needsFfmpeg(ExportFormat f) { return f == ExportFormat::WebM || f == ExportFormat::Mp4; }

QString findFfmpeg() { return QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); }

ad::ExportGeometry planExport(const ad::Model& m, const ExportOptions& o) {
    const ad::Rect world =
        o.framing == Framing::View ? o.viewRect : ad::contentBounds(m, QtTextMeasurer{}).adjusted(ad::kExportPadding);
    return ad::exportGeometry(world, o.scale);
}

QImage renderExportFrame(const ad::Model& m, double t, const ad::ExportGeometry& g, const QColor& bg) {
    QImage img(g.pxW, g.pxH, QImage::Format_RGBA8888);
    img.fill(bg);
    QPainter p(&img);
    const double k = std::min(g.pxW / g.world.w, g.pxH / g.world.h);
    p.translate((g.pxW - g.world.w * k) / 2, (g.pxH - g.world.h * k) / 2);
    p.scale(k, k);
    p.translate(-g.world.x, -g.world.y);
    ad::SceneOptions opt;
    opt.editorChrome = false;
    renderFrame(p, ad::buildFrame(m, t, opt, QtTextMeasurer{}));
    p.end();
    return img;
}

std::expected<ExportResult, QString> runExport(const ad::Model& m, const ExportOptions& o, std::stop_token stop,
                                               const ProgressFn& progress) {
    if (o.outputPath.isEmpty()) return std::unexpected(QObject::tr("Не указан файл для сохранения"));
    if (!(o.fps > 0)) return std::unexpected(QObject::tr("Некорректная частота кадров"));
    const QFileInfo out(o.outputPath);
    if (!QDir().mkpath(out.absolutePath())) return std::unexpected(QObject::tr("Нет доступа к каталогу %1").arg(out.absolutePath()));

    const ad::ExportGeometry g = planExport(m, o);
    const auto times = ad::exportFrameTimes(m.scenario.duration, o.fps);
    switch (o.format) {
        case ExportFormat::Gif: return exportGif(m, o, g, times, stop, progress);
        case ExportFormat::Png: return exportPng(m, o, g, times, stop, progress);
        case ExportFormat::WebM:
        case ExportFormat::Mp4: return exportVideo(m, o, g, times, stop, progress);
    }
    return std::unexpected(QObject::tr("Неизвестный формат"));
}

}  // namespace app
