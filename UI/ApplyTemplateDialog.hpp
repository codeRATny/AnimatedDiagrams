#ifndef _UI_APPLY_TEMPLATE_DIALOG_HPP_
#define _UI_APPLY_TEMPLATE_DIALOG_HPP_

#include <QDialog>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;

/// @file ApplyTemplateDialog.hpp
/// @brief Insert an animation template into the scenario: pick the template, map
///        every role to a node and choose the start time.

namespace ad::ui
{

class Controller;

class ApplyTemplateDialog : public QDialog
{
    Q_OBJECT

public:
    ApplyTemplateDialog(Controller &ctl, const QString &template_id, QWidget *parent = nullptr);

    void accept() override;

private:
    void _RebuildRoles();

    Controller              &_ctl;
    QComboBox               *_template    = nullptr;
    QFormLayout             *_roles_form  = nullptr;
    QDoubleSpinBox          *_start       = nullptr;
    QLabel                  *_description = nullptr;
    std::vector<QComboBox *> _role_boxes;
};

} // namespace ad::ui

#endif // _UI_APPLY_TEMPLATE_DIALOG_HPP_
