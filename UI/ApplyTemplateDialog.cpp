#include "ApplyTemplateDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include <map>
#include <set>

#include "Common/Exceptions.hpp"
#include "Controller.hpp"
#include "Engine/Templates.hpp"
#include "Fields.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

ApplyTemplateDialog::ApplyTemplateDialog(Controller &ctl, const QString &template_id, QWidget *parent) : QDialog(parent), _ctl(ctl)
{
    setWindowTitle(tr("Применить анимацию"));
    setMinimumWidth(420);

    _template = new QComboBox;
    for (const auto &e : _ctl.Reg().Animations(&_ctl.GetModel().library))
    {
        _template->addItem(Qs(e.def->category) + QStringLiteral(" · ") + Qs(e.def->label), Qs(e.def->id));
    }
    if (const int idx = _template->findData(template_id); idx >= 0)
    {
        _template->setCurrentIndex(idx);
    }
    connect(_template, &QComboBox::currentIndexChanged, this, &ApplyTemplateDialog::_RebuildRoles);

    _description = new QLabel;
    _description->setObjectName(QStringLiteral("hint"));
    _description->setWordWrap(true);

    _start = new QDoubleSpinBox;
    _start->setRange(0, 3'600'000);
    _start->setDecimals(0);
    _start->setSingleStep(100);
    _start->setSuffix(tr(" мс"));
    _start->setValue(_ctl.Time());

    auto *top = new QFormLayout;
    top->addRow(tr("Анимация"), _template);
    top->addRow(QString(), _description);
    top->addRow(tr("Начало"), _start);

    auto *roles = new QGroupBox(tr("Роли → узлы"));
    _roles_form = new QFormLayout(roles);

    auto *buttons = fields::OkCancelButtons(this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(roles);
    layout->addWidget(buttons);
    _RebuildRoles();
}

void ApplyTemplateDialog::_RebuildRoles()
{
    while (_roles_form->rowCount() > 0)
    {
        _roles_form->removeRow(0);
    }
    _role_boxes.clear();
    const auto              &m   = _ctl.GetModel();
    const AnimationTemplate *tpl = _ctl.Reg().FindAnimation(Us(_template->currentData().toString()), &m.library);
    if (tpl == nullptr)
    {
        _description->clear();
        return;
    }
    _description->setText(Qs(tpl->description));

    // default mapping: the selected node first, then nodes whose label matches the role, then in order
    std::set<std::string> used;
    std::string           selected_node;
    if (_ctl.GetSelection().kind == Selection::Kind::Node)
    {
        selected_node = _ctl.GetSelection().id;
    }
    for (size_t i = 0; i < tpl->roles.size(); ++i)
    {
        const auto &role = tpl->roles[i];
        auto       *box  = new QComboBox;
        for (const auto &n : m.nodes)
        {
            box->addItem(Qs(n.label), Qs(n.id));
        }
        std::string pick;
        if (i == 0 && !selected_node.empty())
        {
            pick = selected_node;
        }
        for (const auto &n : m.nodes)
        {
            if (pick.empty() && !used.contains(n.id) && (n.label == role.label || n.label == role.id))
            {
                pick = n.id;
            }
        }
        for (const auto &n : m.nodes)
        {
            if (pick.empty() && !used.contains(n.id))
            {
                pick = n.id;
            }
        }
        used.insert(pick);
        box->setCurrentIndex(std::max(0, box->findData(Qs(pick))));
        _roles_form->addRow(Qs(role.label.empty() ? role.id : role.label), box);
        _role_boxes.push_back(box);
    }
}

void ApplyTemplateDialog::accept()
{
    const auto              &m   = _ctl.GetModel();
    const AnimationTemplate *tpl = _ctl.Reg().FindAnimation(Us(_template->currentData().toString()), &m.library);
    if (tpl == nullptr)
    {
        return;
    }
    std::map<std::string, std::string> roles;
    for (size_t i = 0; i < tpl->roles.size() && i < _role_boxes.size(); ++i)
    {
        roles[tpl->roles[i].id] = Us(_role_boxes[i]->currentData().toString());
    }
    try
    {
        const AnimationTemplate copy = *tpl; // the registry entry may live in the document being modified
        const auto              ids  = ApplyTemplate(_ctl.Doc(), copy, roles, _start->value());
        _ctl.Changed(true);
        if (!ids.empty())
        {
            _ctl.Select(Selection::Kind::Step, ids.front());
        }
        QDialog::accept();
    }
    catch (const InvalidArgument &e)
    {
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
    }
}

} // namespace ad::ui
