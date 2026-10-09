#ifndef _UI_EXPORT_MANAGER_HPP_
#define _UI_EXPORT_MANAGER_HPP_

#include <QObject>
#include <QString>

#include <memory>
#include <thread>
#include <vector>

#include "Exporter.hpp"
#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file ExportManager.hpp
/// @brief Background export queue: jobs render from their own snapshot of the document,
///        so editing continues while they run. At most kMaxParallel jobs run at once.

namespace ad::ui
{

struct ExportJobInfo
{
    enum class State
    {
        Queued,
        Running,
        Done,
        Failed,
        Cancelled
    };

    int          id = 0;
    QString      title; // document name
    QString      path;
    ExportFormat format = ExportFormat::Gif;
    State        state  = State::Queued;
    int          done   = 0;
    int          total  = 0;
    QString      message; // error text or a short result ("120 frames · 1.2 MB · libx264")
    ExportResult result;

    [[nodiscard]] bool Finished() const { return state == State::Done || state == State::Failed || state == State::Cancelled; }
};

class ExportManager : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMaxParallel = 2;

    explicit ExportManager(QObject *parent = nullptr);
    ~ExportManager() override;

    ExportManager(const ExportManager &)            = delete;
    ExportManager &operator=(const ExportManager &) = delete;

    /// Queue an export of a document snapshot. Returns the job id.
    int  Start(const QString &title, Model model, Registry registry, ExportOptions options);
    void Cancel(int id);
    void CancelAll();
    /// Remove finished jobs from the list.
    void ClearFinished();
    /// Block (with a local event loop, the UI stays responsive) until the job finishes.
    void Wait(int id);

    [[nodiscard]] std::vector<ExportJobInfo> Jobs() const;
    [[nodiscard]] const ExportJobInfo       *Info(int id) const;
    /// Queued + running jobs.
    [[nodiscard]] int Pending() const;

Q_SIGNALS:
    void JobsChanged();
    void JobProgress(int id, int done, int total);
    void JobFinished(int id);

private:
    struct Job
    {
        ExportJobInfo info;
        Model         model;
        Registry      registry;
        ExportOptions options;
        std::jthread  thread;
    };

    Job *_Find(int id);
    void _Pump();
    void _Run(Job &job);
    void _OnFinished(int id, const std::expected<ExportResult, QString> &result);

    std::vector<std::unique_ptr<Job>> _jobs;
    int                               _next_id = 1;
};

QString HumanSize(qint64 bytes);

} // namespace ad::ui

#endif // _UI_EXPORT_MANAGER_HPP_
