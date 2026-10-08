#pragma once
// Экспорт анимации: GIF (встроенный энкодер), последовательность PNG, WebM/MP4 (через ffmpeg).
// Работает без UI — используется и диалогом, и CLI (--export).

#include <QColor>
#include <QImage>
#include <QString>

#include <expected>
#include <functional>
#include <optional>
#include <stop_token>

#include "ad/export_plan.hpp"
#include "ad/model.hpp"

namespace app {

enum class ExportFormat { Gif, Png, WebM, Mp4 };
enum class Framing { Content, View };

struct ExportOptions {
    ExportFormat format = ExportFormat::Gif;
    double fps = 15;
    double scale = 1;
    Framing framing = Framing::Content;
    ad::Rect viewRect;  // для Framing::View — видимая область холста (мировые координаты)
    QColor background = QColor(0x0a, 0x11, 0x1f);
    bool loop = true;   // только GIF
    QString outputPath;
    QString ffmpeg;     // путь к ffmpeg для WebM/MP4 (пусто — искать в PATH)
};

struct ExportResult {
    QString path;
    int frames = 0;
    qint64 bytes = 0;
};

using ProgressFn = std::function<void(int done, int total)>;

QString formatId(ExportFormat f);
QString formatExtension(ExportFormat f);
std::optional<ExportFormat> formatFromId(const QString& id);
bool needsFfmpeg(ExportFormat f);
/// Путь к ffmpeg из PATH (или пустая строка).
QString findFfmpeg();

ad::ExportGeometry planExport(const ad::Model& m, const ExportOptions& o);
QImage renderExportFrame(const ad::Model& m, double t, const ad::ExportGeometry& g, const QColor& bg);

/// Полный экспорт; проверяет stop-токен между кадрами. Ошибка — человекочитаемая строка.
std::expected<ExportResult, QString> runExport(const ad::Model& m, const ExportOptions& o, std::stop_token stop,
                                               const ProgressFn& progress = {});

}  // namespace app
