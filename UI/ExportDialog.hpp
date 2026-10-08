#ifndef _UI_EXPORT_DIALOG_HPP_
#define _UI_EXPORT_DIALOG_HPP_

#include <QDialog>

#include <expected>
#include <thread>

#include "Exporter.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

/// @file ExportDialog.hpp
/// @brief Export dialog: format, fps, scale, framing, background; progress and cancel.

namespace ad::ui
{

class Controller;

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    ExportDialog(Controller &ctl, Rect view_rect, QWidget *parent = nullptr);
    ~ExportDialog() override;

    ExportDialog(const ExportDialog &)            = delete;
    ExportDialog &operator=(const ExportDialog &) = delete;

protected:
    void reject() override;

private:
    [[nodiscard]] ExportOptions _Options() const;
    void                        _UpdateEstimate();
    void                        _Start();
    void                        _Finished(const std::expected<ExportResult, QString> &result);
    void                        _SetRunning(bool running);
    void                        _SetBackground(const QColor &c);

    Controller &_ctl;
    Rect        _view_rect;
    QColor      _background;

    QComboBox    *_format        = nullptr;
    QComboBox    *_fps           = nullptr;
    QComboBox    *_scale         = nullptr;
    QComboBox    *_framing       = nullptr;
    QPushButton  *_bg_button     = nullptr;
    QCheckBox    *_loop          = nullptr;
    QLabel       *_estimate      = nullptr;
    QProgressBar *_progress      = nullptr;
    QLabel       *_status        = nullptr;
    QPushButton  *_export_button = nullptr;
    QPushButton  *_close_button  = nullptr;

    std::jthread _worker;
    bool         _running = false;
};

} // namespace ad::ui

#endif // _UI_EXPORT_DIALOG_HPP_
