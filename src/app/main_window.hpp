#pragma once

#include <QMainWindow>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QCheckBox;

namespace app {

class CanvasWidget;
class Controller;
class Inspector;
class TimelineWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Controller& ctl, QWidget* parent = nullptr);

    /// Открыть файл (из командной строки); false и сообщение — при ошибке.
    bool openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    QWidget* buildToolPanel();
    QWidget* buildTransport();
    void buildMenus();
    void updateTitle();
    void updateTransport();

    bool confirmDiscard();
    void newDiagram();
    void openDiagram();
    bool save();
    bool saveAs();
    void renameDiagram();
    void exportMedia();
    void addStep();
    void about();

    Controller& ctl_;
    CanvasWidget* canvas_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    Inspector* inspector_ = nullptr;

    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
    QAction* toolActs_[3] = {};
    QPushButton* playBtn_ = nullptr;
    QLabel* readout_ = nullptr;
    QDoubleSpinBox* duration_ = nullptr;
    QComboBox* speed_ = nullptr;
    QCheckBox* loop_ = nullptr;
    QComboBox* stepType_ = nullptr;
    bool fitOnShow_ = true;
};

}  // namespace app
