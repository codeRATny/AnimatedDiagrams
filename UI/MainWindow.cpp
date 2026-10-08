#include "MainWindow.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <cmath>
#include <map>

#include "ApplyTemplateDialog.hpp"
#include "CanvasWidget.hpp"
#include "Controller.hpp"
#include "ExportDialog.hpp"
#include "Fields.hpp"
#include "Import/DrawioImporter.hpp"
#include "Inspector.hpp"
#include "LibraryDialog.hpp"
#include "PluginsDialog.hpp"
#include "QtRender.hpp"
#include "Timeline/TimelineLayout.hpp"
#include "TimelineWidget.hpp"
#include "Utils/File.hpp"

namespace ad::ui
{

namespace
{

constexpr auto    kLastDirKey     = "files/lastDir";
constexpr auto    kGeometryKey    = "window/geometry";
constexpr auto    kStateKey       = "window/splitters";
constexpr auto    kMcpPortKey     = "mcp/port";
constexpr auto    kMcpAutostart   = "mcp/autostart";
constexpr quint16 kDefaultMcpPort = 8765;

QLabel *PanelTitle(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("panelTitle"));
    return l;
}

} // namespace

MainWindow::MainWindow(Controller &ctl, QWidget *parent) : QMainWindow(parent), _ctl(ctl)
{
    _canvas    = new CanvasWidget(_ctl);
    _timeline  = new TimelineWidget(_ctl);
    _inspector = new Inspector(_ctl);
    connect(_inspector, &Inspector::LibraryRequested, this,
            [this](const QString &id)
            {
                _ShowLibrary(id);
            });

    auto *top = new QSplitter(Qt::Horizontal);
    top->addWidget(_BuildToolPanel());
    top->addWidget(_canvas);
    top->addWidget(_inspector);
    top->setStretchFactor(1, 1);
    top->setSizes({190, 860, 360});
    top->setChildrenCollapsible(false);

    auto *bottom = new QWidget;
    auto *bl     = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);
    bl->addWidget(_BuildTransport());
    bl->addWidget(_timeline, 1);

    auto *root = new QSplitter(Qt::Vertical);
    root->setObjectName(QStringLiteral("rootSplitter"));
    root->addWidget(top);
    root->addWidget(bottom);
    root->setStretchFactor(0, 1);
    root->setSizes({620, 260});
    root->setChildrenCollapsible(false);
    setCentralWidget(root);

    _BuildMenus();

    _mcp_label = new QLabel;
    _mcp_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(_mcp_label);

    connect(&_ctl, &Controller::DocumentStateChanged, this, &MainWindow::_UpdateTitle);
    connect(&_ctl, &Controller::TimeChanged, this, &MainWindow::_UpdateTransport);
    connect(&_ctl, &Controller::ModelChanged, this,
            [this](bool structural)
            {
                _UpdateTransport();
                if (structural)
                {
                    _RebuildPalette(); // no-op unless the document element types changed
                }
            });
    connect(&_ctl, &Controller::PlayingChanged, this, &MainWindow::_UpdateTransport);
    connect(&_ctl, &Controller::LibraryChanged, this,
            [this]
            {
                _palette_cache.clear();
                _RebuildPalette();
            });
    connect(&_ctl, &Controller::McpStateChanged, this, &MainWindow::_UpdateMcpState);
    connect(&_ctl, &Controller::McpActivity, this,
            [this](const QString &text)
            {
                statusBar()->showMessage(text, 3000);
            });

    setWindowIcon(QIcon(QStringLiteral(":/icons/animated-diagrams.png")));
    resize(1440, 900);
    const QSettings s;
    restoreGeometry(s.value(kGeometryKey).toByteArray());
    root->restoreState(s.value(kStateKey).toByteArray());

    _canvas->SetTool(CanvasWidget::Tool::Select);
    _RebuildPalette();
    _UpdateTitle();
    _UpdateTransport();
    _UpdateMcpState();
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------

QWidget *MainWindow::_BuildToolPanel()
{
    auto *panel = new QWidget;
    panel->setMinimumWidth(170);
    panel->setMaximumWidth(260);
    auto *l = new QVBoxLayout(panel);
    l->setContentsMargins(10, 10, 10, 10);
    l->setSpacing(6);

    l->addWidget(PanelTitle(tr("ИНСТРУМЕНТЫ")));
    auto                                                 *group = new QActionGroup(this);
    const std::array<std::pair<QString, QKeySequence>, 3> tools{{
        {tr("▘ Выбор"), QKeySequence(Qt::Key_V)},
        {tr("＋ Узел"), QKeySequence(Qt::Key_N)},
        {tr("↗ Связь"), QKeySequence(Qt::Key_E)},
    }};
    for (size_t i = 0; i < tools.size(); ++i)
    {
        auto *a = new QAction(tools[i].first, this);
        a->setCheckable(true);
        a->setShortcut(tools[i].second);
        a->setToolTip(QStringLiteral("%1 (%2)").arg(a->text(), a->shortcut().toString()));
        group->addAction(a);
        addAction(a);
        connect(a, &QAction::triggered, this,
                [this, i]
                {
                    _canvas->SetTool(static_cast<CanvasWidget::Tool>(i));
                });
        auto *b = new QToolButton;
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        l->addWidget(b);
        _tool_acts[i] = a;
    }
    connect(_canvas, &CanvasWidget::ToolChanged, this,
            [this](CanvasWidget::Tool t)
            {
                _tool_acts[static_cast<int>(t)]->setChecked(true);
            });

    l->addSpacing(8);
    auto *title_row = new QHBoxLayout;
    title_row->addWidget(PanelTitle(tr("ЭЛЕМЕНТЫ")), 1);
    auto *lib_btn = new QToolButton;
    lib_btn->setText(QStringLiteral("…"));
    lib_btn->setToolTip(tr("Библиотека элементов, эффектов и анимаций"));
    connect(lib_btn, &QToolButton::clicked, this,
            [this]
            {
                _ShowLibrary();
            });
    title_row->addWidget(lib_btn);
    l->addLayout(title_row);

    auto *palette = new QWidget;
    _palette_box  = new QVBoxLayout(palette);
    _palette_box->setContentsMargins(0, 0, 4, 0);
    _palette_box->setSpacing(4);
    auto *scroll = new QScrollArea;
    scroll->setWidget(palette);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    l->addWidget(scroll, 1);
    _palette_group = new QButtonGroup(this);

    auto *h = new QLabel(tr("Выберите тип и кликните по холсту."));
    h->setObjectName(QStringLiteral("hint"));
    h->setWordWrap(true);
    l->addWidget(h);
    return panel;
}

void MainWindow::_RebuildPalette()
{
    const auto               entries = _ctl.Reg().Elements(&_ctl.GetModel().library);
    std::vector<ElementType> defs;
    defs.reserve(entries.size());
    for (const auto &e : entries)
    {
        defs.push_back(*e.def);
    }
    if (defs == _palette_cache && _palette_box->count() > 0)
    {
        return;
    }
    _palette_cache = defs;

    while (QLayoutItem *item = _palette_box->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    for (QAbstractButton *b : _palette_group->buttons())
    {
        _palette_group->removeButton(b);
    }

    // group by category keeping the registry order
    std::vector<std::string>                                categories;
    std::map<std::string, std::vector<const ElementType *>> by_category;
    for (const auto &d : _palette_cache)
    {
        if (!by_category.contains(d.category))
        {
            categories.push_back(d.category);
        }
        by_category[d.category].push_back(&d);
    }
    for (const auto &cat : categories)
    {
        auto *cap = new QLabel(Qs(cat));
        cap->setObjectName(QStringLiteral("fieldLabel"));
        _palette_box->addWidget(cap);
        for (const ElementType *d : by_category[cat])
        {
            auto *b = new QPushButton(Qs(d->icon) + QLatin1Char(' ') + Qs(d->label));
            b->setCheckable(true);
            b->setToolTip(d->description.empty() ? Qs(d->id) : Qs(d->description));
            b->setStyleSheet(QStringLiteral("text-align: left; border-left: 3px solid %1;").arg(Qs(d->accent)));
            _palette_group->addButton(b);
            _palette_box->addWidget(b);
            if (d->id == _canvas->PendingType())
            {
                b->setChecked(true);
            }
            connect(b, &QPushButton::clicked, this,
                    [this, id = d->id]
                    {
                        _canvas->SetPendingType(id);
                        _canvas->SetTool(CanvasWidget::Tool::Node);
                    });
        }
    }
    _palette_box->addStretch(1);
}

QWidget *MainWindow::_BuildTransport()
{
    auto *bar = new QWidget;
    auto *l   = new QHBoxLayout(bar);
    l->setContentsMargins(8, 6, 8, 6);
    l->setSpacing(6);

    auto mk = [&](QStyle::StandardPixmap icon, const QString &tip, const std::function<void()> &slot)
    {
        auto *b = new QPushButton(style()->standardIcon(icon), QString());
        b->setToolTip(tip);
        b->setFixedWidth(40);
        connect(b, &QPushButton::clicked, this, slot);
        l->addWidget(b);
        return b;
    };
    mk(QStyle::SP_MediaSkipBackward, tr("В начало (Home)"),
       [this]
       {
           _ctl.Seek(0);
       });
    _play_btn = mk(QStyle::SP_MediaPlay, tr("Играть / Пауза (Пробел)"),
                   [this]
                   {
                       _ctl.TogglePlay();
                   });
    _play_btn->setObjectName(QStringLiteral("primaryButton"));
    mk(QStyle::SP_MediaStop, tr("Стоп"),
       [this]
       {
           _ctl.Stop();
       });
    mk(QStyle::SP_MediaSkipForward, tr("В конец (End)"),
       [this]
       {
           _ctl.Seek(_ctl.Duration());
       });

    _readout = new QLabel;
    _readout->setObjectName(QStringLiteral("readout"));
    l->addWidget(_readout);

    l->addWidget(new QLabel(tr("Скорость")));
    _speed = new QComboBox;
    for (const double s : {0.25, 0.5, 1.0, 2.0, 4.0})
    {
        _speed->addItem(QStringLiteral("%1×").arg(s), s);
    }
    _speed->setCurrentIndex(2);
    connect(_speed, &QComboBox::currentIndexChanged, this,
            [this]
            {
                _ctl.SetSpeed(_speed->currentData().toDouble());
            });
    l->addWidget(_speed);

    l->addWidget(new QLabel(tr("Длит., с")));
    _duration = new QDoubleSpinBox;
    _duration->setRange(1, 3600);
    _duration->setDecimals(1);
    _duration->setSingleStep(0.5);
    _duration->setKeyboardTracking(false);
    connect(_duration, &QDoubleSpinBox::valueChanged, this,
            [this](double v)
            {
                if (std::abs((v * 1000) - _ctl.Duration()) < 1)
                {
                    return;
                }
                _ctl.Doc().SetDuration(v * 1000);
                _ctl.Changed(false);
            });
    l->addWidget(_duration);

    _loop = new QCheckBox(tr("Повтор"));
    _loop->setChecked(_ctl.Loop());
    connect(_loop, &QCheckBox::toggled, this,
            [this](bool v)
            {
                _ctl.SetLoop(v);
            });
    l->addWidget(_loop);

    l->addStretch(1);
    _step_type = new QComboBox;
    for (const auto &t : StepTypes())
    {
        _step_type->addItem(Qs(t.label), static_cast<int>(t.type));
    }
    l->addWidget(_step_type);
    auto *add = new QPushButton(tr("＋ Добавить шаг"));
    add->setObjectName(QStringLiteral("primaryButton"));
    connect(add, &QPushButton::clicked, this, &MainWindow::_AddStep);
    l->addWidget(add);
    auto *anim = new QPushButton(tr("▶ Анимация…"));
    anim->setToolTip(tr("Вставить готовую анимацию (шаблон из библиотеки)"));
    connect(anim, &QPushButton::clicked, this,
            [this]
            {
                _ApplyAnimation();
            });
    l->addWidget(anim);
    return bar;
}

void MainWindow::_BuildMenus()
{
    auto act = [this](QMenu *menu, const QString &text, const QKeySequence &key, const std::function<void()> &slot)
    {
        QAction *a = menu->addAction(text);
        if (!key.isEmpty())
        {
            a->setShortcut(key);
        }
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    QMenu *file = menuBar()->addMenu(tr("&Файл"));
    act(file, tr("Новая диаграмма"), QKeySequence::New,
        [this]
        {
            _NewDiagram();
        });
    act(file, tr("Открыть…"), QKeySequence::Open,
        [this]
        {
            _OpenDiagram();
        });
    act(file, tr("Импорт из draw.io…"), QKeySequence(Qt::CTRL | Qt::Key_I),
        [this]
        {
            _ImportDrawio();
        });
    act(file, tr("Сохранить"), QKeySequence::Save,
        [this]
        {
            _Save();
        });
    act(file, tr("Сохранить как…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
        [this]
        {
            _SaveAs();
        });
    file->addSeparator();
    act(file, tr("Загрузить пример"), {},
        [this]
        {
            if (!_ConfirmDiscard())
            {
                return;
            }
            _ctl.LoadSample();
            _canvas->FitView();
        });
    act(file, tr("Переименовать…"), {},
        [this]
        {
            _RenameDiagram();
        });
    file->addSeparator();
    act(file, tr("Экспорт GIF / видео…"), QKeySequence(Qt::CTRL | Qt::Key_E),
        [this]
        {
            _ExportMedia();
        });
    file->addSeparator();
    act(file, tr("Выход"), QKeySequence(Qt::CTRL | Qt::Key_Q),
        [this]
        {
            close();
        });

    QMenu *edit = menuBar()->addMenu(tr("&Правка"));
    _undo_act   = act(edit, tr("Отменить"), QKeySequence::Undo,
                      [this]
                      {
                        _ctl.Undo();
                    });
    _redo_act   = act(edit, tr("Повторить"), QKeySequence::Redo,
                      [this]
                      {
                        _ctl.Redo();
                    });
    edit->addSeparator();
    QAction *del = act(edit, tr("Удалить выделенное"), QKeySequence::Delete,
                       [this]
                       {
                           _ctl.DeleteSelection();
                       });
    del->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    act(edit, tr("Дублировать шаг"), QKeySequence(Qt::CTRL | Qt::Key_D),
        [this]
        {
            if (_ctl.GetSelection().kind != Selection::Kind::Step)
            {
                return;
            }
            if (const Step *s = _ctl.Doc().DuplicateStep(_ctl.GetSelection().id); s != nullptr)
            {
                const std::string id = s->id;
                _ctl.Changed(true);
                _ctl.Select(Selection::Kind::Step, id);
            }
        });
    act(edit, tr("Снять выделение"), QKeySequence(Qt::Key_Escape),
        [this]
        {
            _canvas->CancelInteraction();
            _ctl.ClearSelection();
        });

    QMenu *view = menuBar()->addMenu(tr("&Вид"));
    act(view, tr("Вписать в экран"), QKeySequence(Qt::CTRL | Qt::Key_0),
        [this]
        {
            _canvas->FitView();
        });
    act(view, tr("Сбросить масштаб"), QKeySequence(Qt::CTRL | Qt::Key_1),
        [this]
        {
            _canvas->ResetView();
        });
    act(view, tr("Увеличить"), QKeySequence::ZoomIn,
        [this]
        {
            _canvas->ZoomBy(1.2);
        });
    act(view, tr("Уменьшить"), QKeySequence::ZoomOut,
        [this]
        {
            _canvas->ZoomBy(1 / 1.2);
        });
    view->addSeparator();
    act(view, tr("Полный экран"), QKeySequence(Qt::Key_F11),
        [this]
        {
            isFullScreen() ? showNormal() : showFullScreen();
        });

    QMenu *play = menuBar()->addMenu(tr("&Воспроизведение"));
    act(play, tr("Играть / Пауза"), QKeySequence(Qt::Key_Space),
        [this]
        {
            _ctl.TogglePlay();
        });
    act(play, tr("Стоп"), {},
        [this]
        {
            _ctl.Stop();
        });
    act(play, tr("В начало"), QKeySequence(Qt::Key_Home),
        [this]
        {
            _ctl.Seek(0);
        });
    act(play, tr("В конец"), QKeySequence(Qt::Key_End),
        [this]
        {
            _ctl.Seek(_ctl.Duration());
        });

    QMenu *lib = menuBar()->addMenu(tr("&Библиотека"));
    act(lib, tr("Элементы, эффекты и анимации…"), QKeySequence(Qt::CTRL | Qt::Key_L),
        [this]
        {
            _ShowLibrary();
        });
    act(lib, tr("Применить анимацию…"), QKeySequence(Qt::CTRL | Qt::Key_T),
        [this]
        {
            _ApplyAnimation();
        });
    lib->addSeparator();
    act(lib, tr("Плагины…"), {},
        [this]
        {
            _ShowPlugins();
        });

    QMenu *tools = menuBar()->addMenu(tr("&Инструменты"));
    _mcp_act     = tools->addAction(tr("MCP-сервер для агентов"));
    _mcp_act->setCheckable(true);
    connect(_mcp_act, &QAction::toggled, this, &MainWindow::_ToggleMcp);
    act(tools, tr("Порт MCP-сервера…"), {},
        [this]
        {
            _ChangeMcpPort();
        });
    QAction *autostart = tools->addAction(tr("Запускать MCP-сервер при старте"));
    autostart->setCheckable(true);
    autostart->setChecked(QSettings().value(kMcpAutostart, false).toBool());
    connect(autostart, &QAction::toggled, this,
            [](bool on)
            {
                QSettings().setValue(kMcpAutostart, on);
            });
    act(tools, tr("Скопировать адрес MCP"), {},
        [this]
        {
            if (_ctl.IsMcpRunning())
            {
                QApplication::clipboard()->setText(_ctl.McpUrl());
                statusBar()->showMessage(tr("Скопировано: %1").arg(_ctl.McpUrl()), 3000);
            }
        });
    act(tools, tr("Как подключить агента…"), {},
        [this]
        {
            _ShowMcpHelp();
        });

    QMenu *help = menuBar()->addMenu(tr("&Справка"));
    act(help, tr("О программе"), {},
        [this]
        {
            _About();
        });
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void MainWindow::_UpdateTitle()
{
    const QString name = Qs(_ctl.GetModel().meta.name);
    const QString file = _ctl.FilePath().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(QFileInfo(_ctl.FilePath()).fileName());
    setWindowTitle(QStringLiteral("%1%2[*] — Animated Diagrams").arg(name, file));
    setWindowModified(_ctl.IsModified());
    _undo_act->setEnabled(_ctl.Doc().CanUndo());
    _redo_act->setEnabled(_ctl.Doc().CanRedo());
}

void MainWindow::_UpdateTransport()
{
    _readout->setText(Qs(FormatTime(_ctl.Time())) + QStringLiteral(" / ") + Qs(FormatTime(_ctl.Duration())));
    _play_btn->setIcon(style()->standardIcon(_ctl.IsPlaying() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    if (!_duration->hasFocus())
    {
        const QSignalBlocker block(_duration);
        _duration->setValue(_ctl.Duration() / 1000);
    }
}

void MainWindow::_UpdateMcpState()
{
    const bool running = _ctl.IsMcpRunning();
    {
        const QSignalBlocker block(_mcp_act);
        _mcp_act->setChecked(running);
    }
    _mcp_label->setText(running ? tr("MCP: %1").arg(_ctl.McpUrl()) : tr("MCP: выкл"));
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    if (_fit_on_show)
    {
        _fit_on_show = false;
        QTimer::singleShot(0, _canvas,
                           [this]
                           {
                               _canvas->FitView();
                           });
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    // unsaved changes are not lost: the session is restored from the autosave
    _ctl.Pause();
    QSettings s;
    s.setValue(kGeometryKey, saveGeometry());
    if (auto *root = findChild<QSplitter *>(QStringLiteral("rootSplitter")); root != nullptr)
    {
        s.setValue(kStateKey, root->saveState());
    }
    e->accept();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool MainWindow::_ConfirmDiscard()
{
    if (!_ctl.IsModified())
    {
        return true;
    }
    const auto r =
        QMessageBox::question(this, tr("Несохранённые изменения"), tr("Сохранить изменения в «%1»?").arg(Qs(_ctl.GetModel().meta.name)),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save)
    {
        return _Save();
    }
    return r == QMessageBox::Discard;
}

void MainWindow::_NewDiagram()
{
    if (!_ConfirmDiscard())
    {
        return;
    }
    _ctl.NewDocument();
    _canvas->ResetView();
}

bool MainWindow::OpenPath(const QString &path)
{
    QString err;
    if (!_ctl.OpenFile(path, &err))
    {
        QMessageBox::warning(this, tr("Ошибка открытия"), tr("Не удалось открыть %1:\n%2").arg(path, err));
        return false;
    }
    QSettings().setValue(kLastDirKey, QFileInfo(path).absolutePath());
    _canvas->FitView();
    statusBar()->showMessage(tr("Открыто: %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

void MainWindow::_OpenDiagram()
{
    if (!_ConfirmDiscard())
    {
        return;
    }
    const QString dir = QSettings().value(kLastDirKey, QDir::homePath()).toString();
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Открыть диаграмму"), dir, tr("Диаграммы (*.json);;draw.io (*.drawio *.xml)"));
    if (path.isEmpty())
    {
        return;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("drawio") || suffix == QStringLiteral("xml"))
    {
        ImportDrawioPath(path);
    }
    else
    {
        OpenPath(path);
    }
}

void MainWindow::_ImportDrawio()
{
    if (!_ConfirmDiscard())
    {
        return;
    }
    const QString dir  = QSettings().value(kLastDirKey, QDir::homePath()).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Импорт из draw.io"), dir, tr("draw.io (*.drawio *.xml);;Все файлы (*)"));
    if (!path.isEmpty())
    {
        ImportDrawioPath(path);
    }
}

bool MainWindow::ImportDrawioPath(const QString &path)
{
    std::vector<std::string> pages;
    try
    {
        pages = DrawioPageNames(ReadFile(PathFromUtf8(Us(path))));
    }
    catch (const std::exception &ex)
    {
        QMessageBox::warning(this, tr("Импорт draw.io"), QString::fromUtf8(ex.what()));
        return false;
    }

    // options: page (for multi-page files) and colors
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Импорт draw.io"));
    auto *page_box = new QComboBox;
    for (size_t i = 0; i < pages.size(); ++i)
    {
        page_box->addItem(pages[i].empty() ? tr("Страница %1").arg(i + 1) : Qs(pages[i]));
    }
    auto *keep_colors = new QCheckBox(tr("Сохранить цвета из draw.io"));
    keep_colors->setChecked(true);
    auto *form = new QFormLayout(&dlg);
    form->addRow(new QLabel(QDir::toNativeSeparators(path)));
    if (pages.size() > 1)
    {
        form->addRow(tr("Страница"), page_box);
    }
    form->addRow(keep_colors);
    auto *note = new QLabel(tr("Фигуры станут узлами (тип подбирается по форме), соединители — связями, "
                               "свободный текст — заметками. Сценарий анимации добавляется после импорта."));
    note->setObjectName(QStringLiteral("hint"));
    note->setWordWrap(true);
    form->addRow(note);
    auto *buttons = fields::OkCancelButtons(&dlg);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted)
    {
        return false;
    }

    QString err;
    QString report;
    if (!_ctl.ImportDrawio(path, std::max(0, page_box->currentIndex()), keep_colors->isChecked(), &err, &report))
    {
        QMessageBox::warning(this, tr("Импорт draw.io"), tr("Не удалось импортировать %1:\n%2").arg(path, err));
        return false;
    }
    QSettings().setValue(kLastDirKey, QFileInfo(path).absolutePath());
    _canvas->FitView();
    statusBar()->showMessage(report, 8000);
    return true;
}

bool MainWindow::_Save()
{
    if (_ctl.FilePath().isEmpty())
    {
        return _SaveAs();
    }
    QString err;
    if (!_ctl.SaveFile(_ctl.FilePath(), &err))
    {
        QMessageBox::warning(this, tr("Ошибка сохранения"), err);
        return false;
    }
    statusBar()->showMessage(tr("Сохранено: %1").arg(QDir::toNativeSeparators(_ctl.FilePath())), 4000);
    return true;
}

bool MainWindow::_SaveAs()
{
    static const QRegularExpression kBad(QStringLiteral(R"([\\/:*?"<>|\s]+)"));
    const QString                   dir  = QSettings().value(kLastDirKey, QDir::homePath()).toString();
    const QString                   name = Qs(_ctl.GetModel().meta.name).replace(kBad, QStringLiteral("_"));
    QString path = QFileDialog::getSaveFileName(this, tr("Сохранить диаграмму"), dir + QLatin1Char('/') + name + QStringLiteral(".json"),
                                                tr("Диаграммы (*.json)"));
    if (path.isEmpty())
    {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty())
    {
        path += QStringLiteral(".json");
    }
    QSettings().setValue(kLastDirKey, QFileInfo(path).absolutePath());
    QString err;
    if (!_ctl.SaveFile(path, &err))
    {
        QMessageBox::warning(this, tr("Ошибка сохранения"), err);
        return false;
    }
    statusBar()->showMessage(tr("Сохранено: %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

void MainWindow::_RenameDiagram()
{
    bool          ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Переименовать"), tr("Название диаграммы:"), QLineEdit::Normal, Qs(_ctl.GetModel().meta.name), &ok);
    if (ok)
    {
        _ctl.Rename(name);
    }
}

void MainWindow::_ExportMedia()
{
    _ctl.Pause();
    ExportDialog dlg(_ctl, _canvas->VisibleWorldRect(), this);
    dlg.exec();
}

void MainWindow::_AddStep()
{
    const auto type = static_cast<StepType>(_step_type->currentData().toInt());
    auto       step = _ctl.Doc().MakeDefaultStep(type, _ctl.Time());
    if (!step.has_value())
    {
        statusBar()->showMessage(
            type == StepType::Link ? tr("Сначала добавьте связь между узлами") : tr("Сначала добавьте узлы на диаграмму"), 4000);
        return;
    }
    const std::string id = _ctl.Doc().AddStep(std::move(*step)).id;
    _ctl.Changed(true);
    _ctl.Select(Selection::Kind::Step, id);
}

void MainWindow::_ShowLibrary(const QString &item_id)
{
    if (_library_dialog == nullptr)
    {
        _library_dialog = new LibraryDialog(_ctl, this);
        _library_dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(_library_dialog, &LibraryDialog::ApplyAnimationRequested, this, &MainWindow::_ApplyAnimation);
    }
    if (!item_id.isEmpty())
    {
        _library_dialog->SelectItem(item_id);
    }
    _library_dialog->show();
    _library_dialog->raise();
    _library_dialog->activateWindow();
}

void MainWindow::_ShowPlugins()
{
    if (_plugins_dialog == nullptr)
    {
        _plugins_dialog = new PluginsDialog(_ctl, this);
        _plugins_dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    _plugins_dialog->show();
    _plugins_dialog->raise();
    _plugins_dialog->activateWindow();
}

void MainWindow::_ApplyAnimation(const QString &template_id)
{
    if (_ctl.GetModel().nodes.empty())
    {
        statusBar()->showMessage(tr("Сначала добавьте узлы на диаграмму"), 4000);
        return;
    }
    _ctl.Pause();
    ApplyTemplateDialog dlg(_ctl, template_id,
                            _library_dialog != nullptr && _library_dialog->isVisible() ? static_cast<QWidget *>(_library_dialog) : this);
    dlg.exec();
}

// ---------------------------------------------------------------------------
// MCP
// ---------------------------------------------------------------------------

void MainWindow::StartMcpOnLaunch(int forced_port)
{
    const QSettings s;
    if (forced_port <= 0 && !s.value(kMcpAutostart, false).toBool())
    {
        return;
    }
    const auto port = static_cast<quint16>(forced_port > 0 ? forced_port : s.value(kMcpPortKey, kDefaultMcpPort).toInt());
    QString    err;
    if (!_ctl.StartMcp(port, &err))
    {
        statusBar()->showMessage(tr("MCP-сервер не запущен: %1").arg(err), 8000);
    }
}

void MainWindow::_ToggleMcp(bool on)
{
    if (!on)
    {
        _ctl.StopMcp();
        return;
    }
    const auto port = static_cast<quint16>(QSettings().value(kMcpPortKey, kDefaultMcpPort).toInt());
    QString    err;
    if (!_ctl.StartMcp(port, &err))
    {
        QMessageBox::warning(this, tr("MCP-сервер"), tr("Не удалось открыть порт %1:\n%2").arg(port).arg(err));
        return;
    }
    statusBar()->showMessage(tr("MCP-сервер запущен: %1").arg(_ctl.McpUrl()), 5000);
}

void MainWindow::_ChangeMcpPort()
{
    bool      ok   = false;
    const int port = QInputDialog::getInt(this, tr("Порт MCP-сервера"), tr("Порт (сервер слушает только 127.0.0.1):"),
                                          QSettings().value(kMcpPortKey, kDefaultMcpPort).toInt(), 1024, 65535, 1, &ok);
    if (!ok)
    {
        return;
    }
    QSettings().setValue(kMcpPortKey, port);
    if (_ctl.IsMcpRunning())
    {
        _ctl.StopMcp();
        _ToggleMcp(true);
    }
}

void MainWindow::_ShowMcpHelp()
{
    const int     port = QSettings().value(kMcpPortKey, kDefaultMcpPort).toInt();
    const QString url  = QStringLiteral("http://127.0.0.1:%1/mcp").arg(port);
    QMessageBox   box(this);
    box.setWindowTitle(tr("Подключение агента"));
    box.setTextFormat(Qt::RichText);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.setText(tr("<b>Вариант 1 — к открытому окну (HTTP)</b><br>"
                   "Включите «Инструменты → MCP-сервер» и добавьте сервер в агент:<br>"
                   "<code>claude mcp add --transport http animated-diagrams %1</code><br><br>"
                   "<b>Вариант 2 — без окна (stdio)</b><br>"
                   "Агент сам запускает программу в режиме сервера:<br>"
                   "<code>claude mcp add animated-diagrams -- animated-diagrams --mcp</code><br><br>"
                   "Сервер даёт инструменты для чтения и правки диаграммы, библиотеки, импорта draw.io, "
                   "рендера кадров и экспорта. Изменения через HTTP сразу видны в окне и отменяются Ctrl+Z.")
                    .arg(url));
    box.exec();
}

void MainWindow::_About()
{
    QMessageBox::about(this, tr("О программе"),
                       tr("<b>Animated Diagrams %1</b><br>Редактор анимированных диаграмм и flow-сценариев: "
                          "сообщения, таймеры, состояния, эффекты, шаблоны анимаций и плагины; импорт draw.io; "
                          "встроенный MCP-сервер; экспорт в GIF / WebM / MP4.<br><br>"
                          "Qt %2 · C++23")
                           .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

} // namespace ad::ui
