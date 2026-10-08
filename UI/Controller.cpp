#include "Controller.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

#include "Import/DrawioImporter.hpp"
#include "Io/JsonIo.hpp"
#include "Mcp/DocumentTools.hpp"
#include "Mcp/McpServer.hpp"
#include "McpHosts.hpp"
#include "McpHttpServer.hpp"
#include "Model/Sample.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

constexpr auto kSessionFileKey     = "session/file";
constexpr auto kSessionModifiedKey = "session/modified";
constexpr auto kDisabledPluginsKey = "plugins/disabled";

std::optional<QByteArray> ReadAll(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (error != nullptr)
        {
            *error = f.errorString();
        }
        return std::nullopt;
    }
    return f.readAll();
}

std::string_view AsView(const QByteArray &b) { return {b.constData(), static_cast<size_t>(b.size())}; }

} // namespace

Controller::Controller(QObject *parent) : QObject(parent)
{
    _frame_timer.setTimerType(Qt::PreciseTimer);
    _frame_timer.setInterval(16);
    connect(&_frame_timer, &QTimer::timeout, this, &Controller::_Tick);

    _autosave_timer.setSingleShot(true);
    _autosave_timer.setInterval(800);
    connect(&_autosave_timer, &QTimer::timeout, this, &Controller::_WriteAutosave);

    _host = std::make_unique<AppDocumentHost>(*this);
    _mcp  = std::make_unique<mcp::McpServer>(mcp::ServerInfo{
        "animated-diagrams", "Animated Diagrams", QCoreApplication::applicationVersion().toStdString(), mcp::DefaultInstructions()});
    mcp::RegisterDocumentTools(*_mcp, *_host);
    _http = new McpHttpServer(*_mcp, this);
    connect(_http, &McpHttpServer::RequestHandled, this, &Controller::McpActivity);

    ReloadPlugins();
}

Controller::~Controller() = default;

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void Controller::Select(Selection::Kind kind, const std::string &id)
{
    Selection next{kind, id};
    if (id.empty())
    {
        next = {};
    }
    if (next == _selection)
    {
        return;
    }
    _selection = std::move(next);
    Q_EMIT SelectionChanged();
}

void Controller::DeleteSelection()
{
    switch (_selection.kind)
    {
    case Selection::Kind::Node:
        _doc.RemoveNode(_selection.id);
        break;
    case Selection::Kind::Edge:
        _doc.RemoveEdge(_selection.id);
        break;
    case Selection::Kind::Step:
        _doc.RemoveStep(_selection.id);
        break;
    case Selection::Kind::None:
        return;
    }
    ClearSelection();
    Changed(true);
}

void Controller::_ValidateSelection()
{
    const auto &m     = GetModel();
    bool        alive = true;
    switch (_selection.kind)
    {
    case Selection::Kind::Node:
        alive = m.FindNode(_selection.id) != nullptr;
        break;
    case Selection::Kind::Edge:
        alive = m.FindEdge(_selection.id) != nullptr;
        break;
    case Selection::Kind::Step:
        alive = m.FindStep(_selection.id) != nullptr;
        break;
    case Selection::Kind::None:
        break;
    }
    if (!alive)
    {
        ClearSelection();
    }
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void Controller::Edit(const std::string &merge_key, const std::function<void(Model &)> &fn, bool structural)
{
    _doc.Checkpoint(merge_key);
    fn(_doc.Mutable());
    Changed(structural);
}

void Controller::Changed(bool structural)
{
    _modified = true;
    _ValidateSelection();
    _time = std::min(_time, Duration());
    Q_EMIT ModelChanged(structural);
    Q_EMIT DocumentStateChanged();
    _ScheduleAutosave();
}

void Controller::SetView(const View &v)
{
    _doc.Mutable().view = v;
    _ScheduleAutosave();
}

void Controller::Undo()
{
    const View view = GetModel().view; // undo must not jump the view
    if (!_doc.Undo())
    {
        return;
    }
    _doc.Mutable().view = view;
    Changed(true);
}

void Controller::Redo()
{
    const View view = GetModel().view;
    if (!_doc.Redo())
    {
        return;
    }
    _doc.Mutable().view = view;
    Changed(true);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void Controller::ReplaceModel(Model m, const QString &path, bool modified)
{
    _doc.Reset(std::move(m));
    _path     = path;
    _modified = modified;
    _OnReplaced();
}

void Controller::_OnReplaced()
{
    Pause();
    _selection = {};
    _time      = 0;
    Q_EMIT SelectionChanged();
    Q_EMIT ModelChanged(true);
    Q_EMIT TimeChanged(_time);
    Q_EMIT DocumentStateChanged();
    _ScheduleAutosave();
}

void Controller::NewDocument()
{
    Model m           = EmptyModel();
    m.meta.created_at = QDateTime::currentMSecsSinceEpoch();
    ReplaceModel(std::move(m), {}, false);
}

void Controller::LoadSample() { ReplaceModel(SampleModel(), {}, false); }

bool Controller::OpenFile(const QString &path, QString *error)
{
    const auto bytes = ReadAll(path, error);
    if (!bytes.has_value())
    {
        return false;
    }
    auto parsed = ParseModel(AsView(*bytes));
    if (!parsed.has_value())
    {
        if (error != nullptr)
        {
            *error = Qs(parsed.error());
        }
        return false;
    }
    ReplaceModel(std::move(*parsed), QFileInfo(path).absoluteFilePath(), false);
    return true;
}

bool Controller::SaveFile(const QString &path, QString *error)
{
    Model copy = GetModel();
    _registry.EmbedUsedDefinitions(copy); // plugin definitions travel with the file
    const std::string json = SerializeModel(copy);
    QSaveFile         f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(json.data(), static_cast<qint64>(json.size())) < 0 || !f.commit())
    {
        if (error != nullptr)
        {
            *error = f.errorString();
        }
        return false;
    }
    _path     = QFileInfo(path).absoluteFilePath();
    _modified = false;
    Q_EMIT DocumentStateChanged();
    _WriteAutosave();
    return true;
}

bool Controller::ImportDrawio(const QString &path, int page, bool keep_colors, QString *error, QString *report)
{
    const auto bytes = ReadAll(path, error);
    if (!bytes.has_value())
    {
        return false;
    }
    DrawioImportOptions opt;
    opt.page        = page;
    opt.keep_colors = keep_colors;
    DrawioImportReport rep;
    auto               model = ad::ImportDrawio(AsView(*bytes), opt, &rep);
    if (!model.has_value())
    {
        if (error != nullptr)
        {
            *error = Qs(model.error());
        }
        return false;
    }
    if (model->meta.name.empty() || model->meta.name == "Импорт draw.io")
    {
        model->meta.name = Us(QFileInfo(path).completeBaseName());
    }
    ReplaceModel(std::move(*model), {}, true);
    if (report != nullptr)
    {
        *report = tr("Импортировано: узлов %1, связей %2, заметок %3; пропущено %4")
                      .arg(rep.nodes)
                      .arg(rep.edges)
                      .arg(rep.notes)
                      .arg(rep.skipped);
    }
    return true;
}

void Controller::SetFilePath(const QString &path)
{
    _path = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    Q_EMIT DocumentStateChanged();
}

void Controller::Rename(const QString &name)
{
    Edit("meta:name",
         [&](Model &m)
         {
             m.meta.name = Us(name);
         });
}

QString Controller::AutosavePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/autosave.json");
}

QString Controller::UserPluginDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
}

QStringList Controller::SystemPluginDirs()
{
    const QString app = QCoreApplication::applicationDirPath();
    // next to the exe, Windows install root (exe in bin/), Linux prefix
    QStringList dirs{app + QStringLiteral("/plugins"), app + QStringLiteral("/../plugins"),
                     app + QStringLiteral("/../share/animated-diagrams/plugins")};
    // extra directories (tests, development, portable setups)
    for (const QString &d : qEnvironmentVariable("AD_PLUGIN_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts))
    {
        dirs << d;
    }
    return dirs;
}

void Controller::RestoreSession()
{
    QString    err;
    const auto bytes = ReadAll(AutosavePath(), &err);
    if (bytes.has_value())
    {
        if (auto parsed = ParseModel(AsView(*bytes)); parsed.has_value())
        {
            const QSettings s;
            ReplaceModel(std::move(*parsed), s.value(kSessionFileKey).toString(), s.value(kSessionModifiedKey).toBool());
            return;
        }
    }
    LoadSample();
}

void Controller::_ScheduleAutosave() { _autosave_timer.start(); }

void Controller::_WriteAutosave()
{
    _autosave_timer.stop();
    const QString path = AutosavePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile         f(path);
    const std::string json = SerializeModel(GetModel());
    if (f.open(QIODevice::WriteOnly) && f.write(json.data(), static_cast<qint64>(json.size())) >= 0)
    {
        f.commit();
    }
    QSettings s;
    s.setValue(kSessionFileKey, _path);
    s.setValue(kSessionModifiedKey, _modified);
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void Controller::Play()
{
    if (_playing)
    {
        return;
    }
    if (_time >= Duration() - 1)
    {
        _time = 0;
    }
    _playing = true;
    _clock.start();
    _last_ms = 0;
    _frame_timer.start();
    Q_EMIT PlayingChanged(true);
}

void Controller::Pause()
{
    if (!_playing)
    {
        return;
    }
    _playing = false;
    _frame_timer.stop();
    Q_EMIT PlayingChanged(false);
}

void Controller::TogglePlay()
{
    if (_playing)
    {
        Pause();
    }
    else
    {
        Play();
    }
}

void Controller::Stop()
{
    Pause();
    Seek(0);
}

void Controller::Seek(double t)
{
    _time = std::clamp(t, 0.0, Duration());
    Q_EMIT TimeChanged(_time);
}

void Controller::_Tick()
{
    const qint64 now = _clock.elapsed();
    _time += static_cast<double>(now - _last_ms) * _speed;
    _last_ms = now;
    if (_time >= Duration())
    {
        if (_loop)
        {
            _time = 0;
        }
        else
        {
            _time = Duration();
            Pause();
        }
    }
    Q_EMIT TimeChanged(_time);
}

// ---------------------------------------------------------------------------
// Plugins
// ---------------------------------------------------------------------------

void Controller::LoadPlugins(PluginManager &plugins, Registry &registry)
{
    std::vector<std::filesystem::path> system_dirs;
    for (const QString &d : SystemPluginDirs())
    {
        system_dirs.emplace_back(d.toStdU16String());
    }
    plugins.SetDirectories(std::filesystem::path(UserPluginDir().toStdU16String()), system_dirs);
    std::set<std::string> disabled;
    for (const QString &id : QSettings().value(kDisabledPluginsKey).toStringList())
    {
        disabled.insert(Us(id));
    }
    plugins.SetDisabledIds(disabled);
    plugins.Scan();
    plugins.ApplyTo(registry);
}

void Controller::ReloadPlugins()
{
    LoadPlugins(_plugins, _registry);
    Q_EMIT LibraryChanged();
    Q_EMIT ModelChanged(true);
}

void Controller::SetPluginEnabled(const std::string &id, bool enabled)
{
    _plugins.SetEnabled(id, enabled);
    QStringList disabled;
    for (const auto &d : _plugins.DisabledIds())
    {
        disabled << Qs(d);
    }
    QSettings().setValue(kDisabledPluginsKey, disabled);
    _plugins.ApplyTo(_registry);
    Q_EMIT LibraryChanged();
    Q_EMIT ModelChanged(true);
}

// ---------------------------------------------------------------------------
// MCP
// ---------------------------------------------------------------------------

bool Controller::StartMcp(quint16 port, QString *error)
{
    const bool ok = _http->Start(port, error);
    Q_EMIT McpStateChanged();
    return ok;
}

void Controller::StopMcp()
{
    _http->Stop();
    Q_EMIT McpStateChanged();
}

bool    Controller::IsMcpRunning() const { return _http->IsRunning(); }
QString Controller::McpUrl() const { return _http->Url(); }

} // namespace ad::ui
