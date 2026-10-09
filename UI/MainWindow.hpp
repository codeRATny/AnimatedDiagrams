#ifndef _UI_MAIN_WINDOW_HPP_
#define _UI_MAIN_WINDOW_HPP_

#include <QByteArray>
#include <QMainWindow>
#include <QPointer>

#include <vector>

#include "AppContext.hpp"

class QAction;
class QDockWidget;
class QLabel;
class QProgressBar;
class QStackedWidget;
class QTabWidget;
class QToolBar;
class QToolButton;

/// @file MainWindow.hpp
/// @brief Main window: documents in tabs; element palette, inspector, timeline and
///        background exports in dock panels the user can move, float, hide and lock;
///        menus; session restore of all tabs.

namespace ad::ui
{

class CanvasWidget;
class Controller;
class Inspector;
class LibraryDialog;
class PaletteWidget;
class PluginsDialog;
class TimelinePanel;

class MainWindow : public QMainWindow, public Workspace
{
    Q_OBJECT

public:
    explicit MainWindow(AppContext &ctx, QWidget *parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow &)            = delete;
    MainWindow &operator=(const MainWindow &) = delete;

    // Workspace
    [[nodiscard]] std::vector<Controller *> Documents() const override;
    Controller                             *NewDocumentTab() override;
    void                                    Activate(Controller *ctl) override;

    /// Open a file in a tab (draw.io files are imported); false and a message box on error.
    bool OpenPath(const QString &path);
    /// Import a draw.io file into a new tab (asks for the page when there are several).
    bool ImportDrawioPath(const QString &path);
    /// Reopen the tabs of the previous session; on the first run open the example
    /// (or an empty tab when `example` is false, e.g. files are given on the command line).
    void RestoreSession(bool example = true);
    /// Start the MCP server if enabled in the settings (or forced by the command line).
    void StartMcpOnLaunch(int forced_port);

protected:
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    struct Tab
    {
        Controller    *ctl       = nullptr;
        CanvasWidget  *canvas    = nullptr;
        Inspector     *inspector = nullptr;
        TimelinePanel *timeline  = nullptr;
    };

    Tab                      *_AddTab();
    void                      _CloseTab(int index);
    bool                      _ConfirmClose(Tab &tab);
    void                      _OnCurrentTabChanged(int index);
    void                      _UpdateTabText(Controller *ctl);
    [[nodiscard]] Tab        *_TabOf(Controller *ctl);
    [[nodiscard]] Tab        *_Current();
    [[nodiscard]] Controller *_Ctl();
    [[nodiscard]] bool        _IsPristine(Controller *ctl) const;
    /// Tab for a document that replaces nothing: the current tab when it is pristine, a new one otherwise.
    Tab *_TargetTab();
    void _SaveSession();

    void _BuildDocks();
    void _BuildToolBar();
    void _BuildMenus();
    void _BuildStatusBar();
    void _ResetLayout();
    void _SetLocked(bool locked);
    void _UpdateTitle();
    void _UpdateMcpState();
    void _UpdateExportsIndicator();

    void _NewDiagram();
    void _OpenDiagram();
    void _ImportDrawio();
    bool _Save(Controller *ctl);
    bool _SaveAs(Controller *ctl);
    void _RenameDiagram();
    void _ExportMedia();
    void _ShowLibrary(const QString &item_id = {});
    void _ShowPlugins();
    void _ApplyAnimation(const QString &template_id = {});
    void _ToggleMcp(bool on);
    void _ChangeMcpPort();
    void _ShowMcpHelp();
    void _About();

    AppContext      &_ctx;
    QTabWidget      *_doc_tabs = nullptr;
    std::vector<Tab> _tabs;

    PaletteWidget  *_palette         = nullptr;
    QStackedWidget *_inspector_stack = nullptr;
    QStackedWidget *_timeline_stack  = nullptr;
    QDockWidget    *_palette_dock    = nullptr;
    QDockWidget    *_inspector_dock  = nullptr;
    QDockWidget    *_timeline_dock   = nullptr;
    QDockWidget    *_exports_dock    = nullptr;
    QToolBar       *_tools_bar       = nullptr;
    QByteArray      _default_state;
    QString         _pending_type = QStringLiteral("service");

    QAction      *_undo_act     = nullptr;
    QAction      *_redo_act     = nullptr;
    QAction      *_tool_acts[3] = {};
    QAction      *_mcp_act      = nullptr;
    QAction      *_lock_act     = nullptr;
    QLabel       *_mcp_label    = nullptr;
    QToolButton  *_jobs_button  = nullptr;
    QProgressBar *_jobs_bar     = nullptr;
    bool          _fit_on_show  = true;
    bool          _closing      = false;
    bool          _restoring    = false; // RestoreSession() is adding tabs: do not overwrite the saved session

    QPointer<LibraryDialog> _library_dialog;
    QPointer<PluginsDialog> _plugins_dialog;
};

} // namespace ad::ui

#endif // _UI_MAIN_WINDOW_HPP_
