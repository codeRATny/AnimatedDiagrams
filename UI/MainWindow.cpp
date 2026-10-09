#include "MainWindow.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QRegularExpression>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <array>

#include "ApplyTemplateDialog.hpp"
#include "CanvasWidget.hpp"
#include "Controller.hpp"
#include "ExportDialog.hpp"
#include "ExportManager.hpp"
#include "ExportsPanel.hpp"
#include "Fields.hpp"
#include "Import/DrawioImporter.hpp"
#include "Inspector.hpp"
#include "LibraryDialog.hpp"
#include "Model/Sample.hpp"
#include "PaletteWidget.hpp"
#include "PluginsDialog.hpp"
#include "QtRender.hpp"
#include "TimelinePanel.hpp"
#include "Utils/File.hpp"

namespace ad::ui
{

namespace
{

constexpr auto    kLastDirKey     = "files/lastDir";
constexpr auto    kGeometryKey    = "window/geometry";
constexpr auto    kStateKey       = "window/dockState";
constexpr auto    kLockedKey      = "window/locked";
constexpr auto    kSessionTabsKey = "session/tabs";
constexpr auto    kSessionCurrent = "session/current";
constexpr auto    kMcpPortKey     = "mcp/port";
constexpr auto    kMcpAutostart   = "mcp/autostart";
constexpr quint16 kDefaultMcpPort = 8765;
constexpr int     kStateVersion   = 3;

bool IsDrawio(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("drawio") || suffix == QStringLiteral("xml");
}

} // namespace

MainWindow::MainWindow(AppContext &ctx, QWidget *parent) : QMainWindow(parent), _ctx(ctx)
{
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    _doc_tabs = new QTabWidget;
    _doc_tabs->setDocumentMode(true);
    _doc_tabs->setTabsClosable(true);
    _doc_tabs->setMovable(true);
    _doc_tabs->setElideMode(Qt::ElideRight);
    auto *plus = new QToolButton;
    plus->setText(QStringLiteral("＋"));
    plus->setToolTip(tr("Новая вкладка (Ctrl+N)"));
    plus->setAutoRaise(true);
    connect(plus, &QToolButton::clicked, this, &MainWindow::_NewDiagram);
    _doc_tabs->setCornerWidget(plus, Qt::TopRightCorner);
    connect(_doc_tabs, &QTabWidget::currentChanged, this, &MainWindow::_OnCurrentTabChanged);
    connect(_doc_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::_CloseTab);
    connect(_doc_tabs->tabBar(), &QTabBar::tabMoved, this, &MainWindow::_SaveSession);
    setCentralWidget(_doc_tabs);

    _BuildDocks();
    _BuildToolBar();
    _BuildMenus();
    _BuildStatusBar();

    connect(&_ctx, &AppContext::McpStateChanged, this, &MainWindow::_UpdateMcpState);
    connect(&_ctx, &AppContext::McpActivity, this,
            [this](const QString &text)
            {
                statusBar()->showMessage(text, 3000);
            });
    _ctx.SetWorkspace(this);

    setWindowIcon(QIcon(QStringLiteral(":/icons/animated-diagrams.png")));
    resize(1440, 900);
    _default_state = saveState(kStateVersion);
    const QSettings s;
    restoreGeometry(s.value(kGeometryKey).toByteArray());
    restoreState(s.value(kStateKey).toByteArray(), kStateVersion);
    _SetLocked(s.value(kLockedKey, false).toBool());
    _UpdateMcpState();
    _UpdateExportsIndicator();
}

MainWindow::~MainWindow()
{
    _closing = true;
    _ctx.SetWorkspace(nullptr);
    _ctx.SetActive(nullptr);
}

// ---------------------------------------------------------------------------
// Docks, tool bar, status bar
// ---------------------------------------------------------------------------

void MainWindow::_BuildDocks()
{
    auto dock = [this](const QString &title, const QString &name, QWidget *content, Qt::DockWidgetArea area)
    {
        auto *d = new QDockWidget(title, this);
        d->setObjectName(name); // required by saveState / restoreState
        d->setWidget(content);
        addDockWidget(area, d);
        return d;
    };

    _palette = new PaletteWidget(_ctx);
    connect(_palette, &PaletteWidget::TypeChosen, this,
            [this](const QString &id)
            {
                _pending_type = id;
                for (const Tab &t : _tabs)
                {
                    t.canvas->SetPendingType(Us(id));
                }
                if (Tab *t = _Current(); t != nullptr)
                {
                    t->canvas->SetTool(CanvasWidget::Tool::Node);
                }
            });
    connect(_palette, &PaletteWidget::LibraryRequested, this,
            [this](const QString &id)
            {
                _ShowLibrary(id);
            });
    _palette->SetCurrentType(_pending_type);
    _palette_dock = dock(tr("Элементы"), QStringLiteral("paletteDock"), _palette, Qt::LeftDockWidgetArea);

    _inspector_stack = new QStackedWidget;
    _inspector_stack->setMinimumWidth(300);
    _inspector_dock = dock(tr("Свойства"), QStringLiteral("inspectorDock"), _inspector_stack, Qt::RightDockWidgetArea);

    _timeline_stack = new QStackedWidget;
    _timeline_dock  = dock(tr("Таймлайн"), QStringLiteral("timelineDock"), _timeline_stack, Qt::BottomDockWidgetArea);

    _exports_dock = dock(tr("Экспорт"), QStringLiteral("exportsDock"), new ExportsPanel(_ctx.Exports()), Qt::RightDockWidgetArea);
    tabifyDockWidget(_inspector_dock, _exports_dock);
    _inspector_dock->raise();

    resizeDocks({_palette_dock, _inspector_dock}, {210, 340}, Qt::Horizontal);
    resizeDocks({_timeline_dock}, {260}, Qt::Vertical);
}

void MainWindow::_BuildToolBar()
{
    _tools_bar = addToolBar(tr("Инструменты"));
    _tools_bar->setObjectName(QStringLiteral("toolsBar"));
    _tools_bar->setToolButtonStyle(Qt::ToolButtonTextOnly);

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
        _tools_bar->addAction(a);
        connect(a, &QAction::triggered, this,
                [this, i]
                {
                    if (Tab *t = _Current(); t != nullptr)
                    {
                        t->canvas->SetTool(static_cast<CanvasWidget::Tool>(i));
                    }
                });
        _tool_acts[i] = a;
    }
    _tool_acts[0]->setChecked(true);
    _tools_bar->addSeparator();
    _tools_bar->addAction(tr("Вписать"),
                          [this]
                          {
                              if (Tab *t = _Current(); t != nullptr)
                              {
                                  t->canvas->FitView();
                              }
                          });
    _tools_bar->addAction(tr("Библиотека"),
                          [this]
                          {
                              _ShowLibrary();
                          });
    _tools_bar->addAction(tr("Экспорт"),
                          [this]
                          {
                              _ExportMedia();
                          });
}

void MainWindow::_BuildStatusBar()
{
    _jobs_bar = new QProgressBar;
    _jobs_bar->setFixedWidth(120);
    _jobs_bar->setFixedHeight(10);
    _jobs_bar->setTextVisible(false);
    _jobs_button = new QToolButton;
    _jobs_button->setAutoRaise(true);
    _jobs_button->setToolTip(tr("Фоновые экспорты"));
    connect(_jobs_button, &QToolButton::clicked, this,
            [this]
            {
                _exports_dock->show();
                _exports_dock->raise();
            });
    statusBar()->addPermanentWidget(_jobs_button);
    statusBar()->addPermanentWidget(_jobs_bar);
    _mcp_label = new QLabel;
    _mcp_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(_mcp_label);

    auto &jobs = _ctx.Exports();
    connect(&jobs, &ExportManager::JobsChanged, this, &MainWindow::_UpdateExportsIndicator);
    connect(&jobs, &ExportManager::JobProgress, this, &MainWindow::_UpdateExportsIndicator);
    connect(&jobs, &ExportManager::JobFinished, this,
            [this](int id)
            {
                const ExportJobInfo *j = _ctx.Exports().Info(id);
                if (j == nullptr)
                {
                    return;
                }
                const QString file = QFileInfo(j->path).fileName();
                switch (j->state)
                {
                case ExportJobInfo::State::Done:
                    statusBar()->showMessage(tr("Экспорт готов: %1 (%2)").arg(file, j->message), 8000);
                    break;
                case ExportJobInfo::State::Failed:
                    statusBar()->showMessage(tr("Экспорт %1 не удался: %2").arg(file, j->message), 10000);
                    _exports_dock->show();
                    _exports_dock->raise();
                    break;
                default:
                    break;
                }
            });
}

void MainWindow::_UpdateExportsIndicator()
{
    const auto jobs    = _ctx.Exports().Jobs();
    int        pending = 0;
    int64_t    done    = 0;
    int64_t    total   = 0;
    for (const auto &j : jobs)
    {
        if (!j.Finished())
        {
            ++pending;
            done += j.done;
            total += std::max(1, j.total);
        }
    }
    _jobs_button->setText(pending > 0 ? tr("⤓ Экспорт: %1").arg(pending) : tr("⤓ Экспорт"));
    _jobs_button->setVisible(!jobs.empty());
    _jobs_bar->setVisible(pending > 0);
    _jobs_bar->setRange(0, static_cast<int>(std::max<int64_t>(1, total)));
    _jobs_bar->setValue(static_cast<int>(done));
}

void MainWindow::_ResetLayout()
{
    restoreState(_default_state, kStateVersion);
    for (QDockWidget *d : {_palette_dock, _inspector_dock, _timeline_dock})
    {
        d->show();
    }
    _tools_bar->show();
    _inspector_dock->raise();
}

void MainWindow::_SetLocked(bool locked)
{
    const auto features = locked ? QDockWidget::NoDockWidgetFeatures
                                 : QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;
    for (QDockWidget *d : {_palette_dock, _inspector_dock, _timeline_dock, _exports_dock})
    {
        d->setFeatures(features);
    }
    _tools_bar->setMovable(!locked);
    if (_lock_act != nullptr)
    {
        const QSignalBlocker block(_lock_act);
        _lock_act->setChecked(locked);
    }
    QSettings().setValue(kLockedKey, locked);
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

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
    auto with_tab = [this](const std::function<void(Tab &)> &fn)
    {
        return [this, fn]
        {
            if (Tab *t = _Current(); t != nullptr)
            {
                fn(*t);
            }
        };
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
            _Save(_Ctl());
        });
    act(file, tr("Сохранить как…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
        [this]
        {
            _SaveAs(_Ctl());
        });
    act(file, tr("Закрыть вкладку"), QKeySequence(Qt::CTRL | Qt::Key_W),
        [this]
        {
            _CloseTab(_doc_tabs->currentIndex());
        });
    file->addSeparator();
    act(file, tr("Открыть пример"), {},
        [this]
        {
            Tab *t = _TargetTab();
            t->ctl->LoadSample();
            t->canvas->FitView();
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
                      with_tab(
                        [](Tab &t)
                        {
                            t.ctl->Undo();
                        }));
    _redo_act   = act(edit, tr("Повторить"), QKeySequence::Redo,
                      with_tab(
                        [](Tab &t)
                        {
                            t.ctl->Redo();
                        }));
    edit->addSeparator();
    QAction *del = act(edit, tr("Удалить выделенное"), QKeySequence::Delete,
                       with_tab(
                           [](Tab &t)
                           {
                               t.ctl->DeleteSelection();
                           }));
    del->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    act(edit, tr("Дублировать шаг"), QKeySequence(Qt::CTRL | Qt::Key_D),
        with_tab(
            [](Tab &t)
            {
                if (t.ctl->GetSelection().kind != Selection::Kind::Step)
                {
                    return;
                }
                if (const Step *s = t.ctl->Doc().DuplicateStep(t.ctl->GetSelection().id); s != nullptr)
                {
                    const std::string id = s->id;
                    t.ctl->Changed(true);
                    t.ctl->Select(Selection::Kind::Step, id);
                }
            }));
    act(edit, tr("Снять выделение"), QKeySequence(Qt::Key_Escape),
        with_tab(
            [](Tab &t)
            {
                t.canvas->CancelInteraction();
                t.ctl->ClearSelection();
            }));

    QMenu *view = menuBar()->addMenu(tr("&Вид"));
    act(view, tr("Вписать в экран"), QKeySequence(Qt::CTRL | Qt::Key_0),
        with_tab(
            [](Tab &t)
            {
                t.canvas->FitView();
            }));
    act(view, tr("Сбросить масштаб"), QKeySequence(Qt::CTRL | Qt::Key_1),
        with_tab(
            [](Tab &t)
            {
                t.canvas->ResetView();
            }));
    act(view, tr("Увеличить"), QKeySequence::ZoomIn,
        with_tab(
            [](Tab &t)
            {
                t.canvas->ZoomBy(1.2);
            }));
    act(view, tr("Уменьшить"), QKeySequence::ZoomOut,
        with_tab(
            [](Tab &t)
            {
                t.canvas->ZoomBy(1 / 1.2);
            }));
    view->addSeparator();
    QMenu *panels = view->addMenu(tr("Панели"));
    for (QDockWidget *d : {_palette_dock, _inspector_dock, _timeline_dock, _exports_dock})
    {
        panels->addAction(d->toggleViewAction());
    }
    panels->addAction(_tools_bar->toggleViewAction());
    _lock_act = view->addAction(tr("Закрепить панели"));
    _lock_act->setCheckable(true);
    connect(_lock_act, &QAction::toggled, this, &MainWindow::_SetLocked);
    act(view, tr("Сбросить расположение панелей"), {},
        [this]
        {
            _ResetLayout();
        });
    view->addSeparator();
    act(view, tr("Следующая вкладка"), QKeySequence(Qt::CTRL | Qt::Key_PageDown),
        [this]
        {
            if (_doc_tabs->count() > 1)
            {
                _doc_tabs->setCurrentIndex((_doc_tabs->currentIndex() + 1) % _doc_tabs->count());
            }
        });
    act(view, tr("Предыдущая вкладка"), QKeySequence(Qt::CTRL | Qt::Key_PageUp),
        [this]
        {
            if (_doc_tabs->count() > 1)
            {
                _doc_tabs->setCurrentIndex((_doc_tabs->currentIndex() + _doc_tabs->count() - 1) % _doc_tabs->count());
            }
        });
    act(view, tr("Полный экран"), QKeySequence(Qt::Key_F11),
        [this]
        {
            isFullScreen() ? showNormal() : showFullScreen();
        });

    QMenu *play = menuBar()->addMenu(tr("&Воспроизведение"));
    act(play, tr("Играть / Пауза"), QKeySequence(Qt::Key_Space),
        with_tab(
            [](Tab &t)
            {
                t.ctl->TogglePlay();
            }));
    act(play, tr("Стоп"), {},
        with_tab(
            [](Tab &t)
            {
                t.ctl->Stop();
            }));
    act(play, tr("В начало"), QKeySequence(Qt::Key_Home),
        with_tab(
            [](Tab &t)
            {
                t.ctl->Seek(0);
            }));
    act(play, tr("В конец"), QKeySequence(Qt::Key_End),
        with_tab(
            [](Tab &t)
            {
                t.ctl->Seek(t.ctl->Duration());
            }));

    QMenu *lib = menuBar()->addMenu(tr("&Библиотека"));
    act(lib, tr("Элементы, эффекты, анимации, дизайн-системы…"), QKeySequence(Qt::CTRL | Qt::Key_L),
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
            if (_ctx.IsMcpRunning())
            {
                QApplication::clipboard()->setText(_ctx.McpUrl());
                statusBar()->showMessage(tr("Скопировано: %1").arg(_ctx.McpUrl()), 3000);
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
// Tabs
// ---------------------------------------------------------------------------

std::vector<Controller *> MainWindow::Documents() const
{
    std::vector<Controller *> out;
    for (int i = 0; i < _doc_tabs->count(); ++i)
    {
        for (const Tab &t : _tabs)
        {
            if (t.canvas == _doc_tabs->widget(i))
            {
                out.push_back(t.ctl);
            }
        }
    }
    return out;
}

MainWindow::Tab *MainWindow::_TabOf(Controller *ctl)
{
    const auto it = std::ranges::find(_tabs, ctl, &Tab::ctl);
    return it != _tabs.end() ? &*it : nullptr;
}

MainWindow::Tab *MainWindow::_Current()
{
    QWidget   *page = _doc_tabs->currentWidget();
    const auto it   = std::ranges::find_if(_tabs,
                                           [page](const Tab &t)
                                           {
                                             return t.canvas == page;
                                         });
    return it != _tabs.end() ? &*it : nullptr;
}

Controller *MainWindow::_Ctl()
{
    Tab *t = _Current();
    return t != nullptr ? t->ctl : nullptr;
}

bool MainWindow::_IsPristine(Controller *ctl) const
{
    const auto &m = ctl->GetModel();
    return !ctl->IsModified() && ctl->FilePath().isEmpty() && m.nodes.empty() && m.scenario.steps.empty() && !ctl->Doc().CanUndo();
}

MainWindow::Tab *MainWindow::_TargetTab()
{
    Tab *t = _Current();
    if (t != nullptr && _IsPristine(t->ctl))
    {
        return t;
    }
    t = _AddTab();
    _doc_tabs->setCurrentWidget(t->canvas);
    return t;
}

MainWindow::Tab *MainWindow::_AddTab()
{
    auto *ctl       = new Controller(_ctx, this);
    auto *canvas    = new CanvasWidget(*ctl);
    auto *inspector = new Inspector(*ctl);
    auto *timeline  = new TimelinePanel(*ctl);
    canvas->SetPendingType(Us(_pending_type));
    _inspector_stack->addWidget(inspector);
    _timeline_stack->addWidget(timeline);

    connect(inspector, &Inspector::LibraryRequested, this,
            [this](const QString &id)
            {
                _ShowLibrary(id);
            });
    connect(timeline, &TimelinePanel::AnimationRequested, this,
            [this]
            {
                _ApplyAnimation();
            });
    connect(timeline, &TimelinePanel::StatusMessage, this,
            [this](const QString &text)
            {
                statusBar()->showMessage(text, 4000);
            });
    connect(canvas, &CanvasWidget::ToolChanged, this,
            [this, canvas](CanvasWidget::Tool tool)
            {
                if (_doc_tabs->currentWidget() == canvas)
                {
                    _tool_acts[static_cast<int>(tool)]->setChecked(true);
                }
            });
    connect(canvas, &CanvasWidget::FilesDropped, this,
            [this](const QStringList &files)
            {
                for (const QString &f : files)
                {
                    OpenPath(f);
                }
            });
    connect(ctl, &Controller::DocumentStateChanged, this,
            [this, ctl]
            {
                _UpdateTabText(ctl);
                if (ctl == _Ctl())
                {
                    _UpdateTitle();
                }
                _SaveSession();
            });

    _tabs.push_back(Tab{ctl, canvas, inspector, timeline});
    _doc_tabs->addTab(canvas, QString());
    _UpdateTabText(ctl);
    return &_tabs.back();
}

Controller *MainWindow::NewDocumentTab()
{
    Tab *t = _AddTab();
    t->ctl->NewDocument();
    _doc_tabs->setCurrentWidget(t->canvas);
    _SaveSession();
    return t->ctl;
}

void MainWindow::Activate(Controller *ctl)
{
    if (Tab *t = _TabOf(ctl); t != nullptr)
    {
        _doc_tabs->setCurrentWidget(t->canvas);
    }
}

void MainWindow::_UpdateTabText(Controller *ctl)
{
    Tab *t = _TabOf(ctl);
    if (t == nullptr)
    {
        return;
    }
    const int index = _doc_tabs->indexOf(t->canvas);
    QString   name  = Qs(ctl->GetModel().meta.name);
    if (name.size() > 32)
    {
        name = name.left(30) + QStringLiteral("…");
    }
    _doc_tabs->setTabText(index, (ctl->IsModified() ? QStringLiteral("● ") : QString()) + name);
    _doc_tabs->setTabToolTip(index, ctl->FilePath().isEmpty() ? tr("Не сохранён") : QDir::toNativeSeparators(ctl->FilePath()));
}

void MainWindow::_OnCurrentTabChanged(int /*index*/)
{
    if (_closing)
    {
        return;
    }
    Tab *t = _Current();
    if (t == nullptr)
    {
        return;
    }
    for (const Tab &other : _tabs)
    {
        if (other.ctl != t->ctl)
        {
            other.ctl->Pause(); // only the visible document plays
        }
    }
    _inspector_stack->setCurrentWidget(t->inspector);
    _timeline_stack->setCurrentWidget(t->timeline);
    _ctx.SetActive(t->ctl);
    _palette->SetDocument(t->ctl);
    if (_library_dialog != nullptr)
    {
        _library_dialog->SetController(t->ctl);
    }
    _tool_acts[static_cast<int>(t->canvas->GetTool())]->setChecked(true);
    _UpdateTitle();
    _SaveSession();
}

bool MainWindow::_ConfirmClose(Tab &tab)
{
    if (!tab.ctl->IsModified())
    {
        return true;
    }
    _doc_tabs->setCurrentWidget(tab.canvas);
    const auto r =
        QMessageBox::question(this, tr("Несохранённые изменения"), tr("Сохранить изменения в «%1»?").arg(Qs(tab.ctl->GetModel().meta.name)),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save)
    {
        return _Save(tab.ctl);
    }
    return r == QMessageBox::Discard;
}

void MainWindow::_CloseTab(int index)
{
    QWidget   *page = _doc_tabs->widget(index);
    const auto it   = std::ranges::find_if(_tabs,
                                           [page](const Tab &t)
                                           {
                                             return t.canvas == page;
                                         });
    if (it == _tabs.end() || !_ConfirmClose(*it))
    {
        return;
    }
    const Tab tab = *it;
    if (_doc_tabs->count() == 1)
    {
        NewDocumentTab(); // there is always a document
    }
    tab.ctl->Pause();
    tab.ctl->DiscardAutosave();
    _tabs.erase(std::ranges::find(_tabs, tab.ctl, &Tab::ctl));
    _doc_tabs->removeTab(_doc_tabs->indexOf(tab.canvas)); // switches the active document first
    _inspector_stack->removeWidget(tab.inspector);
    _timeline_stack->removeWidget(tab.timeline);
    if (_ctx.Active() == tab.ctl)
    {
        _ctx.SetActive(_Ctl());
    }
    tab.canvas->deleteLater();
    tab.inspector->deleteLater();
    tab.timeline->deleteLater();
    tab.ctl->deleteLater(); // after the widgets that refer to it
    _SaveSession();
}

void MainWindow::_SaveSession()
{
    if (_restoring || (_closing && _tabs.empty()))
    {
        return;
    }
    QVariantList list;
    int          current = 0;
    int          i       = 0;
    for (Controller *c : Documents())
    {
        QVariantMap entry;
        entry[QStringLiteral("id")]       = c->SessionId();
        entry[QStringLiteral("path")]     = c->FilePath();
        entry[QStringLiteral("modified")] = c->IsModified();
        list << entry;
        if (c == _Ctl())
        {
            current = i;
        }
        ++i;
    }
    QSettings s;
    s.setValue(kSessionTabsKey, list);
    s.setValue(kSessionCurrent, current);
}

void MainWindow::RestoreSession(bool example)
{
    const QSettings    s;
    const QVariantList list    = s.value(kSessionTabsKey).toList();
    const int          current = s.value(kSessionCurrent, 0).toInt();
    _restoring                 = true;
    for (const QVariant &v : list)
    {
        const QVariantMap entry = v.toMap();
        Tab              *t     = _AddTab();
        const QString     path  = entry.value(QStringLiteral("path")).toString();
        bool              ok =
            t->ctl->RestoreAutosave(entry.value(QStringLiteral("id")).toString(), path, entry.value(QStringLiteral("modified")).toBool());
        if (!ok && !path.isEmpty())
        {
            ok = t->ctl->OpenFile(path, nullptr);
        }
        if (!ok)
        {
            _tabs.pop_back();
            _doc_tabs->removeTab(_doc_tabs->indexOf(t->canvas));
            delete t->canvas;
            delete t->inspector;
            delete t->timeline;
            delete t->ctl;
        }
    }
    if (_tabs.empty())
    {
        // first run, or a session of the single-document version
        Tab          *t      = _AddTab();
        const QString legacy = QFileInfo(AppContext::SessionDir()).absolutePath() + QStringLiteral("/autosave.json");
        bool          ok     = false;
        if (QFile::exists(legacy))
        {
            QFile::copy(legacy, t->ctl->AutosavePath());
            ok = t->ctl->RestoreAutosave(t->ctl->SessionId(), s.value(QStringLiteral("session/file")).toString(),
                                         s.value(QStringLiteral("session/modified")).toBool());
            QFile::remove(legacy);
        }
        if (!ok && example)
        {
            t->ctl->LoadSample();
        }
        else if (!ok)
        {
            t->ctl->NewDocument();
        }
    }
    _restoring = false;
    _doc_tabs->setCurrentIndex(std::clamp(current, 0, _doc_tabs->count() - 1));
    _OnCurrentTabChanged(_doc_tabs->currentIndex());
    _SaveSession();
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void MainWindow::_UpdateTitle()
{
    Controller *ctl = _Ctl();
    if (ctl == nullptr)
    {
        return;
    }
    const QString name = Qs(ctl->GetModel().meta.name);
    const QString file = ctl->FilePath().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(QFileInfo(ctl->FilePath()).fileName());
    setWindowTitle(QStringLiteral("%1%2[*] — Animated Diagrams").arg(name, file));
    setWindowModified(ctl->IsModified());
    _undo_act->setEnabled(ctl->Doc().CanUndo());
    _redo_act->setEnabled(ctl->Doc().CanRedo());
}

void MainWindow::_UpdateMcpState()
{
    const bool running = _ctx.IsMcpRunning();
    if (_mcp_act != nullptr)
    {
        const QSignalBlocker block(_mcp_act);
        _mcp_act->setChecked(running);
    }
    _mcp_label->setText(running ? tr("MCP: %1").arg(_ctx.McpUrl()) : tr("MCP: выкл"));
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    if (_fit_on_show)
    {
        _fit_on_show = false;
        QTimer::singleShot(0, this,
                           [this]
                           {
                               if (Tab *t = _Current(); t != nullptr)
                               {
                                   t->canvas->FitView();
                               }
                           });
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (_ctx.Exports().Pending() > 0)
    {
        const auto r =
            QMessageBox::question(this, tr("Идёт экспорт"), tr("Фоновых экспортов: %1. Прервать их и выйти?").arg(_ctx.Exports().Pending()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes)
        {
            e->ignore();
            return;
        }
        _ctx.Exports().CancelAll();
    }
    // unsaved changes are not lost: every tab is restored from its autosave
    _SaveSession();
    _closing = true;
    for (const Tab &t : _tabs)
    {
        t.ctl->Pause();
    }
    QSettings s;
    s.setValue(kGeometryKey, saveGeometry());
    s.setValue(kStateKey, saveState(kStateVersion));
    e->accept();
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void MainWindow::_NewDiagram() { NewDocumentTab(); }

bool MainWindow::OpenPath(const QString &path)
{
    if (IsDrawio(path))
    {
        return ImportDrawioPath(path);
    }
    // already open: switch to it
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (const Tab &t : _tabs)
    {
        if (!t.ctl->FilePath().isEmpty() && QFileInfo(t.ctl->FilePath()) == QFileInfo(abs))
        {
            _doc_tabs->setCurrentWidget(t.canvas);
            return true;
        }
    }
    Tab    *t = _TargetTab();
    QString err;
    if (!t->ctl->OpenFile(path, &err))
    {
        QMessageBox::warning(this, tr("Ошибка открытия"), tr("Не удалось открыть %1:\n%2").arg(path, err));
        return false;
    }
    QSettings().setValue(kLastDirKey, QFileInfo(path).absolutePath());
    t->canvas->FitView();
    statusBar()->showMessage(tr("Открыто: %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

void MainWindow::_OpenDiagram()
{
    const QString     dir = QSettings().value(kLastDirKey, QDir::homePath()).toString();
    const QStringList paths =
        QFileDialog::getOpenFileNames(this, tr("Открыть диаграммы"), dir, tr("Диаграммы (*.json);;draw.io (*.drawio *.xml)"));
    for (const QString &p : paths)
    {
        OpenPath(p);
    }
}

void MainWindow::_ImportDrawio()
{
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
                               "свободный текст — заметками. Документ откроется в новой вкладке."));
    note->setObjectName(QStringLiteral("hint"));
    note->setWordWrap(true);
    form->addRow(note);
    form->addRow(fields::OkCancelButtons(&dlg));
    if (dlg.exec() != QDialog::Accepted)
    {
        return false;
    }

    Tab    *t = _TargetTab();
    QString err;
    QString report;
    if (!t->ctl->ImportDrawio(path, std::max(0, page_box->currentIndex()), keep_colors->isChecked(), &err, &report))
    {
        QMessageBox::warning(this, tr("Импорт draw.io"), tr("Не удалось импортировать %1:\n%2").arg(path, err));
        return false;
    }
    QSettings().setValue(kLastDirKey, QFileInfo(path).absolutePath());
    t->canvas->FitView();
    statusBar()->showMessage(report, 8000);
    return true;
}

bool MainWindow::_Save(Controller *ctl)
{
    if (ctl == nullptr)
    {
        return false;
    }
    if (ctl->FilePath().isEmpty())
    {
        return _SaveAs(ctl);
    }
    QString err;
    if (!ctl->SaveFile(ctl->FilePath(), &err))
    {
        QMessageBox::warning(this, tr("Ошибка сохранения"), err);
        return false;
    }
    statusBar()->showMessage(tr("Сохранено: %1").arg(QDir::toNativeSeparators(ctl->FilePath())), 4000);
    return true;
}

bool MainWindow::_SaveAs(Controller *ctl)
{
    if (ctl == nullptr)
    {
        return false;
    }
    static const QRegularExpression kBad(QStringLiteral(R"([\\/:*?"<>|\s]+)"));
    const QString                   dir  = QSettings().value(kLastDirKey, QDir::homePath()).toString();
    const QString                   name = Qs(ctl->GetModel().meta.name).replace(kBad, QStringLiteral("_"));
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
    if (!ctl->SaveFile(path, &err))
    {
        QMessageBox::warning(this, tr("Ошибка сохранения"), err);
        return false;
    }
    statusBar()->showMessage(tr("Сохранено: %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

void MainWindow::_RenameDiagram()
{
    Controller *ctl = _Ctl();
    if (ctl == nullptr)
    {
        return;
    }
    bool          ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Переименовать"), tr("Название диаграммы:"), QLineEdit::Normal, Qs(ctl->GetModel().meta.name), &ok);
    if (ok)
    {
        ctl->Rename(name);
    }
}

void MainWindow::_ExportMedia()
{
    Tab *t = _Current();
    if (t == nullptr)
    {
        return;
    }
    t->ctl->Pause();
    ExportDialog dlg(*t->ctl, t->canvas->VisibleWorldRect(), this);
    if (dlg.exec() == QDialog::Accepted && dlg.JobId() != 0)
    {
        statusBar()->showMessage(tr("Экспорт запущен в фоне — можно продолжать работу"), 5000);
        _exports_dock->show();
        _exports_dock->raise();
    }
}

// ---------------------------------------------------------------------------
// Library, plugins, templates
// ---------------------------------------------------------------------------

void MainWindow::_ShowLibrary(const QString &item_id)
{
    if (_library_dialog == nullptr)
    {
        _library_dialog = new LibraryDialog(_ctx, _Ctl(), this);
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
        _plugins_dialog = new PluginsDialog(_ctx, this);
        _plugins_dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    _plugins_dialog->show();
    _plugins_dialog->raise();
    _plugins_dialog->activateWindow();
}

void MainWindow::_ApplyAnimation(const QString &template_id)
{
    Controller *ctl = _Ctl();
    if (ctl == nullptr || ctl->GetModel().nodes.empty())
    {
        statusBar()->showMessage(tr("Сначала добавьте узлы на диаграмму"), 4000);
        return;
    }
    ctl->Pause();
    ApplyTemplateDialog dlg(*ctl, template_id,
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
    if (!_ctx.StartMcp(port, &err))
    {
        statusBar()->showMessage(tr("MCP-сервер не запущен: %1").arg(err), 8000);
    }
}

void MainWindow::_ToggleMcp(bool on)
{
    if (!on)
    {
        _ctx.StopMcp();
        return;
    }
    const auto port = static_cast<quint16>(QSettings().value(kMcpPortKey, kDefaultMcpPort).toInt());
    QString    err;
    if (!_ctx.StartMcp(port, &err))
    {
        QMessageBox::warning(this, tr("MCP-сервер"), tr("Не удалось открыть порт %1:\n%2").arg(port).arg(err));
        return;
    }
    statusBar()->showMessage(tr("MCP-сервер запущен: %1").arg(_ctx.McpUrl()), 5000);
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
    if (_ctx.IsMcpRunning())
    {
        _ctx.StopMcp();
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
                   "Инструменты работают с активной вкладкой (list_documents / select_document — переключение), "
                   "новые документы открываются во вкладках, изменения отменяются Ctrl+Z.")
                    .arg(url));
    box.exec();
}

void MainWindow::_About()
{
    QMessageBox::about(this, tr("О программе"),
                       tr("<b>Animated Diagrams %1</b><br>Редактор анимированных диаграмм и flow-сценариев: "
                          "сообщения, таймеры, состояния, эффекты, шаблоны анимаций, плагины и дизайн-системы; импорт draw.io; "
                          "встроенный MCP-сервер; фоновый экспорт в GIF / WebM / MP4 (libav).<br><br>"
                          "Qt %2 · C++23")
                           .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

} // namespace ad::ui
