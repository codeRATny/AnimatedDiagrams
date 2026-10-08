#pragma once
// Диалог экспорта: формат, fps, масштаб, кадрирование, фон; прогресс и отмена.

#include <QDialog>

#include <thread>

#include "exporter.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;

namespace app {

class Controller;

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(Controller& ctl, ad::Rect viewRect, QWidget* parent = nullptr);
    ~ExportDialog() override;

protected:
    void reject() override;

private:
    [[nodiscard]] ExportOptions options() const;
    void updateEstimate();
    void start();
    void finished(const std::expected<ExportResult, QString>& result);
    void setRunning(bool running);
    void setBackground(const QColor& c);

    Controller& ctl_;
    ad::Rect viewRect_;
    QColor background_{0x0a, 0x11, 0x1f};

    QComboBox* format_ = nullptr;
    QComboBox* fps_ = nullptr;
    QComboBox* scale_ = nullptr;
    QComboBox* framing_ = nullptr;
    QPushButton* bgButton_ = nullptr;
    QCheckBox* loop_ = nullptr;
    QLabel* estimate_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;

    std::jthread worker_;
    bool running_ = false;
};

}  // namespace app
