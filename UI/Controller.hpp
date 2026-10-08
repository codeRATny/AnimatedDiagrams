#ifndef _UI_CONTROLLER_HPP_
#define _UI_CONTROLLER_HPP_

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <string>

#include "Model/Document.hpp"
#include "Model/Registry.hpp"
#include "Plugins/PluginManager.hpp"

/// @file Controller.hpp
/// @brief Application controller: document + selection + playback + files / autosave +
///        plugins + the built-in MCP server. Widgets talk to each other only through it.

namespace ad::mcp
{
class McpServer;
}

namespace ad::ui
{

class AppDocumentHost;
class McpHttpServer;

class Controller : public QObject
{
    Q_OBJECT

public:
    explicit Controller(QObject *parent = nullptr);
    ~Controller() override;

    [[nodiscard]] const Model    &GetModel() const { return _doc.Get(); }
    [[nodiscard]] Document       &Doc() { return _doc; }
    [[nodiscard]] const Registry &Reg() const { return _registry; }
    [[nodiscard]] PluginManager  &Plugins() { return _plugins; }

    // -----------------------------------------------------------------------
    // Selection
    // -----------------------------------------------------------------------
    [[nodiscard]] const Selection &GetSelection() const { return _selection; }
    void                           Select(Selection::Kind kind, const std::string &id);
    void                           ClearSelection() { Select(Selection::Kind::None, {}); }
    void                           DeleteSelection();

    // -----------------------------------------------------------------------
    // Editing
    // -----------------------------------------------------------------------
    /// Checkpoint(merge_key) + fn(model) + notification. The same merge key in a row = one undo step.
    void Edit(const std::string &merge_key, const std::function<void(Model &)> &fn, bool structural = false);
    /// Notify after Document operations (AddNode, ...).
    void Changed(bool structural = true);
    /// Change zoom / pan -- not recorded in the history, does not mark the document modified.
    void SetView(const View &v);
    void Undo();
    void Redo();

    // -----------------------------------------------------------------------
    // Files
    // -----------------------------------------------------------------------
    void NewDocument();
    void LoadSample();
    bool OpenFile(const QString &path, QString *error);
    bool SaveFile(const QString &path, QString *error);
    bool ImportDrawio(const QString &path, int page, bool keep_colors, QString *error, QString *report = nullptr);
    void ReplaceModel(Model m, const QString &path, bool modified);
    /// Restore the last session from the autosave (or load the example).
    void                         RestoreSession();
    [[nodiscard]] const QString &FilePath() const { return _path; }
    void                         SetFilePath(const QString &path);
    [[nodiscard]] bool           IsModified() const { return _modified; }
    void                         Rename(const QString &name);

    // -----------------------------------------------------------------------
    // Playback
    // -----------------------------------------------------------------------
    [[nodiscard]] double Time() const { return _time; }
    [[nodiscard]] double Duration() const { return GetModel().scenario.duration; }
    [[nodiscard]] bool   IsPlaying() const { return _playing; }
    [[nodiscard]] double Speed() const { return _speed; }
    [[nodiscard]] bool   Loop() const { return _loop; }
    void                 Play();
    void                 Pause();
    void                 TogglePlay();
    void                 Stop();
    void                 Seek(double t);
    void                 SetSpeed(double s) { _speed = s; }
    void                 SetLoop(bool l) { _loop = l; }

    // -----------------------------------------------------------------------
    // Plugins
    // -----------------------------------------------------------------------
    /// Rescan plugin directories and rebuild the registry.
    void ReloadPlugins();
    void SetPluginEnabled(const std::string &id, bool enabled);

    // -----------------------------------------------------------------------
    // MCP server (HTTP, localhost only)
    // -----------------------------------------------------------------------
    bool                          StartMcp(quint16 port, QString *error);
    void                          StopMcp();
    [[nodiscard]] bool            IsMcpRunning() const;
    [[nodiscard]] QString         McpUrl() const;
    [[nodiscard]] mcp::McpServer &Mcp() { return *_mcp; }

    /// Configure directories and disabled ids from the settings, scan and fill the registry.
    static void        LoadPlugins(PluginManager &plugins, Registry &registry);
    static QString     AutosavePath();
    static QString     UserPluginDir();
    static QStringList SystemPluginDirs();

Q_SIGNALS:
    void ModelChanged(bool structural);
    void SelectionChanged();
    void TimeChanged(double t);
    void PlayingChanged(bool playing);
    /// Name, path, modified flag, undo / redo availability.
    void DocumentStateChanged();
    /// Plugins or the registry changed (palette and library lists must be refreshed).
    void LibraryChanged();
    void McpStateChanged();
    /// A tool call arrived over MCP (for the status bar).
    void McpActivity(const QString &text);

private:
    friend class AppDocumentHost;

    void _Tick();
    void _ValidateSelection();
    void _ScheduleAutosave();
    void _WriteAutosave();
    void _OnReplaced();

    Document      _doc;
    Registry      _registry;
    PluginManager _plugins;
    Selection     _selection;
    QString       _path;
    bool          _modified = false;

    double        _time    = 0;
    bool          _playing = false;
    double        _speed   = 1;
    bool          _loop    = true;
    QTimer        _frame_timer;
    QElapsedTimer _clock;
    qint64        _last_ms = 0;
    QTimer        _autosave_timer;

    std::unique_ptr<AppDocumentHost> _host;
    std::unique_ptr<mcp::McpServer>  _mcp;
    McpHttpServer                   *_http = nullptr;
};

} // namespace ad::ui

#endif // _UI_CONTROLLER_HPP_
