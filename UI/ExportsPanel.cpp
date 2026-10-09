#include "ExportsPanel.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "ExportManager.hpp"

namespace ad::ui
{

namespace
{

QString StateText(const ExportJobInfo &j)
{
    using S = ExportJobInfo::State;
    switch (j.state)
    {
    case S::Queued:
        return QObject::tr("Queued");
    case S::Running:
        return j.total > 0 ? QObject::tr("Frame %1 / %2").arg(j.done).arg(j.total) : QObject::tr("Preparing…");
    case S::Done:
        return QObject::tr("Done · %1").arg(j.message);
    case S::Failed:
        return QObject::tr("Error: %1").arg(j.message);
    case S::Cancelled:
        return QObject::tr("Cancelled");
    }
    return {};
}

} // namespace

ExportsPanel::ExportsPanel(ExportManager &exports, QWidget *parent) : QWidget(parent), _exports(exports)
{
    auto *content = new QWidget;
    _list         = new QVBoxLayout(content);
    _list->setContentsMargins(8, 8, 8, 8);
    _list->setSpacing(8);
    auto *scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    _clear = new QPushButton(tr("Clear finished"));
    connect(_clear, &QPushButton::clicked, &_exports, &ExportManager::ClearFinished);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll, 1);
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(8, 0, 8, 8);
    bottom->addStretch(1);
    bottom->addWidget(_clear);
    layout->addLayout(bottom);

    connect(&_exports, &ExportManager::JobsChanged, this, &ExportsPanel::_Rebuild);
    connect(&_exports, &ExportManager::JobProgress, this, &ExportsPanel::_OnProgress);
    _Rebuild();
}

void ExportsPanel::_Rebuild()
{
    while (QLayoutItem *item = _list->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    _rows.clear();
    const auto jobs = _exports.Jobs();
    for (auto it = jobs.rbegin(); it != jobs.rend(); ++it) // newest first
    {
        const ExportJobInfo &j   = *it;
        auto                *box = new QWidget;
        box->setObjectName(QStringLiteral("jobCard"));
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(8, 6, 8, 6);
        v->setSpacing(4);

        auto *title =
            new QLabel(QStringLiteral("<b>%1</b> → %2").arg(j.title.toHtmlEscaped(), QFileInfo(j.path).fileName().toHtmlEscaped()));
        title->setToolTip(QDir::toNativeSeparators(j.path));
        v->addWidget(title);

        auto *bar = new QProgressBar;
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        bar->setRange(0, std::max(1, j.total));
        bar->setValue(j.state == ExportJobInfo::State::Done ? bar->maximum() : j.done);
        bar->setVisible(!j.Finished() || j.state == ExportJobInfo::State::Done);
        v->addWidget(bar);

        auto *row    = new QHBoxLayout;
        auto *status = new QLabel(StateText(j));
        status->setObjectName(QStringLiteral("hint"));
        status->setWordWrap(true);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(status, 1);
        if (!j.Finished())
        {
            auto     *cancel = new QPushButton(tr("Cancel"));
            const int id     = j.id;
            connect(cancel, &QPushButton::clicked, this,
                    [this, id]
                    {
                        _exports.Cancel(id);
                    });
            row->addWidget(cancel);
        }
        else if (j.state == ExportJobInfo::State::Done)
        {
            auto         *open   = new QPushButton(tr("Open folder"));
            const QString folder = QFileInfo(j.path).absolutePath();
            connect(open, &QPushButton::clicked, this,
                    [folder]
                    {
                        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
                    });
            row->addWidget(open);
        }
        v->addLayout(row);
        _list->addWidget(box);
        _rows.insert(j.id, Row{bar, status});
    }
    if (jobs.empty())
    {
        auto *empty = new QLabel(tr("No background jobs. Exports (Ctrl+E) run here without blocking editing."));
        empty->setObjectName(QStringLiteral("hint"));
        empty->setWordWrap(true);
        _list->addWidget(empty);
    }
    _list->addStretch(1);
    _clear->setEnabled(std::ranges::any_of(jobs,
                                           [](const ExportJobInfo &j)
                                           {
                                               return j.Finished();
                                           }));
}

void ExportsPanel::_OnProgress(int id, int done, int total)
{
    const auto it = _rows.find(id);
    if (it == _rows.end())
    {
        return;
    }
    it->bar->setRange(0, std::max(1, total));
    it->bar->setValue(done);
    it->status->setText(tr("Frame %1 / %2").arg(done).arg(total));
}

} // namespace ad::ui
