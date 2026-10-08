#ifndef _UI_MAIN_WINDOW_HPP_
#define _UI_MAIN_WINDOW_HPP_

#include <QMainWindow>
#include <QPointer>

#include <vector>

#include "Model/Library.hpp"

class QAction;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

/// @file MainWindow.hpp
/// @brief Main window: element palette, canvas, inspector, transport + timeline, menus.

namespace ad::ui
{

class CanvasWidget;
class Controller;
class Inspector;
class LibraryDialog;
class PluginsDialog;
class TimelineWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Controller &ctl, QWidget *parent = nullptr);

    /// Open a file (from the command line); false and a message box on error.
    bool OpenPath(const QString &path);
    /// Import a draw.io file (asks for the page when there are several).
    bool ImportDrawioPath(const QString &path);
    /// Start the MCP server if enabled in the settings (or forced by the command line).
    void StartMcpOnLaunch(int forced_port);

protected:
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    QWidget *_BuildToolPanel();
    QWidget *_BuildTransport();
    void     _BuildMenus();
    void     _RebuildPalette();
    void     _UpdateTitle();
    void     _UpdateTransport();
    void     _UpdateMcpState();

    bool _ConfirmDiscard();
    void _NewDiagram();
    void _OpenDiagram();
    void _ImportDrawio();
    bool _Save();
    bool _SaveAs();
    void _RenameDiagram();
    void _ExportMedia();
    void _AddStep();
    void _ShowLibrary(const QString &item_id = {});
    void _ShowPlugins();
    void _ApplyAnimation(const QString &template_id = {});
    void _ToggleMcp(bool on);
    void _ChangeMcpPort();
    void _ShowMcpHelp();
    void _About();

    Controller     &_ctl;
    CanvasWidget   *_canvas    = nullptr;
    TimelineWidget *_timeline  = nullptr;
    Inspector      *_inspector = nullptr;

    QVBoxLayout             *_palette_box   = nullptr;
    QButtonGroup            *_palette_group = nullptr;
    std::vector<ElementType> _palette_cache;

    QAction        *_undo_act     = nullptr;
    QAction        *_redo_act     = nullptr;
    QAction        *_tool_acts[3] = {};
    QAction        *_mcp_act      = nullptr;
    QPushButton    *_play_btn     = nullptr;
    QLabel         *_readout      = nullptr;
    QLabel         *_mcp_label    = nullptr;
    QDoubleSpinBox *_duration     = nullptr;
    QComboBox      *_speed        = nullptr;
    QCheckBox      *_loop         = nullptr;
    QComboBox      *_step_type    = nullptr;
    bool            _fit_on_show  = true;

    QPointer<LibraryDialog> _library_dialog;
    QPointer<PluginsDialog> _plugins_dialog;
};

} // namespace ad::ui

#endif // _UI_MAIN_WINDOW_HPP_
