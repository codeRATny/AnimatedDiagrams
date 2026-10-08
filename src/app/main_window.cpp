#include "main_window.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <cmath>

#include "ad/catalog.hpp"
#include "ad/timeline.hpp"
#include "canvas_widget.hpp"
#include "controller.hpp"
#include "export_dialog.hpp"
#include "inspector.hpp"
#include "qt_render.hpp"
#include "timeline_widget.hpp"

namespace app {

namespace {

constexpr auto kLastDirKey = "files/lastDir";
constexpr auto kGeometryKey = "window/geometry";
constexpr auto kStateKey = "window/splitters";

QLabel* panelTitle(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("panelTitle");
    return l;
}

}  // namespace

MainWindow::MainWindow(Controller& ctl, QWidget* parent) : QMainWindow(parent), ctl_(ctl) {
    canvas_ = new CanvasWidget(ctl_);
    timeline_ = new TimelineWidget(ctl_);
    inspector_ = new Inspector(ctl_);

    auto* top = new QSplitter(Qt::Horizontal);
    top->addWidget(buildToolPanel());
    top->addWidget(canvas_);
    top->addWidget(inspector_);
    top->setStretchFactor(1, 1);
    top->setSizes({180, 900, 300});
    top->setChildrenCollapsible(false);

    auto* bottom = new QWidget;
    auto* bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);
    bl->addWidget(buildTransport());
    bl->addWidget(timeline_, 1);

    auto* root = new QSplitter(Qt::Vertical);
    root->setObjectName("rootSplitter");
    root->addWidget(top);
    root->addWidget(bottom);
    root->setStretchFactor(0, 1);
    root->setSizes({620, 260});
    root->setChildrenCollapsible(false);
    setCentralWidget(root);

    buildMenus();

    connect(&ctl_, &Controller::documentStateChanged, this, &MainWindow::updateTitle);
    connect(&ctl_, &Controller::timeChanged, this, &MainWindow::updateTransport);
    connect(&ctl_, &Controller::modelChanged, this, &MainWindow::updateTransport);
    connect(&ctl_, &Controller::playingChanged, this, &MainWindow::updateTransport);

    setWindowIcon(QIcon(QStringLiteral(":/icons/animated-diagrams.png")));
    resize(1440, 900);
    const QSettings s;
    restoreGeometry(s.value(kGeometryKey).toByteArray());
    root->restoreState(s.value(kStateKey).toByteArray());

    canvas_->setTool(CanvasWidget::Tool::Select);
    updateTitle();
    updateTransport();
}

// ---- панели -----------------------------------------------------------------

QWidget* MainWindow::buildToolPanel() {
    auto* panel = new QWidget;
    panel->setMinimumWidth(160);
    panel->setMaximumWidth(240);
    auto* l = new QVBoxLayout(panel);
    l->setContentsMargins(10, 10, 10, 10);
    l->setSpacing(6);

    l->addWidget(panelTitle(tr("ИНСТРУМЕНТЫ")));
    auto* group = new QActionGroup(this);
    const std::array<std::pair<QString, QKeySequence>, 3> tools{{
        {tr("▘ Выбор"), QKeySequence(Qt::Key_V)},
        {tr("＋ Узел"), QKeySequence(Qt::Key_N)},
        {tr("↗ Связь"), QKeySequence(Qt::Key_E)},
    }};
    for (int i = 0; i < 3; ++i) {
        auto* a = new QAction(tools[static_cast<std::size_t>(i)].first, this);
        a->setCheckable(true);
        a->setShortcut(tools[static_cast<std::size_t>(i)].second);
        a->setToolTip(QStringLiteral("%1 (%2)").arg(a->text(), a->shortcut().toString()));
        group->addAction(a);
        addAction(a);
        connect(a, &QAction::triggered, this, [this, i] { canvas_->setTool(static_cast<CanvasWidget::Tool>(i)); });
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        l->addWidget(b);
        toolActs_[i] = a;
    }
    connect(canvas_, &CanvasWidget::toolChanged, this, [this](CanvasWidget::Tool t) {
        toolActs_[static_cast<int>(t)]->setChecked(true);
    });

    l->addSpacing(8);
    l->addWidget(panelTitle(tr("ТИПЫ УЗЛОВ")));
    auto* kinds = new QButtonGroup(this);
    for (const auto& k : ad::nodeKinds()) {
        auto* b = new QPushButton(qs(k.icon) + QLatin1Char(' ') + qs(k.label));
        b->setCheckable(true);
        b->setStyleSheet(QStringLiteral("text-align: left; border-left: 3px solid %1;").arg(qs(k.color.hex())));
        kinds->addButton(b);
        l->addWidget(b);
        if (k.id == "service") b->setChecked(true);
        connect(b, &QPushButton::clicked, this, [this, id = std::string(k.id)] {
            canvas_->setPendingKind(id);
            canvas_->setTool(CanvasWidget::Tool::Node);
        });
    }
    auto* h = new QLabel(tr("Выберите тип и кликните по холсту."));
    h->setObjectName("hint");
    h->setWordWrap(true);
    l->addWidget(h);
    l->addStretch(1);
    return panel;
}

QWidget* MainWindow::buildTransport() {
    auto* bar = new QWidget;
    auto* l = new QHBoxLayout(bar);
    l->setContentsMargins(8, 6, 8, 6);
    l->setSpacing(6);

    auto mk = [&](QStyle::StandardPixmap icon, const QString& tip, auto slot) {
        auto* b = new QPushButton(style()->standardIcon(icon), QString());
        b->setToolTip(tip);
        b->setFixedWidth(40);
        connect(b, &QPushButton::clicked, this, slot);
        l->addWidget(b);
        return b;
    };
    mk(QStyle::SP_MediaSkipBackward, tr("В начало (Home)"), [this] { ctl_.seek(0); });
    playBtn_ = mk(QStyle::SP_MediaPlay, tr("Играть / Пауза (Пробел)"), [this] { ctl_.togglePlay(); });
    playBtn_->setObjectName("primaryButton");
    mk(QStyle::SP_MediaStop, tr("Стоп"), [this] { ctl_.stop(); });
    mk(QStyle::SP_MediaSkipForward, tr("В конец (End)"), [this] { ctl_.seek(ctl_.duration()); });

    readout_ = new QLabel;
    readout_->setObjectName("readout");
    l->addWidget(readout_);

    l->addWidget(new QLabel(tr("Скорость")));
    speed_ = new QComboBox;
    for (double s : {0.25, 0.5, 1.0, 2.0, 4.0}) speed_->addItem(QStringLiteral("%1×").arg(s), s);
    speed_->setCurrentIndex(2);
    connect(speed_, &QComboBox::currentIndexChanged, this, [this] { ctl_.setSpeed(speed_->currentData().toDouble()); });
    l->addWidget(speed_);

    l->addWidget(new QLabel(tr("Длит., с")));
    duration_ = new QDoubleSpinBox;
    duration_->setRange(1, 3600);
    duration_->setDecimals(1);
    duration_->setSingleStep(0.5);
    duration_->setKeyboardTracking(false);
    connect(duration_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (std::abs(v * 1000 - ctl_.duration()) < 1) return;
        ctl_.document().setDuration(v * 1000);
        ctl_.changed(false);
    });
    l->addWidget(duration_);

    loop_ = new QCheckBox(tr("Повтор"));
    loop_->setChecked(ctl_.loop());
    connect(loop_, &QCheckBox::toggled, this, [this](bool v) { ctl_.setLoop(v); });
    l->addWidget(loop_);

    l->addStretch(1);
    stepType_ = new QComboBox;
    for (const auto& t : ad::stepTypes()) stepType_->addItem(qs(t.label), static_cast<int>(t.type));
    l->addWidget(stepType_);
    auto* add = new QPushButton(tr("＋ Добавить шаг"));
    add->setObjectName("primaryButton");
    connect(add, &QPushButton::clicked, this, &MainWindow::addStep);
    l->addWidget(add);
    return bar;
}

void MainWindow::buildMenus() {
    auto act = [this](QMenu* menu, const QString& text, const QKeySequence& key, auto slot) {
        QAction* a = menu->addAction(text);
        if (!key.isEmpty()) a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    QMenu* file = menuBar()->addMenu(tr("&Файл"));
    act(file, tr("Новая диаграмма"), QKeySequence::New, [this] { newDiagram(); });
    act(file, tr("Открыть…"), QKeySequence::Open, [this] { openDiagram(); });
    act(file, tr("Сохранить"), QKeySequence::Save, [this] { save(); });
    act(file, tr("Сохранить как…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this] { saveAs(); });
    file->addSeparator();
    act(file, tr("Загрузить пример"), {}, [this] {
        if (!confirmDiscard()) return;
        ctl_.loadSample();
        canvas_->fitView();
    });
    act(file, tr("Переименовать…"), {}, [this] { renameDiagram(); });
    file->addSeparator();
    act(file, tr("Экспорт GIF / видео…"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { exportMedia(); });
    file->addSeparator();
    act(file, tr("Выход"), QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { close(); });

    QMenu* edit = menuBar()->addMenu(tr("&Правка"));
    undoAct_ = act(edit, tr("Отменить"), QKeySequence::Undo, [this] { ctl_.undo(); });
    redoAct_ = act(edit, tr("Повторить"), QKeySequence::Redo, [this] { ctl_.redo(); });
    edit->addSeparator();
    QAction* del = act(edit, tr("Удалить выделенное"), QKeySequence::Delete, [this] { ctl_.deleteSelection(); });
    del->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    act(edit, tr("Дублировать шаг"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] {
        if (ctl_.selection().kind != ad::Selection::Kind::Step) return;
        if (const ad::Step* s = ctl_.document().duplicateStep(ctl_.selection().id)) {
            const std::string id = s->id;
            ctl_.changed(true);
            ctl_.select(ad::Selection::Kind::Step, id);
        }
    });
    act(edit, tr("Снять выделение"), QKeySequence(Qt::Key_Escape), [this] {
        canvas_->cancelInteraction();
        ctl_.clearSelection();
    });

    QMenu* view = menuBar()->addMenu(tr("&Вид"));
    act(view, tr("Вписать в экран"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { canvas_->fitView(); });
    act(view, tr("Сбросить масштаб"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] { canvas_->resetView(); });
    act(view, tr("Увеличить"), QKeySequence::ZoomIn, [this] { canvas_->zoomBy(1.2); });
    act(view, tr("Уменьшить"), QKeySequence::ZoomOut, [this] { canvas_->zoomBy(1 / 1.2); });
    view->addSeparator();
    act(view, tr("Полный экран"), QKeySequence(Qt::Key_F11), [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });

    QMenu* play = menuBar()->addMenu(tr("&Воспроизведение"));
    act(play, tr("Играть / Пауза"), QKeySequence(Qt::Key_Space), [this] { ctl_.togglePlay(); });
    act(play, tr("Стоп"), {}, [this] { ctl_.stop(); });
    act(play, tr("В начало"), QKeySequence(Qt::Key_Home), [this] { ctl_.seek(0); });
    act(play, tr("В конец"), QKeySequence(Qt::Key_End), [this] { ctl_.seek(ctl_.duration()); });

    QMenu* help = menuBar()->addMenu(tr("&Справка"));
    act(help, tr("О программе"), {}, [this] { about(); });
}

// ---- состояние --------------------------------------------------------------

void MainWindow::updateTitle() {
    const QString name = qs(ctl_.model().meta.name);
    const QString file = ctl_.filePath().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(QFileInfo(ctl_.filePath()).fileName());
    setWindowTitle(QStringLiteral("%1%2[*] — Animated Diagrams").arg(name, file));
    setWindowModified(ctl_.isModified());
    undoAct_->setEnabled(ctl_.document().canUndo());
    redoAct_->setEnabled(ctl_.document().canRedo());
}

void MainWindow::updateTransport() {
    readout_->setText(qs(ad::formatTime(ctl_.time())) + QStringLiteral(" / ") + qs(ad::formatTime(ctl_.duration())));
    playBtn_->setIcon(style()->standardIcon(ctl_.isPlaying() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    if (!duration_->hasFocus()) {
        const QSignalBlocker block(duration_);
        duration_->setValue(ctl_.duration() / 1000);
    }
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    if (fitOnShow_) {
        fitOnShow_ = false;
        QTimer::singleShot(0, canvas_, [this] { canvas_->fitView(); });
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // несохранённые изменения не теряются: сессия восстанавливается из автосохранения
    ctl_.pause();
    QSettings s;
    s.setValue(kGeometryKey, saveGeometry());
    if (auto* root = findChild<QSplitter*>(QStringLiteral("rootSplitter"))) s.setValue(kStateKey, root->saveState());
    e->accept();
}

// ---- действия ---------------------------------------------------------------

bool MainWindow::confirmDiscard() {
    if (!ctl_.isModified()) return true;
    const auto r = QMessageBox::question(this, tr("Несохранённые изменения"),
                                         tr("Сохранить изменения в «%1»?").arg(qs(ctl_.model().meta.name)),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

void MainWindow::newDiagram() {
    if (!confirmDiscard()) return;
    ctl_.newDocument();
    canvas_->resetView();
}

bool MainWindow::openPath(const QString& path) {
    QString err;
    if (!ctl_.openFile(path, &err)) {
        QMessageBox::warning(this, tr("Ошибка открытия"), tr("Не удалось открыть %1:\n%2").arg(path, err));
        return false;
    }
    QSettings().setValue(kLastDirKey, QFileInfo(path).absolutePath());
    canvas_->fitView();
    statusBar()->showMessage(tr("Открыто: %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

void MainWindow::openDiagram() {
    if (!confirmDiscard()) return;
    const QString dir = QSettings().value(kLastDirKey, QDir::homePath()).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Открыть диаграмму"), dir, tr("Диаграммы (*.json)"));
    if (!path.isEmpty()) openPath(path);
}

bool MainWindow::save() {
    if (ctl_.filePath().isEmpty()) return saveAs();
    QString err;
    if (!ctl_.saveFile(ctl_.filePath(), &err)) {
        QMessageBox::warning(this, tr("Ошибка сохранения"), err);
        return false;
    }
    statusBar()->showMessage(tr("Сохранено: %1").arg(QDir::toNativeSeparators(ctl_.filePath())), 4000);
    return true;
}

bool MainWindow::saveAs() {
    const QString dir = QSettings().value(kLastDirKey, QDir::homePath()).toString();
    QString name = qs(ctl_.model().meta.name).replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|\s]+)")), QStringLiteral("_"));
    QString path = QFileDialog::getSaveFileName(this, tr("Сохранить диаграмму"), dir + QLatin1Char('/') + name + QStringLiteral(".json"),
                                                tr("Диаграммы (*.json)"));
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".json");
    QSettings().setValue(kLastDirKey, QFileInfo(path).absolutePath());
    QString err;
    if (!ctl_.saveFile(path, &err)) {
        QMessageBox::warning(this, tr("Ошибка сохранения"), err);
        return false;
    }
    statusBar()->showMessage(tr("Сохранено: %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

void MainWindow::renameDiagram() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Переименовать"), tr("Название диаграммы:"), QLineEdit::Normal,
                                               qs(ctl_.model().meta.name), &ok);
    if (ok) ctl_.rename(name);
}

void MainWindow::exportMedia() {
    ctl_.pause();
    ExportDialog dlg(ctl_, canvas_->visibleWorldRect(), this);
    dlg.exec();
}

void MainWindow::addStep() {
    const auto type = static_cast<ad::StepType>(stepType_->currentData().toInt());
    auto step = ctl_.document().makeDefaultStep(type, ctl_.time());
    if (!step) {
        statusBar()->showMessage(type == ad::StepType::Link ? tr("Сначала добавьте связь между узлами")
                                                            : tr("Сначала добавьте узлы на диаграмму"),
                                 4000);
        return;
    }
    const std::string id = ctl_.document().addStep(std::move(*step)).id;
    ctl_.changed(true);
    ctl_.select(ad::Selection::Kind::Step, id);
}

void MainWindow::about() {
    QMessageBox::about(this, tr("О программе"),
                       tr("<b>Animated Diagrams %1</b><br>Редактор анимированных диаграмм и flow-сценариев: "
                          "сообщения, таймеры, состояния, ретраи; экспорт в GIF / WebM / MP4.<br><br>"
                          "Qt %2 · C++23")
                           .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

}  // namespace app
