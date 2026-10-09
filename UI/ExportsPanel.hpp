#ifndef _UI_EXPORTS_PANEL_HPP_
#define _UI_EXPORTS_PANEL_HPP_

#include <QHash>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

/// @file ExportsPanel.hpp
/// @brief List of background export jobs: progress, cancel, open the result.

namespace ad::ui
{

class ExportManager;

class ExportsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ExportsPanel(ExportManager &exports, QWidget *parent = nullptr);

private:
    struct Row
    {
        QProgressBar *bar    = nullptr;
        QLabel       *status = nullptr;
    };

    void _Rebuild();
    void _OnProgress(int id, int done, int total);

    ExportManager  &_exports;
    QVBoxLayout    *_list  = nullptr;
    QPushButton    *_clear = nullptr;
    QHash<int, Row> _rows;
};

} // namespace ad::ui

#endif // _UI_EXPORTS_PANEL_HPP_
