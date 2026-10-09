#ifndef _UI_EXPORT_DIALOG_HPP_
#define _UI_EXPORT_DIALOG_HPP_

#include <QDialog>

#include "Exporter.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;

/// @file ExportDialog.hpp
/// @brief Export settings: format, fps, scale, quality, framing, background. "Export"
///        queues a background job (see ExportManager) and closes the dialog.

namespace ad::ui
{

class Controller;

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    ExportDialog(Controller &ctl, Rect view_rect, QWidget *parent = nullptr);

    /// Id of the queued job (0 -- nothing was started).
    [[nodiscard]] int JobId() const { return _job_id; }

private:
    [[nodiscard]] ExportOptions _Options() const;
    void                        _UpdateEstimate();
    void                        _Start();
    void                        _SetBackground(const QColor &c);

    Controller &_ctl;
    Rect        _view_rect;
    QColor      _background;
    int         _job_id = 0;

    QComboBox   *_format    = nullptr;
    QComboBox   *_fps       = nullptr;
    QComboBox   *_scale     = nullptr;
    QComboBox   *_quality   = nullptr;
    QComboBox   *_framing   = nullptr;
    QPushButton *_bg_button = nullptr;
    QCheckBox   *_loop      = nullptr;
    QLabel      *_estimate  = nullptr;

    // PowerPoint
    QWidget   *_pptx_box     = nullptr;
    QComboBox *_pptx_mode    = nullptr;
    QComboBox *_slide_size   = nullptr;
    QCheckBox *_by_markers   = nullptr;
    QCheckBox *_insert       = nullptr;
    QLineEdit *_insert_path  = nullptr;
    QSpinBox  *_insert_after = nullptr;
};

} // namespace ad::ui

#endif // _UI_EXPORT_DIALOG_HPP_
