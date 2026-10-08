#pragma once
// Инспектор: форма свойств выбранного узла, связи или шага сценария.

#include <QScrollArea>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class QFormLayout;
class QLabel;
class QVBoxLayout;

namespace app {

class Controller;

class Inspector : public QScrollArea {
    Q_OBJECT

public:
    explicit Inspector(Controller& ctl, QWidget* parent = nullptr);

private:
    using Options = std::vector<std::pair<QString, QString>>;  // (value, label)

    void requestRebuild(bool force);
    void rebuild();
    void buildEmpty(QVBoxLayout* box);
    void buildNode(QVBoxLayout* box, const std::string& id);
    void buildEdge(QVBoxLayout* box, const std::string& id);
    void buildStep(QVBoxLayout* box, const std::string& id);

    // фабрики полей; onChange вызывается с новым значением
    QWidget* lineEdit(const QString& value, std::function<void(const QString&)> onChange, const QString& placeholder = {});
    QWidget* spin(double value, double min, double max, double step, int decimals, std::function<void(double)> onChange);
    QWidget* combo(const Options& options, const QString& current, std::function<void(const QString&)> onChange);
    QWidget* colorButton(const QString& hex, std::function<void(const QString&)> onChange);
    QWidget* slider(double value, double min, double max, double step, std::function<void(double)> onChange);
    QWidget* check(const QString& label, bool value, std::function<void(bool)> onChange);
    QWidget* button(const QString& text, std::function<void()> onClick, bool danger = false);
    QWidget* hint(const QString& text);
    QWidget* row(QWidget* a, QWidget* b);
    QWidget* labeled(const QString& label, QWidget* field);

    Options nodeOptions() const;
    Options edgeOptions() const;
    Options portOptions(const std::string& nodeId) const;

    /// Размер/смещение/положение подписи (у связи и шага «Соединение»); get(Model&) → Edge* | Step*.
    template <class Get>
    void labelControls(QVBoxLayout* box, const std::string& key, double defOffset, Get get);

    Controller& ctl_;
    QLabel* title_ = nullptr;
    bool rebuildPending_ = false;
};

}  // namespace app
