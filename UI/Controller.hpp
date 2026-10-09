#ifndef _UI_CONTROLLER_HPP_
#define _UI_CONTROLLER_HPP_

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Model/Document.hpp"
#include "Model/Registry.hpp"

/// @file Controller.hpp
/// @brief Controller of one open document (a tab): document + selection + playback +
///        file / autosave. Widgets of the tab talk to each other only through it;
///        shared state (registry, plugins, MCP, exports) lives in AppContext.

namespace ad::ui
{

class AppContext;

class Controller : public QObject
{
    Q_OBJECT

public:
    explicit Controller(AppContext &ctx, QObject *parent = nullptr);
    ~Controller() override;

    Controller(const Controller &)            = delete;
    Controller &operator=(const Controller &) = delete;

    [[nodiscard]] const Model    &GetModel() const { return _doc.Get(); }
    [[nodiscard]] Document       &Doc() { return _doc; }
    [[nodiscard]] const Registry &Reg() const;
    [[nodiscard]] AppContext     &Context() { return _ctx; }

    // -----------------------------------------------------------------------
    // Selection
    // -----------------------------------------------------------------------
    [[nodiscard]] const Selection &GetSelection() const { return _selection; }
    void                           Select(Selection::Kind kind, const std::string &id);
    void                           ClearSelection() { Select(Selection::Kind::None, {}); }
    void                           DeleteSelection();

    // -----------------------------------------------------------------------
    // Editing
    // -----------------------------------------------------------------------
    /// Checkpoint(merge_key) + fn(model) + notification. The same merge key in a row = one undo step.
    void Edit(const std::string &merge_key, const std::function<void(Model &)> &fn, bool structural = false);
    /// Notify after Document operations (AddNode, ...).
    void Changed(bool structural = true);
    /// Change zoom / pan -- not recorded in the history, does not mark the document modified.
    void SetView(const View &v);
    void Undo();
    void Redo();

    // -----------------------------------------------------------------------
    // Files
    // -----------------------------------------------------------------------
    void NewDocument();
    void LoadSample();
    bool OpenFile(const QString &path, QString *error);
    bool SaveFile(const QString &path, QString *error);
    bool ImportDrawio(const QString &path, int page, bool keep_colors, QString *error, QString *report = nullptr);
    void ReplaceModel(Model m, const QString &path, bool modified);
    /// Load the autosave of a previous session (tab id from the session list).
    bool RestoreAutosave(const QString &session_id, const QString &path, bool modified);
    /// Delete the autosave (the tab is closed).
    void                         DiscardAutosave();
    [[nodiscard]] const QString &SessionId() const { return _session_id; }
    [[nodiscard]] QString        AutosavePath() const;
    [[nodiscard]] const QString &FilePath() const { return _path; }
    void                         SetFilePath(const QString &path);
    [[nodiscard]] bool           IsModified() const { return _modified; }
    void                         Rename(const QString &name);

    // -----------------------------------------------------------------------
    // Playback
    // -----------------------------------------------------------------------
    [[nodiscard]] double Time() const { return _time; }
    [[nodiscard]] double Duration() const { return GetModel().scenario.duration; }
    [[nodiscard]] bool   IsPlaying() const { return _playing; }
    [[nodiscard]] double Speed() const { return _speed; }
    [[nodiscard]] bool   Loop() const { return _loop; }
    /// Playback pauses when it reaches a marker (see Model/Markers.hpp).
    [[nodiscard]] bool StopAtMarkers() const { return _stop_at_markers; }
    void               Play();
    /// Play from the current time and pause exactly at `until` (presenter mode: the next
    /// marker or the end); looping and "stop at markers" do not apply. No-op when `until` <= Time().
    void PlayUntil(double until);
    void Pause();
    void TogglePlay();
    void Stop();
    void Seek(double t);
    void SetSpeed(double s) { _speed = s; }
    void SetLoop(bool l) { _loop = l; }
    void SetStopAtMarkers(bool on);

Q_SIGNALS:
    void ModelChanged(bool structural);
    void SelectionChanged();
    void TimeChanged(double t);
    void PlayingChanged(bool playing);
    void StopAtMarkersChanged(bool on);
    /// Name, path, modified flag, undo / redo availability.
    void DocumentStateChanged();
    /// Plugins or the registry changed (palette and library lists must be refreshed).
    void LibraryChanged();

private:
    friend class AppDocumentHost;

    void _Tick();
    void _StartClock();
    void _ValidateSelection();
    void _ScheduleAutosave();
    void _WriteAutosave();
    void _OnReplaced();

    AppContext &_ctx;
    Document    _doc;
    Selection   _selection;
    QString     _session_id;
    QString     _path;
    bool        _modified = false;

    double                _time            = 0;
    bool                  _playing         = false;
    double                _speed           = 1;
    bool                  _loop            = true;
    bool                  _stop_at_markers = false;
    std::optional<double> _play_until; // PlayUntil() target
    QTimer                _frame_timer;
    QElapsedTimer         _clock;
    qint64                _last_ms = 0;
    QTimer                _autosave_timer;
};

} // namespace ad::ui

#endif // _UI_CONTROLLER_HPP_
