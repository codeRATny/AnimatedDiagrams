#pragma once
// Контроллер приложения: документ + выделение + воспроизведение + файлы/автосохранение.
// Виджеты общаются только через него (сигналы Qt).

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <string>

#include "ad/document.hpp"

namespace app {

class Controller : public QObject {
    Q_OBJECT

public:
    explicit Controller(QObject* parent = nullptr);

    [[nodiscard]] const ad::Model& model() const { return doc_.model(); }
    [[nodiscard]] ad::Document& document() { return doc_; }

    // ---- выделение ----------------------------------------------------------
    [[nodiscard]] const ad::Selection& selection() const { return sel_; }
    void select(ad::Selection::Kind kind, const std::string& id);
    void clearSelection() { select(ad::Selection::Kind::None, {}); }
    /// Удалить выделенный узел/связь/шаг.
    void deleteSelection();

    // ---- правки -------------------------------------------------------------
    /// checkpoint(mergeKey) + fn(model) + уведомление. Одинаковый mergeKey подряд = один шаг отмены.
    void edit(const std::string& mergeKey, const std::function<void(ad::Model&)>& fn, bool structural = false);
    /// Уведомить об изменении после вызова операций Document (addNode и т.п.).
    void changed(bool structural = true);
    /// Изменить вид (масштаб/сдвиг) — не попадает в историю и не помечает документ изменённым.
    void setView(const ad::View& v);
    void undo();
    void redo();

    // ---- файлы --------------------------------------------------------------
    void newDocument();
    void loadSample();
    bool openFile(const QString& path, QString* error);
    bool saveFile(const QString& path, QString* error);
    /// Восстановить сессию из автосохранения (или загрузить пример).
    void restoreSession();
    [[nodiscard]] const QString& filePath() const { return path_; }
    [[nodiscard]] bool isModified() const { return modified_; }
    void rename(const QString& name);

    // ---- воспроизведение ----------------------------------------------------
    [[nodiscard]] double time() const { return t_; }
    [[nodiscard]] double duration() const { return model().scenario.duration; }
    [[nodiscard]] bool isPlaying() const { return playing_; }
    [[nodiscard]] double speed() const { return speed_; }
    [[nodiscard]] bool loop() const { return loop_; }
    void play();
    void pause();
    void togglePlay();
    void stop();
    void seek(double t);
    void setSpeed(double s) { speed_ = s; }
    void setLoop(bool l) { loop_ = l; }

    static QString autosavePath();

signals:
    void modelChanged(bool structural);
    void selectionChanged();
    void timeChanged(double t);
    void playingChanged(bool playing);
    /// Имя, путь, признак изменений, доступность undo/redo.
    void documentStateChanged();

private:
    void tick();
    void replaceModel(ad::Model m, const QString& path, bool modified);
    void validateSelection();
    void scheduleAutosave();
    void writeAutosave();

    ad::Document doc_;
    ad::Selection sel_;
    QString path_;
    bool modified_ = false;

    double t_ = 0;
    bool playing_ = false;
    double speed_ = 1;
    bool loop_ = true;
    QTimer frameTimer_;
    QElapsedTimer clock_;
    qint64 lastMs_ = 0;
    QTimer autosaveTimer_;
};

}  // namespace app
