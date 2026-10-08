#include "PluginsDialog.hpp"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "Controller.hpp"
#include "QtRender.hpp"
#include "Utils/File.hpp"

namespace ad::ui
{

namespace
{

constexpr int kIdRole = Qt::UserRole;

QString Contents(const LibrarySet &s)
{
    return QObject::tr("элементов: %1 · эффектов: %2 · анимаций: %3").arg(s.elements.size()).arg(s.effects.size()).arg(s.animations.size());
}

} // namespace

PluginsDialog::PluginsDialog(Controller &ctl, QWidget *parent) : QDialog(parent), _ctl(ctl)
{
    setWindowTitle(tr("Плагины"));
    resize(760, 520);

    _tree = new QTreeWidget;
    _tree->setHeaderLabels({tr("Плагин"), tr("Версия"), tr("Содержимое"), tr("Расположение")});
    _tree->setRootIsDecorated(false);
    _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int column)
            {
                if (_filling || column != 0)
                {
                    return;
                }
                _ctl.SetPluginEnabled(Us(item->data(0, kIdRole).toString()), item->checkState(0) == Qt::Checked);
            });
    connect(_tree, &QTreeWidget::currentItemChanged, this, &PluginsDialog::_UpdateButtons);

    _details = new QLabel;
    _details->setWordWrap(true);
    _details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _errors = new QLabel;
    _errors->setWordWrap(true);
    _errors->setStyleSheet(QStringLiteral("color: #fca5a5;"));
    _errors->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *install_button = new QPushButton(tr("Установить из файла…"));
    _uninstall_button    = new QPushButton(tr("Удалить"));
    _uninstall_button->setObjectName(QStringLiteral("dangerButton"));
    auto *folder_button = new QPushButton(tr("Открыть папку"));
    auto *reload_button = new QPushButton(tr("Перечитать"));
    connect(install_button, &QPushButton::clicked, this, &PluginsDialog::_Install);
    connect(_uninstall_button, &QPushButton::clicked, this, &PluginsDialog::_Uninstall);
    connect(folder_button, &QPushButton::clicked, this,
            []
            {
                QDir().mkpath(Controller::UserPluginDir());
                QDesktopServices::openUrl(QUrl::fromLocalFile(Controller::UserPluginDir()));
            });
    connect(reload_button, &QPushButton::clicked, this,
            [this]
            {
                _ctl.ReloadPlugins();
            });

    auto *buttons = new QDialogButtonBox;
    buttons->addButton(new QPushButton(tr("Закрыть")), QDialogButtonBox::RejectRole);
    buttons->addButton(install_button, QDialogButtonBox::ActionRole);
    buttons->addButton(_uninstall_button, QDialogButtonBox::ActionRole);
    buttons->addButton(folder_button, QDialogButtonBox::ActionRole);
    buttons->addButton(reload_button, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *hint = new QLabel(tr("Плагины — наборы элементов, эффектов и анимаций в JSON. Пользовательская папка: %1")
                                .arg(QDir::toNativeSeparators(Controller::UserPluginDir())));
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    hint->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(_tree, 1);
    layout->addWidget(_details);
    layout->addWidget(_errors);
    layout->addWidget(buttons);

    connect(&_ctl, &Controller::LibraryChanged, this, &PluginsDialog::_Refresh);
    _Refresh();
}

void PluginsDialog::_Refresh()
{
    const QString selected = _tree->currentItem() != nullptr ? _tree->currentItem()->data(0, kIdRole).toString() : QString();
    _filling               = true;
    _tree->clear();
    for (const auto &rec : _ctl.Plugins().Plugins())
    {
        auto *item = new QTreeWidgetItem(_tree);
        item->setText(0, Qs(rec.plugin.info.name));
        item->setText(1, Qs(rec.plugin.info.version));
        item->setText(2, Contents(rec.plugin.library));
        item->setText(3, rec.writable ? tr("пользовательский") : tr("поставляется с программой"));
        item->setToolTip(3, Qs(PathToUtf8(rec.path)));
        item->setData(0, kIdRole, Qs(rec.plugin.info.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, rec.enabled ? Qt::Checked : Qt::Unchecked);
        if (item->data(0, kIdRole).toString() == selected)
        {
            _tree->setCurrentItem(item);
        }
    }
    _filling = false;

    QStringList errors;
    for (const auto &e : _ctl.Plugins().Errors())
    {
        errors << QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(Qs(PathToUtf8(e.path))), Qs(e.message));
    }
    _errors->setText(errors.isEmpty() ? QString() : tr("Не удалось загрузить:\n%1").arg(errors.join(QLatin1Char('\n'))));
    _errors->setVisible(!errors.isEmpty());
    _UpdateButtons();
}

void PluginsDialog::_UpdateButtons()
{
    const QTreeWidgetItem *item = _tree->currentItem();
    const PluginRecord    *rec  = item != nullptr ? _ctl.Plugins().Find(Us(item->data(0, kIdRole).toString())) : nullptr;
    _uninstall_button->setEnabled(rec != nullptr && rec->writable);
    if (rec == nullptr)
    {
        _details->setText(tr("Выберите плагин, чтобы увидеть подробности."));
        return;
    }
    QString text = QStringLiteral("<b>%1</b> (%2)").arg(Qs(rec->plugin.info.name).toHtmlEscaped(), Qs(rec->plugin.info.id).toHtmlEscaped());
    if (!rec->plugin.info.author.empty())
    {
        text += tr(" · автор: %1").arg(Qs(rec->plugin.info.author).toHtmlEscaped());
    }
    if (!rec->plugin.info.description.empty())
    {
        text += QStringLiteral("<br>") + Qs(rec->plugin.info.description).toHtmlEscaped();
    }
    text += QStringLiteral("<br><small>%1</small>").arg(QDir::toNativeSeparators(Qs(PathToUtf8(rec->path))).toHtmlEscaped());
    for (const auto &w : rec->warnings)
    {
        text += QStringLiteral("<br><span style='color:#fbbf24'>⚠ %1</span>").arg(Qs(w).toHtmlEscaped());
    }
    _details->setText(text);
}

void PluginsDialog::_Install()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Установить плагин"), QDir::homePath(), tr("Плагин (*.json)"));
    if (path.isEmpty())
    {
        return;
    }
    const auto result = _ctl.Plugins().Install(PathFromUtf8(Us(path)));
    if (!result.has_value())
    {
        QMessageBox::warning(this, tr("Установка плагина"), Qs(result.error()));
        return;
    }
    _ctl.ReloadPlugins();
}

void PluginsDialog::_Uninstall()
{
    const QTreeWidgetItem *item = _tree->currentItem();
    if (item == nullptr)
    {
        return;
    }
    const std::string id = Us(item->data(0, kIdRole).toString());
    if (QMessageBox::question(this, tr("Удалить плагин"), tr("Удалить плагин «%1»?").arg(item->text(0))) != QMessageBox::Yes)
    {
        return;
    }
    if (!_ctl.Plugins().Uninstall(id))
    {
        QMessageBox::warning(this, tr("Удалить плагин"), tr("Не удалось удалить плагин."));
        return;
    }
    _ctl.ReloadPlugins();
}

} // namespace ad::ui
