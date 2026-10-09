// Animated Diagrams -- animated diagram editor (Qt 6, C++23).
//
//   animated-diagrams [file]                          GUI (file: .json or draw.io)
//   animated-diagrams --export out.gif [opts] file    headless export (no display needed; gif, png, webm, mp4, html)
//   animated-diagrams --convert out.json file.drawio  headless conversion to the native format
//   animated-diagrams --mcp [file]                    MCP server over stdio (for AI agents)
//   animated-diagrams --mcp-port 8765 [file]          GUI with the HTTP MCP server enabled

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <expected>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

#include "AppContext.hpp"
#include "Controller.hpp"
#include "CrashReports.hpp"
#include "Exporter.hpp"
#include "Import/DrawioImporter.hpp"
#include "Io/JsonIo.hpp"
#include "Language.hpp"
#include "MainWindow.hpp"
#include "Mcp/DocumentTools.hpp"
#include "Mcp/McpServer.hpp"
#include "Mcp/StdioTransport.hpp"
#include "McpHosts.hpp"
#include "Plugins/PluginManager.hpp"
#include "QtRender.hpp"
#include "Theme.hpp"
#include "TitleBar.hpp"
#include "Utils/File.hpp"

namespace
{

using namespace ad;
using namespace ad::ui;

bool HasArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], name) == 0)
        {
            return true;
        }
    }
    return false;
}

#ifdef _WIN32
/// A GUI application on Windows has no console: attach to the parent console for the
/// CLI modes unless the output is already redirected (file / pipe).
void AttachParentConsole()
{
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS))
    {
        FILE *f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
#endif

bool IsDrawioPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("drawio") || suffix == QStringLiteral("xml");
}

/// Load a native document or import a draw.io file.
std::expected<Model, std::string> LoadInput(const QString &path, int page)
{
    std::string bytes;
    try
    {
        bytes = ReadFile(PathFromUtf8(Us(path)));
    }
    catch (const std::exception &ex)
    {
        return std::unexpected(std::string(ex.what()));
    }
    if (IsDrawioPath(path))
    {
        DrawioImportOptions opt;
        opt.page = page;
        auto m   = ImportDrawio(bytes, opt);
        if (m.has_value() && (m->meta.name.empty() || m->meta.name == DrawioDefaultName()))
        {
            m->meta.name = Us(QFileInfo(path).completeBaseName());
        }
        return m;
    }
    return ParseModel(bytes);
}

int RunExport(const QCommandLineParser &cli, const Model &model, const Registry &reg)
{
    ExportOptions o;
    o.output_path        = cli.value(QStringLiteral("export"));
    const QString fmt    = cli.isSet(QStringLiteral("format")) ? cli.value(QStringLiteral("format")) : QFileInfo(o.output_path).suffix();
    const auto    format = FormatFromId(fmt);
    if (!format.has_value())
    {
        std::cerr << "error: unknown format '" << fmt.toStdString() << "' ("
                  << AvailableFormatIds().join(QStringLiteral(", ")).toStdString() << ")\n";
        return 2;
    }
    o.format     = *format;
    o.fps        = cli.value(QStringLiteral("fps")).toDouble();
    o.scale      = cli.value(QStringLiteral("scale")).toDouble();
    o.background = QColor(cli.isSet(QStringLiteral("background")) ? cli.value(QStringLiteral("background")) : Qs(model.scene.background));
    o.loop       = !cli.isSet(QStringLiteral("no-loop"));
    o.autoplay   = !cli.isSet(QStringLiteral("no-autoplay"));
    if (!o.background.isValid() || o.fps <= 0 || o.fps > 120 || o.scale <= 0 || o.scale > 10)
    {
        std::cerr << "error: invalid --fps/--scale/--background\n";
        return 2;
    }

    int        last_percent = -1;
    const auto result       = ad::ui::RunExport(model, o, reg, std::stop_token{},
                                                [&last_percent](int done, int total)
                                                {
                                              const int percent = done * 100 / std::max(1, total);
                                              if (percent / 10 == last_percent / 10 && done != total)
                                              {
                                                  return; // at most every 10%
                                              }
                                              last_percent = percent;
                                              std::cerr << "\rexport: " << percent << "% (" << done << "/" << total << " frames)"
                                                        << std::flush;
                                          });
    if (last_percent >= 0)
    {
        std::cerr << "\n"; // end of the progress line (the HTML export has no frames)
    }
    if (!result.has_value())
    {
        std::cerr << "error: " << result.error().toStdString() << "\n";
        return 1;
    }
    std::cout << "OK " << result->path.toStdString() << " frames=" << result->frames << " bytes=" << result->bytes << "\n";
    return 0;
}

int RunConvert(const QCommandLineParser &cli, Model model, const Registry &reg)
{
    const QString out = cli.value(QStringLiteral("convert"));
    reg.EmbedUsedDefinitions(model);
    try
    {
        WriteFile(PathFromUtf8(Us(out)), SerializeModel(model));
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "OK " << out.toStdString() << " nodes=" << model.nodes.size() << " edges=" << model.edges.size() << "\n";
    return 0;
}

int RunMcpStdio(const QStringList &files, int page, const Registry &reg)
{
#ifdef _WIN32
    // JSON-RPC lines must not be mangled by CRLF translation
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    HeadlessDocumentHost host(reg);
    if (!files.isEmpty())
    {
        auto model = LoadInput(files.front(), page);
        if (!model.has_value())
        {
            std::cerr << "error: " << model.error() << "\n";
            return 2;
        }
        host.Doc().Reset(std::move(*model));
        if (!IsDrawioPath(files.front()))
        {
            host.SetCurrentPath(Us(QFileInfo(files.front()).absoluteFilePath()));
        }
    }
    mcp::McpServer server(mcp::ServerInfo{"animated-diagrams", "Animated Diagrams", QCoreApplication::applicationVersion().toStdString(),
                                          mcp::DefaultInstructions()});
    mcp::RegisterDocumentTools(server, host);
    std::cerr << "animated-diagrams: MCP server on stdio (" << server.Tools().size() << " tools)\n";
    mcp::RunStdio(server, std::cin, std::cout, std::cerr);
    return 0;
}

/// --lang <code> / --lang=<code>: read before the command line parser runs, since the
/// translations must be installed before anything is created.
QString LanguageArgument(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg(argv[i]);
        if (arg == "--lang" && i + 1 < argc)
        {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
        if (arg.starts_with("--lang="))
        {
            return QString::fromLocal8Bit(arg.substr(7).data());
        }
    }
    return {};
}

} // namespace

int main(int argc, char **argv)
{
    // CLI modes do not need a display (CI, servers, ssh, agents)
    bool headless = false;
    for (const char *a : {"--export", "--convert", "--mcp", "--help", "-h", "--help-all", "--version", "-v"})
    {
        headless = headless || HasArg(argc, argv, a);
    }
    if (headless)
    {
#ifdef _WIN32
        AttachParentConsole();
#else
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
#endif
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("AnimatedDiagrams"));
    QApplication::setApplicationName(QStringLiteral("animated-diagrams"));
    QApplication::setApplicationDisplayName(QStringLiteral("Animated Diagrams"));
    QApplication::setApplicationVersion(QStringLiteral(AD_VERSION_STRING));
    QApplication::setDesktopFileName(QStringLiteral("animated-diagrams"));
    InstallCrashReporting(); // reports go to <app data>/crashes (GUI and headless modes)
    // before any window or library definition: labels are translated when created
    InstallTranslations(LanguageArgument(argc, argv));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Animated diagram editor: request flows, timers, retries, effects"));
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Diagram to open: .json or draw.io (.drawio / .xml)"));
    cli.addOptions({
        {QStringLiteral("export"),
         QStringLiteral("Export without GUI to <path> (%1 by extension)").arg(AvailableFormatIds().join(QLatin1Char('/'))),
         QStringLiteral("path")},
        {QStringLiteral("format"), QStringLiteral("Export format: %1").arg(AvailableFormatIds().join(QStringLiteral(", "))),
         QStringLiteral("format")},
        {QStringLiteral("fps"), QStringLiteral("Frame rate (default 15)"), QStringLiteral("fps"), QStringLiteral("15")},
        {QStringLiteral("scale"), QStringLiteral("Resolution scale (default 1)"), QStringLiteral("scale"), QStringLiteral("1")},
        {QStringLiteral("background"), QStringLiteral("Background color #rrggbb (default: scene background)"), QStringLiteral("color")},
        {QStringLiteral("no-loop"), QStringLiteral("GIF / HTML player without looping")},
        {QStringLiteral("no-autoplay"), QStringLiteral("HTML player: do not start playing when opened")},
        {QStringLiteral("convert"), QStringLiteral("Save the input (e.g. a draw.io file) as a native .json document"),
         QStringLiteral("path")},
        {QStringLiteral("page"), QStringLiteral("draw.io page index (default 0)"), QStringLiteral("index"), QStringLiteral("0")},
        {QStringLiteral("mcp"), QStringLiteral("Run the MCP server over stdio (JSON-RPC lines) without a window")},
        {QStringLiteral("lang"), QStringLiteral("Interface language: en, ru, ... (default: the saved choice or the system language)"),
         QStringLiteral("code")},
        {QStringLiteral("mcp-port"), QStringLiteral("Start the HTTP MCP server on 127.0.0.1:<port> together with the GUI"),
         QStringLiteral("port")},
    });
    cli.process(app);

    const QStringList files = cli.positionalArguments();
    const int         page  = std::max(0, cli.value(QStringLiteral("page")).toInt());

    if (cli.isSet(QStringLiteral("mcp")) || cli.isSet(QStringLiteral("export")) || cli.isSet(QStringLiteral("convert")))
    {
        PluginManager plugins;
        Registry      registry;
        AppContext::LoadPlugins(plugins, registry);
        for (const auto &e : plugins.Errors())
        {
            std::cerr << "warning: plugin " << PathToUtf8(e.path) << ": " << e.message << "\n";
        }
        if (cli.isSet(QStringLiteral("mcp")))
        {
            return RunMcpStdio(files, page, registry);
        }
        if (files.size() != 1)
        {
            std::cerr << "error: --export / --convert require exactly one input file\n";
            return 2;
        }
        auto model = LoadInput(files.front(), page);
        if (!model.has_value())
        {
            std::cerr << "error: " << model.error() << "\n";
            return 2;
        }
        if (cli.isSet(QStringLiteral("convert")))
        {
            const int rc = RunConvert(cli, *model, registry);
            if (rc != 0 || !cli.isSet(QStringLiteral("export")))
            {
                return rc;
            }
        }
        return RunExport(cli, *model, registry);
    }

    int mcp_port = 0;
    if (cli.isSet(QStringLiteral("mcp-port")))
    {
        bool ok  = false;
        mcp_port = cli.value(QStringLiteral("mcp-port")).toInt(&ok);
        if (!ok || mcp_port < 1 || mcp_port > 65535)
        {
            std::cerr << "error: invalid --mcp-port\n";
            return 2;
        }
    }

    ApplyTheme();
    InstallNativeFrameTheming();
    AppContext ctx;
    MainWindow window(ctx);
    window.RestoreSession(files.isEmpty()); // all tabs of the previous session
    for (const QString &f : files)
    {
        window.OpenPath(f); // command line files open in tabs
    }
    window.StartMcpOnLaunch(mcp_port);
    window.show();
    QTimer::singleShot(0, &window,
                       [&window]
                       {
                           NotifyAboutCrashReports(&window); // a report left by the previous session
                       });
    return QApplication::exec();
}
