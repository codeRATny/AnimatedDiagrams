#include "ExportManager.hpp"

#include <QEventLoop>

#include <algorithm>

namespace ad::ui
{

QString HumanSize(qint64 bytes)
{
    return bytes < 1024 * 1024 ? QObject::tr("%1 КБ").arg(std::max<qint64>(1, bytes / 1024))
                               : QObject::tr("%1 МБ").arg(static_cast<double>(bytes) / 1024 / 1024, 0, 'f', 2);
}

ExportManager::ExportManager(QObject *parent) : QObject(parent) {}

ExportManager::~ExportManager()
{
    for (auto &j : _jobs)
    {
        j->thread.request_stop();
    }
    for (auto &j : _jobs)
    {
        if (j->thread.joinable())
        {
            j->thread.join(); // queued callbacks to `this` are dropped with the object
        }
    }
}

ExportManager::Job *ExportManager::_Find(int id)
{
    const auto it = std::ranges::find_if(_jobs,
                                         [id](const auto &j)
                                         {
                                             return j->info.id == id;
                                         });
    return it != _jobs.end() ? it->get() : nullptr;
}

int ExportManager::Start(const QString &title, Model model, Registry registry, ExportOptions options)
{
    auto job         = std::make_unique<Job>();
    job->info.id     = _next_id++;
    job->info.title  = title;
    job->info.path   = options.output_path;
    job->info.format = options.format;
    job->model       = std::move(model);
    job->registry    = std::move(registry);
    job->options     = std::move(options);
    const int id     = job->info.id;
    _jobs.push_back(std::move(job));
    Q_EMIT JobsChanged();
    _Pump();
    return id;
}

void ExportManager::_Pump()
{
    int running = static_cast<int>(std::ranges::count_if(_jobs,
                                                         [](const auto &j)
                                                         {
                                                             return j->info.state == ExportJobInfo::State::Running;
                                                         }));
    for (auto &j : _jobs)
    {
        if (running >= kMaxParallel)
        {
            break;
        }
        if (j->info.state == ExportJobInfo::State::Queued)
        {
            _Run(*j);
            ++running;
        }
    }
}

void ExportManager::_Run(Job &job)
{
    job.info.state = ExportJobInfo::State::Running;
    Q_EMIT JobsChanged();
    const int id = job.info.id;
    job.thread   = std::jthread(
        [this, id, &job](const std::stop_token &stop)
        {
            auto result = RunExport(job.model, job.options, job.registry, stop,
                                      [this, id](int done, int total)
                                      {
                                        QMetaObject::invokeMethod(
                                            this,
                                            [this, id, done, total]
                                            {
                                                if (Job *j = _Find(id); j != nullptr)
                                                {
                                                    j->info.done  = done;
                                                    j->info.total = total;
                                                    Q_EMIT JobProgress(id, done, total);
                                                }
                                            },
                                            Qt::QueuedConnection);
                                    });
            QMetaObject::invokeMethod(
                this,
                [this, id, result = std::move(result)]
                {
                    _OnFinished(id, result);
                },
                Qt::QueuedConnection);
        });
}

void ExportManager::_OnFinished(int id, const std::expected<ExportResult, QString> &result)
{
    Job *j = _Find(id);
    if (j == nullptr)
    {
        return;
    }
    if (j->thread.joinable())
    {
        j->thread.join(); // the worker has returned (this callback is its last action)
    }
    if (result.has_value())
    {
        j->info.state  = ExportJobInfo::State::Done;
        j->info.result = *result;
        j->info.done   = result->frames;
        j->info.total  = result->frames;
        QString msg    = tr("%1 кадров · %2").arg(result->frames).arg(HumanSize(result->bytes));
        if (!result->encoder.isEmpty())
        {
            msg += QStringLiteral(" · ") + result->encoder;
        }
        j->info.message = msg;
    }
    else
    {
        j->info.state   = j->thread.get_stop_token().stop_requested() ? ExportJobInfo::State::Cancelled : ExportJobInfo::State::Failed;
        j->info.message = result.error();
    }
    j->model = {}; // free the snapshot
    Q_EMIT JobFinished(id);
    Q_EMIT JobsChanged();
    _Pump();
}

void ExportManager::Cancel(int id)
{
    Job *j = _Find(id);
    if (j == nullptr || j->info.Finished())
    {
        return;
    }
    if (j->info.state == ExportJobInfo::State::Queued)
    {
        j->info.state   = ExportJobInfo::State::Cancelled;
        j->info.message = tr("Отменено");
        Q_EMIT JobFinished(id);
        Q_EMIT JobsChanged();
        return;
    }
    j->thread.request_stop(); // the worker reports "cancelled"
}

void ExportManager::CancelAll()
{
    for (const auto &j : _jobs)
    {
        Cancel(j->info.id);
    }
}

void ExportManager::ClearFinished()
{
    std::erase_if(_jobs,
                  [](const auto &j)
                  {
                      return j->info.Finished();
                  });
    Q_EMIT JobsChanged();
}

void ExportManager::Wait(int id)
{
    const ExportJobInfo *info = Info(id);
    if (info == nullptr || info->Finished())
    {
        return;
    }
    QEventLoop loop;
    connect(this, &ExportManager::JobFinished, &loop,
            [&loop, id](int finished)
            {
                if (finished == id)
                {
                    loop.quit();
                }
            });
    loop.exec();
}

std::vector<ExportJobInfo> ExportManager::Jobs() const
{
    std::vector<ExportJobInfo> out;
    out.reserve(_jobs.size());
    for (const auto &j : _jobs)
    {
        out.push_back(j->info);
    }
    return out;
}

const ExportJobInfo *ExportManager::Info(int id) const
{
    const auto it = std::ranges::find_if(_jobs,
                                         [id](const auto &j)
                                         {
                                             return j->info.id == id;
                                         });
    return it != _jobs.end() ? &(*it)->info : nullptr;
}

int ExportManager::Pending() const
{
    return static_cast<int>(std::ranges::count_if(_jobs,
                                                  [](const auto &j)
                                                  {
                                                      return !j->info.Finished();
                                                  }));
}

} // namespace ad::ui
