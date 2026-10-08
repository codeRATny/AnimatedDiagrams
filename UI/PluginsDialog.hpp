#ifndef _UI_PLUGINS_DIALOG_HPP_
#define _UI_PLUGINS_DIALOG_HPP_

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;

/// @file PluginsDialog.hpp
/// @brief Installed plugins: enable / disable, install from a file, uninstall, reload.

namespace ad::ui
{

class Controller;

class PluginsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PluginsDialog(Controller &ctl, QWidget *parent = nullptr);

private:
    void _Refresh();
    void _Install();
    void _Uninstall();
    void _UpdateButtons();

    Controller  &_ctl;
    QTreeWidget *_tree             = nullptr;
    QLabel      *_details          = nullptr;
    QLabel      *_errors           = nullptr;
    QPushButton *_uninstall_button = nullptr;
    bool         _filling          = false;
};

} // namespace ad::ui

#endif // _UI_PLUGINS_DIALOG_HPP_
