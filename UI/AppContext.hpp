#ifndef _UI_APP_CONTEXT_HPP_
#define _UI_APP_CONTEXT_HPP_

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "Model/Registry.hpp"
#include "Plugins/PluginManager.hpp"

/// @file AppContext.hpp
/// @brief State shared by all open documents: the definition registry and plugins,
///        favorites, background exports and the built-in MCP server.

namespace ad::mcp
{
class McpServer;
}

namespace ad::ui
{

class AppDocumentHost;
class Controller;
class ExportManager;
class McpHttpServer;

/// Open documents (tabs); implemented by the main window.
class Workspace
{
public:
    virtual ~Workspace() = default;

    [[nodiscard]] virtual std::vector<Controller *> Documents() const = 0;
    /// Open an empty document in a new tab, make it active and return it.
    virtual Controller *NewDocumentTab()          = 0;
    virtual void        Activate(Controller *ctl) = 0;
};

class AppContext : public QObject
{
    Q_OBJECT

public:
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    AppContext(const AppContext &)            = delete;
    AppContext &operator=(const AppContext &) = delete;

    [[nodiscard]] const Registry &Reg() const { return _registry; }
    [[nodiscard]] PluginManager  &Plugins() { return _plugins; }
    [[nodiscard]] ExportManager  &Exports() { return *_exports; }

    // -----------------------------------------------------------------------
    // Documents
    // -----------------------------------------------------------------------
    void                      SetWorkspace(Workspace *w) { _workspace = w; }
    [[nodiscard]] Workspace  *GetWorkspace() const { return _workspace; }
    [[nodiscard]] Controller *Active() const { return _active; }
    void                      SetActive(Controller *ctl);

    // -----------------------------------------------------------------------
    // Plugins
    // -----------------------------------------------------------------------
    /// Rescan plugin directories and rebuild the registry.
    void ReloadPlugins();
    void SetPluginEnabled(const std::string &id, bool enabled);
    /// Configure directories and disabled ids from the settings, scan and fill the registry.
    static void        LoadPlugins(PluginManager &plugins, Registry &registry);
    static QString     UserPluginDir();
    static QStringList SystemPluginDirs();
    /// Directory with the autosaved tabs of the last session.
    static QString SessionDir();

    // -----------------------------------------------------------------------
    // Favorite element types (palette)
    // -----------------------------------------------------------------------
    [[nodiscard]] const QStringList &Favorites() const { return _favorites; }
    [[nodiscard]] bool               IsFavorite(const QString &id) const { return _favorites.contains(id); }
    void                             SetFavorite(const QString &id, bool favorite);

    // -----------------------------------------------------------------------
    // MCP server (HTTP, localhost only) -- works on the active document
    // -----------------------------------------------------------------------
    bool                          StartMcp(quint16 port, QString *error);
    void                          StopMcp();
    [[nodiscard]] bool            IsMcpRunning() const;
    [[nodiscard]] QString         McpUrl() const;
    [[nodiscard]] mcp::McpServer &Mcp() { return *_mcp; }

Q_SIGNALS:
    /// Plugins or the registry changed.
    void LibraryChanged();
    void FavoritesChanged();
    void ActiveChanged(ad::ui::Controller *ctl);
    void McpStateChanged();
    /// A tool call arrived over MCP (for the status bar).
    void McpActivity(const QString &text);

private:
    Registry                         _registry;
    PluginManager                    _plugins;
    QStringList                      _favorites;
    Workspace                       *_workspace = nullptr;
    Controller                      *_active    = nullptr;
    std::unique_ptr<ExportManager>   _exports;
    std::unique_ptr<AppDocumentHost> _host;
    std::unique_ptr<mcp::McpServer>  _mcp;
    McpHttpServer                   *_http = nullptr;
};

} // namespace ad::ui

#endif // _UI_APP_CONTEXT_HPP_
