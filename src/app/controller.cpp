#include "controller.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

#include "ad/json_io.hpp"
#include "ad/sample.hpp"
#include "qt_render.hpp"

namespace app {

namespace {

constexpr auto kSessionFileKey = "session/file";
constexpr auto kSessionModifiedKey = "session/modified";

std::optional<QByteArray> readAll(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return std::nullopt;
    }
    return f.readAll();
}

}  // namespace

Controller::Controller(QObject* parent) : QObject(parent) {
    frameTimer_.setTimerType(Qt::PreciseTimer);
    frameTimer_.setInterval(16);
    connect(&frameTimer_, &QTimer::timeout, this, &Controller::tick);

    autosaveTimer_.setSingleShot(true);
    autosaveTimer_.setInterval(800);
    connect(&autosaveTimer_, &QTimer::timeout, this, &Controller::writeAutosave);
}

// ---- выделение --------------------------------------------------------------

void Controller::select(ad::Selection::Kind kind, const std::string& id) {
    ad::Selection next{kind, id};
    if (id.empty()) next = {};
    if (next == sel_) return;
    sel_ = std::move(next);
    emit selectionChanged();
}

void Controller::deleteSelection() {
    if (sel_.empty()) return;
    switch (sel_.kind) {
        case ad::Selection::Kind::Node: doc_.removeNode(sel_.id); break;
        case ad::Selection::Kind::Edge: doc_.removeEdge(sel_.id); break;
        case ad::Selection::Kind::Step: doc_.removeStep(sel_.id); break;
        case ad::Selection::Kind::None: return;
    }
    clearSelection();
    changed(true);
}

void Controller::validateSelection() {
    const auto& m = model();
    bool alive = true;
    switch (sel_.kind) {
        case ad::Selection::Kind::Node: alive = m.node(sel_.id) != nullptr; break;
        case ad::Selection::Kind::Edge: alive = m.edge(sel_.id) != nullptr; break;
        case ad::Selection::Kind::Step: alive = m.step(sel_.id) != nullptr; break;
        case ad::Selection::Kind::None: break;
    }
    if (!alive) clearSelection();
}

// ---- правки -----------------------------------------------------------------

void Controller::edit(const std::string& mergeKey, const std::function<void(ad::Model&)>& fn, bool structural) {
    doc_.checkpoint(mergeKey);
    fn(doc_.mutableModel());
    changed(structural);
}

void Controller::changed(bool structural) {
    modified_ = true;
    validateSelection();
    if (t_ > duration()) t_ = duration();
    emit modelChanged(structural);
    emit documentStateChanged();
    scheduleAutosave();
}

void Controller::setView(const ad::View& v) {
    doc_.mutableModel().view = v;
    scheduleAutosave();
}

void Controller::undo() {
    const ad::View view = model().view;  // отмена не должна «прыгать» видом
    if (!doc_.undo()) return;
    doc_.mutableModel().view = view;
    changed(true);
}

void Controller::redo() {
    const ad::View view = model().view;
    if (!doc_.redo()) return;
    doc_.mutableModel().view = view;
    changed(true);
}

// ---- файлы ------------------------------------------------------------------

void Controller::replaceModel(ad::Model m, const QString& path, bool modified) {
    pause();
    doc_.reset(std::move(m));
    path_ = path;
    modified_ = modified;
    sel_ = {};
    t_ = 0;
    emit selectionChanged();
    emit modelChanged(true);
    emit timeChanged(t_);
    emit documentStateChanged();
    scheduleAutosave();
}

void Controller::newDocument() {
    ad::Model m = ad::emptyModel();
    m.meta.createdAt = QDateTime::currentMSecsSinceEpoch();
    replaceModel(std::move(m), {}, false);
}

void Controller::loadSample() { replaceModel(ad::sampleModel(), {}, false); }

bool Controller::openFile(const QString& path, QString* error) {
    const auto bytes = readAll(path, error);
    if (!bytes) return false;
    auto parsed = ad::parseModel(std::string_view(bytes->constData(), static_cast<std::size_t>(bytes->size())));
    if (!parsed) {
        if (error) *error = qs(parsed.error());
        return false;
    }
    replaceModel(std::move(*parsed), QFileInfo(path).absoluteFilePath(), false);
    return true;
}

bool Controller::saveFile(const QString& path, QString* error) {
    QSaveFile f(path);
    const std::string json = ad::serializeModel(model());
    if (!f.open(QIODevice::WriteOnly) || f.write(json.data(), static_cast<qint64>(json.size())) < 0 || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    path_ = QFileInfo(path).absoluteFilePath();
    modified_ = false;
    emit documentStateChanged();
    writeAutosave();
    return true;
}

void Controller::rename(const QString& name) {
    edit("meta:name", [&](ad::Model& m) { m.meta.name = us(name); });
}

QString Controller::autosavePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/autosave.json");
}

void Controller::restoreSession() {
    QString err;
    const auto bytes = readAll(autosavePath(), &err);
    if (bytes) {
        if (auto parsed = ad::parseModel(std::string_view(bytes->constData(), static_cast<std::size_t>(bytes->size())))) {
            const QSettings s;
            replaceModel(std::move(*parsed), s.value(kSessionFileKey).toString(), s.value(kSessionModifiedKey).toBool());
            return;
        }
    }
    loadSample();
}

void Controller::scheduleAutosave() { autosaveTimer_.start(); }

void Controller::writeAutosave() {
    autosaveTimer_.stop();
    const QString path = autosavePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    const std::string json = ad::serializeModel(model());
    if (f.open(QIODevice::WriteOnly) && f.write(json.data(), static_cast<qint64>(json.size())) >= 0) f.commit();
    QSettings s;
    s.setValue(kSessionFileKey, path_);
    s.setValue(kSessionModifiedKey, modified_);
}

// ---- воспроизведение --------------------------------------------------------

void Controller::play() {
    if (playing_) return;
    if (t_ >= duration() - 1) t_ = 0;
    playing_ = true;
    clock_.start();
    lastMs_ = 0;
    frameTimer_.start();
    emit playingChanged(true);
}

void Controller::pause() {
    if (!playing_) return;
    playing_ = false;
    frameTimer_.stop();
    emit playingChanged(false);
}

void Controller::togglePlay() { playing_ ? pause() : play(); }

void Controller::stop() {
    pause();
    seek(0);
}

void Controller::seek(double t) {
    t_ = std::clamp(t, 0.0, duration());
    emit timeChanged(t_);
}

void Controller::tick() {
    const qint64 now = clock_.elapsed();
    const double dt = static_cast<double>(now - lastMs_) * speed_;
    lastMs_ = now;
    t_ += dt;
    if (t_ >= duration()) {
        if (loop_) {
            t_ = 0;
        } else {
            t_ = duration();
            pause();
        }
    }
    emit timeChanged(t_);
}

}  // namespace app
