// Animated Diagrams — редактор анимированных диаграмм (Qt 6, C++23).
//
//   animated-diagrams [file.json]                       — GUI
//   animated-diagrams --export out.gif [опции] file.json — headless-экспорт (без дисплея)

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "ad/json_io.hpp"
#include "controller.hpp"
#include "exporter.hpp"
#include "main_window.hpp"
#include "qt_render.hpp"
#include "theme.hpp"

namespace {

bool hasArg(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

#ifdef _WIN32
/// GUI-приложение на Windows не имеет консоли: для CLI подключаемся к консоли родителя,
/// если вывод ещё никуда не направлен (при перенаправлении в файл/pipe оставляем как есть).
void attachParentConsole() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
#endif

int runCli(const QCommandLineParser& cli, const QString& input) {
    using namespace app;
    QFile f(input);
    if (!f.open(QIODevice::ReadOnly)) {
        std::cerr << "error: cannot open " << input.toStdString() << ": " << f.errorString().toStdString() << "\n";
        return 2;
    }
    const QByteArray bytes = f.readAll();
    const auto model = ad::parseModel(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!model) {
        std::cerr << "error: " << model.error() << "\n";
        return 2;
    }

    ExportOptions o;
    o.outputPath = cli.value(QStringLiteral("export"));
    const QString fmt = cli.isSet(QStringLiteral("format")) ? cli.value(QStringLiteral("format"))
                                                             : QFileInfo(o.outputPath).suffix();
    const auto format = formatFromId(fmt);
    if (!format) {
        std::cerr << "error: unknown format '" << fmt.toStdString() << "' (gif, png, webm, mp4)\n";
        return 2;
    }
    o.format = *format;
    o.fps = cli.value(QStringLiteral("fps")).toDouble();
    o.scale = cli.value(QStringLiteral("scale")).toDouble();
    o.background = QColor(cli.value(QStringLiteral("background")));
    o.loop = !cli.isSet(QStringLiteral("no-loop"));
    if (!o.background.isValid() || o.fps <= 0 || o.fps > 120 || o.scale <= 0 || o.scale > 10) {
        std::cerr << "error: invalid --fps/--scale/--background\n";
        return 2;
    }

    int lastPercent = -1;
    const auto result = runExport(*model, o, std::stop_token{}, [&lastPercent](int done, int total) {
        const int percent = done * 100 / std::max(1, total);
        if (percent / 10 == lastPercent / 10 && done != total) return;  // не чаще, чем каждые 10%
        lastPercent = percent;
        std::cerr << "\rexport: " << percent << "% (" << done << "/" << total << " frames)" << std::flush;
    });
    std::cerr << "\n";
    if (!result) {
        std::cerr << "error: " << result.error().toStdString() << "\n";
        return 1;
    }
    std::cout << "OK " << result->path.toStdString() << " frames=" << result->frames << " bytes=" << result->bytes << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // CLI-режимы не требуют дисплея (CI, серверы, ssh)
    bool headless = false;
    for (const char* a : {"--export", "--help", "-h", "--help-all", "--version", "-v"}) headless = headless || hasArg(argc, argv, a);
    if (headless) {
#ifdef _WIN32
        attachParentConsole();
#else
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("AnimatedDiagrams"));
    QApplication::setApplicationName(QStringLiteral("animated-diagrams"));
    QApplication::setApplicationDisplayName(QStringLiteral("Animated Diagrams"));
    QApplication::setApplicationVersion(QStringLiteral(AD_VERSION_STRING));
    QApplication::setDesktopFileName(QStringLiteral("animated-diagrams"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Редактор анимированных диаграмм и flow-сценариев"));
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Диаграмма (.json) для открытия/экспорта"));
    cli.addOptions({
        {QStringLiteral("export"), QStringLiteral("Экспорт без GUI в <path> (gif/png/webm/mp4 по расширению)"), QStringLiteral("path")},
        {QStringLiteral("format"), QStringLiteral("Формат экспорта: gif, png, webm, mp4"), QStringLiteral("format")},
        {QStringLiteral("fps"), QStringLiteral("Частота кадров (по умолчанию 15)"), QStringLiteral("fps"), QStringLiteral("15")},
        {QStringLiteral("scale"), QStringLiteral("Масштаб разрешения (по умолчанию 1)"), QStringLiteral("scale"), QStringLiteral("1")},
        {QStringLiteral("background"), QStringLiteral("Цвет фона (#rrggbb)"), QStringLiteral("color"), QStringLiteral("#0a111f")},
        {QStringLiteral("no-loop"), QStringLiteral("GIF без зацикливания")},
    });
    cli.process(app);

    const QStringList files = cli.positionalArguments();
    if (cli.isSet(QStringLiteral("export"))) {
        if (files.size() != 1) {
            std::cerr << "error: --export requires exactly one input file\n";
            return 2;
        }
        return runCli(cli, files.front());
    }

    app::applyTheme(app);
    app::Controller ctl;
    app::MainWindow window(ctl);
    if (files.isEmpty() || !window.openPath(files.front())) ctl.restoreSession();
    window.show();
    return QApplication::exec();
}
