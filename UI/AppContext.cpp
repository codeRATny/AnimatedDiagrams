#include "AppContext.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <filesystem>
#include <set>

#include "ExportManager.hpp"
#include "Mcp/DocumentTools.hpp"
#include "Mcp/McpServer.hpp"
#include "McpHosts.hpp"
#include "McpHttpServer.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

constexpr auto kDisabledPluginsKey = "plugins/disabled";
constexpr auto kFavoritesKey       = "palette/favorites";

} // namespace

AppContext::AppContext(QObject *parent) : QObject(parent), _exports(std::make_unique<ExportManager>())
{
    _favorites = QSettings().value(kFavoritesKey).toStringList();
    _host      = std::make_unique<AppDocumentHost>(*this);
    _mcp       = std::make_unique<mcp::McpServer>(mcp::ServerInfo{
        "animated-diagrams", "Animated Diagrams", QCoreApplication::applicationVersion().toStdString(), mcp::DefaultInstructions()});
    mcp::RegisterDocumentTools(*_mcp, *_host);
    _http = new McpHttpServer(*_mcp, this);
    connect(_http, &McpHttpServer::RequestHandled, this, &AppContext::McpActivity);
    LoadPlugins(_plugins, _registry);
}

AppContext::~AppContext()
{
    delete _http; // before the MCP server it refers to
    _http = nullptr;
}

void AppContext::SetActive(Controller *ctl)
{
    if (_active == ctl)
    {
        return;
    }
    _active = ctl;
    Q_EMIT ActiveChanged(ctl);
}

// ---------------------------------------------------------------------------
// Plugins
// ---------------------------------------------------------------------------

QString AppContext::UserPluginDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
}

QString AppContext::SessionDir() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/session"); }

QStringList AppContext::SystemPluginDirs()
{
    const QString app = QCoreApplication::applicationDirPath();
    // next to the exe, Windows install root (exe in bin/), Linux prefix
    QStringList dirs{app + QStringLiteral("/plugins"), app + QStringLiteral("/../plugins"),
                     app + QStringLiteral("/../share/animated-diagrams/plugins")};
    // extra directories (tests, development, portable setups)
    for (const QString &d : qEnvironmentVariable("AD_PLUGIN_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts))
    {
        dirs << d;
    }
    return dirs;
}

void AppContext::LoadPlugins(PluginManager &plugins, Registry &registry)
{
    std::vector<std::filesystem::path> system_dirs;
    for (const QString &d : SystemPluginDirs())
    {
        system_dirs.emplace_back(d.toStdU16String());
    }
    plugins.SetDirectories(std::filesystem::path(UserPluginDir().toStdU16String()), system_dirs);
    std::set<std::string> disabled;
    for (const QString &id : QSettings().value(kDisabledPluginsKey).toStringList())
    {
        disabled.insert(Us(id));
    }
    plugins.SetDisabledIds(disabled);
    plugins.Scan();
    plugins.ApplyTo(registry);
}

void AppContext::ReloadPlugins()
{
    LoadPlugins(_plugins, _registry);
    Q_EMIT LibraryChanged();
}

void AppContext::SetPluginEnabled(const std::string &id, bool enabled)
{
    _plugins.SetEnabled(id, enabled);
    QStringList disabled;
    for (const auto &d : _plugins.DisabledIds())
    {
        disabled << Qs(d);
    }
    QSettings().setValue(kDisabledPluginsKey, disabled);
    _plugins.ApplyTo(_registry);
    Q_EMIT LibraryChanged();
}

// ---------------------------------------------------------------------------
// Favorites
// ---------------------------------------------------------------------------

void AppContext::SetFavorite(const QString &id, bool favorite)
{
    if (favorite == _favorites.contains(id))
    {
        return;
    }
    if (favorite)
    {
        _favorites << id;
    }
    else
    {
        _favorites.removeAll(id);
    }
    QSettings().setValue(kFavoritesKey, _favorites);
    Q_EMIT FavoritesChanged();
}

// ---------------------------------------------------------------------------
// MCP
// ---------------------------------------------------------------------------

bool AppContext::StartMcp(quint16 port, QString *error)
{
    const bool ok = _http->Start(port, error);
    Q_EMIT McpStateChanged();
    return ok;
}

void AppContext::StopMcp()
{
    _http->Stop();
    Q_EMIT McpStateChanged();
}

bool    AppContext::IsMcpRunning() const { return _http->IsRunning(); }
QString AppContext::McpUrl() const { return _http->Url(); }

} // namespace ad::ui
