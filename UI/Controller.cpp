#include "Controller.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

#include "AppContext.hpp"
#include "Import/DrawioImporter.hpp"
#include "Import/MermaidImporter.hpp"
#include "Io/JsonCodec.hpp"
#include "Io/JsonIo.hpp"
#include "Model/Markers.hpp"
#include "Model/Sample.hpp"
#include "QtRender.hpp"
#include "Utils/I18n.hpp"

namespace ad::ui
{

namespace
{

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

Controller::Controller(AppContext &ctx, QObject *parent)
    : QObject(parent), _ctx(ctx), _session_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    _frame_timer.setTimerType(Qt::PreciseTimer);
    _frame_timer.setInterval(16);
    connect(&_frame_timer, &QTimer::timeout, this, &Controller::_Tick);

    _autosave_timer.setSingleShot(true);
    _autosave_timer.setInterval(800);
    connect(&_autosave_timer, &QTimer::timeout, this, &Controller::_WriteAutosave);

    connect(&_ctx, &AppContext::LibraryChanged, this,
            [this]
            {
                Q_EMIT LibraryChanged();
                Q_EMIT ModelChanged(true);
            });
}

const Registry &Controller::Reg() const { return _ctx.Reg(); }

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

void Controller::SelectNodes(std::vector<std::string> ids, const std::string &primary)
{
    std::vector<std::string> valid;
    for (const auto &id : ids)
    {
        if (GetModel().FindNode(id) != nullptr && !std::ranges::contains(valid, id))
        {
            valid.push_back(id);
        }
    }
    if (valid.empty())
    {
        ClearSelection();
        return;
    }
    Selection next{Selection::Kind::Node, std::ranges::contains(valid, primary) ? primary : valid.back()};
    if (valid.size() > 1)
    {
        next.node_ids = std::move(valid);
    }
    if (next != _selection)
    {
        _selection = std::move(next);
        Q_EMIT SelectionChanged();
    }
}

void Controller::SelectAllOfType()
{
    const Node *primary = GetModel().FindNode(_selection.id);
    if (_selection.kind != Selection::Kind::Node || primary == nullptr)
    {
        return;
    }
    const std::string        type = primary->type;
    std::vector<std::string> ids;
    for (const auto &node : GetModel().nodes)
    {
        if (node.type == type)
        {
            ids.push_back(node.id);
        }
    }
    SelectNodes(std::move(ids), _selection.id);
}

namespace
{
constexpr auto kNodeClipboard = "application/x-animated-diagrams-nodes";
}

void Controller::CopyNodes()
{
    if (_selection.kind != Selection::Kind::Node)
    {
        return;
    }
    const auto ids = _selection.node_ids.empty() ? std::vector<std::string>{_selection.id} : _selection.node_ids;
    Model      fragment;
    for (const auto &node : GetModel().nodes)
    {
        if (!std::ranges::contains(ids, node.id))
        {
            continue;
        }
        fragment.nodes.push_back(node);
        if (!std::ranges::any_of(fragment.library.elements,
                                 [&](const ElementType &e)
                                 {
                                     return e.id == node.type;
                                 }))
        {
            ElementType type = Reg().Element(node.type, &GetModel().library);
            type.id          = node.type;
            fragment.library.elements.push_back(std::move(type));
        }
    }
    for (const auto &edge : GetModel().edges)
    {
        if (std::ranges::contains(ids, edge.from) && std::ranges::contains(ids, edge.to))
        {
            fragment.edges.push_back(edge);
        }
    }
    if (fragment.nodes.empty())
    {
        return;
    }
    const nlohmann::json payload = {{"version", 1}, {"document", SerializeModel(fragment)}};
    auto                *mime    = new QMimeData;
    mime->setData(kNodeClipboard, QByteArray::fromStdString(payload.dump()));
    QApplication::clipboard()->setMimeData(mime);
    _last_clipboard.clear();
    _paste_count = 0;
}

void Controller::PasteNodes()
{
    if (_playing)
    {
        return;
    }
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (mime == nullptr || !mime->hasFormat(kNodeClipboard))
    {
        return;
    }
    const QByteArray bytes = mime->data(kNodeClipboard);
    if (bytes.size() > 16 * 1024 * 1024)
    {
        return;
    }
    const auto payload = nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size(), nullptr, false);
    if (!payload.is_object() || !payload.contains("version") || payload["version"] != 1 || !payload.contains("document") ||
        !payload["document"].is_string())
    {
        return;
    }
    const auto text = payload["document"].get<std::string>();
    const auto raw  = nlohmann::json::parse(text, nullptr, false);
    if (!raw.is_object() || !raw.contains("nodes") || !raw["nodes"].is_array() || !raw.contains("edges") || !raw["edges"].is_array() ||
        !raw.contains("scenario") || !raw["scenario"].is_object())
    {
        return;
    }
    const Model          decoded   = json::ModelFromJson(raw);
    const Model         *fragment  = &decoded;
    const nlohmann::json canonical = json::ToJson(decoded);
    if (raw != canonical || decoded.nodes.empty() || !decoded.scenario.steps.empty() || !decoded.scenario.markers.empty() ||
        raw["nodes"].size() != decoded.nodes.size() || raw["edges"].size() != decoded.edges.size())
    {
        return;
    }
    std::set<std::string> ids;
    for (const auto &node : decoded.nodes)
    {
        if (node.id.empty() || node.type.empty() || !ids.insert(node.id).second || !std::isfinite(node.x) || !std::isfinite(node.y) ||
            !std::isfinite(node.w) || !std::isfinite(node.h) || node.w <= 0 || node.h <= 0)
        {
            return;
        }
        for (const auto &port : node.ports)
        {
            if (port.id.empty() || !ids.insert(port.id).second || !std::isfinite(port.dx) || !std::isfinite(port.dy))
            {
                return;
            }
        }
    }
    for (const auto &edge : decoded.edges)
    {
        if (edge.id.empty() || !ids.insert(edge.id).second || edge.from == edge.to || decoded.FindNode(edge.from) == nullptr ||
            decoded.FindNode(edge.to) == nullptr || (!edge.from_port.empty() && decoded.FindPort(edge.from, edge.from_port) == nullptr) ||
            (!edge.to_port.empty() && decoded.FindPort(edge.to, edge.to_port) == nullptr))
        {
            return;
        }
        for (const auto &point : edge.waypoints)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
            {
                return;
            }
        }
    }
    std::set<std::string> definitions;
    for (const auto &type : decoded.library.elements)
    {
        if (type.id.empty() || !definitions.insert(type.id).second)
        {
            return;
        }
    }
    for (const Node &node : fragment->nodes)
    {
        if (!std::ranges::any_of(fragment->library.elements,
                                 [&](const ElementType &e)
                                 {
                                     return e.id == node.type;
                                 }))
        {
            return;
        }
    }
    if (bytes != _last_clipboard)
    {
        _last_clipboard = bytes;
        _paste_count    = 0;
    }
    const double offset = 26.0 * (++_paste_count) / GetModel().view.zoom;
    _doc.Checkpoint();
    Model                             &model = _doc.Mutable();
    std::map<std::string, std::string> types;
    for (const auto &type : fragment->library.elements)
    {
        ElementType copy = type;
        if (const auto *existing = Reg().FindElement(copy.id, &model.library); existing != nullptr)
        {
            if (*existing == copy)
            {
                types[type.id] = copy.id;
                continue;
            }
            int suffix = 1;
            do
            {
                copy.id = type.id + "_copy" + std::to_string(suffix++);
            } while (Reg().FindElement(copy.id, &model.library) != nullptr);
        }
        types[type.id] = copy.id;
        model.library.elements.push_back(std::move(copy));
    }
    std::map<std::string, std::string>                         nodes;
    std::map<std::pair<std::string, std::string>, std::string> ports;
    std::vector<std::string>                                   selected;
    for (const auto &source : fragment->nodes)
    {
        Node copy = source;
        copy.id   = _doc.NewId("n");
        copy.type = types.at(source.type);
        copy.x += offset;
        copy.y += offset;
        nodes[source.id] = copy.id;
        selected.push_back(copy.id);
        copy.ports.clear();
        model.nodes.push_back(std::move(copy));
        for (const auto &port : source.ports)
        {
            Port new_port               = port;
            new_port.id                 = _doc.NewId("p");
            ports[{source.id, port.id}] = new_port.id;
            model.nodes.back().ports.push_back(std::move(new_port));
        }
    }
    for (const auto &source : fragment->edges)
    {
        Edge copy = source;
        copy.id   = _doc.NewId("e");
        copy.from = nodes.at(source.from);
        copy.to   = nodes.at(source.to);
        if (!source.from_port.empty())
        {
            copy.from_port = ports.at({source.from, source.from_port});
        }
        if (!source.to_port.empty())
        {
            copy.to_port = ports.at({source.to, source.to_port});
        }
        for (auto &point : copy.waypoints)
        {
            point = point + Vec2{offset, offset};
        }
        model.edges.push_back(std::move(copy));
    }
    Changed(true);
    SelectNodes(std::move(selected));
}

void Controller::DeleteSelection()
{
    if (_playing)
    {
        return;
    }
    switch (_selection.kind)
    {
    case Selection::Kind::Node:
        if (_selection.node_ids.empty())
        {
            _doc.RemoveNode(_selection.id);
        }
        else
        {
            _doc.RemoveNodes(_selection.node_ids);
        }
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
    const auto &m = GetModel();
    if (_selection.kind == Selection::Kind::Node && !_selection.node_ids.empty())
    {
        SelectNodes(_selection.node_ids, _selection.id);
        return;
    }
    bool alive = true;
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
    if (_doc.CanUndo())
    {
        Q_EMIT HistoryTraversal();
    }
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
    if (_doc.CanRedo())
    {
        Q_EMIT HistoryTraversal();
    }
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
    Q_EMIT ModelReplaced();
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
    Reg().EmbedUsedDefinitions(copy); // plugin definitions travel with the file
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
    if (model->meta.name.empty() || model->meta.name == DrawioDefaultName())
    {
        model->meta.name = Us(QFileInfo(path).completeBaseName());
    }
    ReplaceModel(std::move(*model), {}, true);
    if (report != nullptr)
    {
        *report =
            tr("Imported — nodes: %1, edges: %2, notes: %3; skipped: %4").arg(rep.nodes).arg(rep.edges).arg(rep.notes).arg(rep.skipped);
    }
    return true;
}

bool Controller::ImportMermaid(const QString &text, const QString &name, bool keep_colors, QString *error, QString *report)
{
    MermaidImportOptions opt;
    opt.keep_colors = keep_colors;
    MermaidImportReport rep;
    auto                model = ad::ImportMermaid(Us(text), opt, &rep);
    if (!model.has_value())
    {
        if (error != nullptr)
        {
            *error = Qs(model.error());
        }
        return false;
    }
    if (!name.isEmpty() && model->meta.name == Tr("document", "Mermaid import"))
    {
        model->meta.name = Us(name);
    }
    ReplaceModel(std::move(*model), {}, true);
    if (report != nullptr)
    {
        *report = tr("Imported Mermaid %1 — nodes: %2, edges: %3, steps: %4, markers: %5; skipped: %6")
                      .arg(Qs(rep.kinds))
                      .arg(rep.nodes)
                      .arg(rep.edges)
                      .arg(rep.steps)
                      .arg(rep.markers)
                      .arg(rep.skipped);
        for (const auto &w : rep.warnings)
        {
            *report += QStringLiteral("\n") + Qs(w);
        }
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

QString Controller::AutosavePath() const { return AppContext::SessionDir() + QLatin1Char('/') + _session_id + QStringLiteral(".json"); }

bool Controller::RestoreAutosave(const QString &session_id, const QString &path, bool modified)
{
    _session_id = session_id;
    QString    err;
    const auto bytes = ReadAll(AutosavePath(), &err);
    if (!bytes.has_value())
    {
        return false;
    }
    auto parsed = ParseModel(AsView(*bytes));
    if (!parsed.has_value())
    {
        return false;
    }
    ReplaceModel(std::move(*parsed), path, modified);
    return true;
}

void Controller::DiscardAutosave()
{
    _autosave_timer.stop();
    QFile::remove(AutosavePath());
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
    _StartClock();
}

void Controller::PlayUntil(double until)
{
    until = std::clamp(until, 0.0, Duration());
    if (until <= _time)
    {
        return;
    }
    _play_until = until;
    if (!_playing)
    {
        _StartClock();
    }
}

void Controller::_StartClock()
{
    _playing = true;
    _clock.start();
    _last_ms = 0;
    _frame_timer.start();
    Q_EMIT PlayingChanged(true);
}

void Controller::Pause()
{
    _play_until.reset();
    if (!_playing)
    {
        return;
    }
    _playing = false;
    _frame_timer.stop();
    Q_EMIT PlayingChanged(false);
}

void Controller::SetStopAtMarkers(bool on)
{
    if (on == _stop_at_markers)
    {
        return;
    }
    _stop_at_markers = on;
    Q_EMIT StopAtMarkersChanged(on);
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
    _play_until.reset(); // a jump cancels PlayUntil (playback continues normally)
    _time = std::clamp(t, 0.0, Duration());
    Q_EMIT TimeChanged(_time);
}

void Controller::_Tick()
{
    const qint64 now  = _clock.elapsed();
    const double prev = _time;
    _time += static_cast<double>(now - _last_ms) * _speed;
    _last_ms = now;
    if (_play_until.has_value() && _time >= *_play_until)
    {
        _time = *_play_until;
        Pause();
    }
    else if (const auto marker =
                 _stop_at_markers && !_play_until.has_value() ? MarkerCrossed(GetModel().scenario, prev, _time) : std::nullopt;
             marker.has_value())
    {
        _time = *marker;
        Pause();
    }
    else if (_time >= Duration())
    {
        if (_loop && !_play_until.has_value())
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

} // namespace ad::ui
