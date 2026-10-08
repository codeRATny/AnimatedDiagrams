#pragma once
// Документ: модель + операции редактирования + история (undo/redo снапшотами).

#include <cstddef>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "ad/model.hpp"

namespace ad {

/// Генератор id вида "<prefix>_<7 символов base36>", уникальных в пределах модели.
class IdGenerator {
public:
    IdGenerator();
    explicit IdGenerator(std::uint32_t seed);
    std::string next(std::string_view prefix, const Model& model);

private:
    std::mt19937 rng_;
};

/// Длительность сцены по шагам: max(4с, конец последнего шага + 1.2с, округл. до 0.5с),
/// а если длительность задана вручную — не меньше неё.
double autoDuration(const Scenario& s);

class Document {
public:
    static constexpr std::size_t kMaxHistory = 200;

    Document() = default;
    explicit Document(Model m) : model_(std::move(m)) {}

    [[nodiscard]] const Model& model() const { return model_; }
    /// Прямой доступ для правок «на месте»; перед правкой вызовите checkpoint().
    [[nodiscard]] Model& mutableModel() { return model_; }

    /// Заменить модель целиком (новый/открытый документ); история очищается.
    void reset(Model m);

    /// Запомнить текущее состояние в истории. Подряд идущие checkpoint() с одинаковым
    /// непустым mergeKey объединяются (ввод текста посимвольно = один шаг отмены).
    void checkpoint(std::string_view mergeKey = {});
    /// Разорвать объединение правок (следующий checkpoint создаст новый шаг).
    void breakMerge() { lastMergeKey_.clear(); }

    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();

    // ---- операции (каждая сама создаёт шаг истории) ------------------------
    Node& addNode(Vec2 center, std::string_view kind);
    /// nullptr, если from == to или узлов нет.
    Edge* addEdge(std::string_view from, std::string_view to, std::string_view fromPort = {},
                  std::string_view toPort = {});
    void removeNode(std::string_view id);
    void removeEdge(std::string_view id);
    void removeStep(std::string_view id);

    Port* addPort(std::string_view nodeId, std::optional<Vec2> offset = std::nullopt);
    void removePort(std::string_view nodeId, std::string_view portId);

    void addWaypoint(std::string_view edgeId, Vec2 p, std::optional<std::size_t> index = std::nullopt);
    void removeWaypoint(std::string_view edgeId, std::size_t index);
    void reverseEdge(std::string_view edgeId);

    Step& addStep(Step s);
    Step* duplicateStep(std::string_view id);
    /// Шаг по умолчанию заданного типа в момент `at` (null, если для типа нет узлов/связей).
    [[nodiscard]] std::optional<Step> makeDefaultStep(StepType type, double at) const;
    /// Подставить недостающие поля после смены типа шага.
    void normalizeStep(Step& s) const;

    void setDuration(double ms);
    void sortSteps();
    void updateDuration();

    std::string newId(std::string_view prefix) { return ids_.next(prefix, model_); }

private:
    Model model_;
    std::vector<Model> undo_;
    std::vector<Model> redo_;
    std::string lastMergeKey_;
    IdGenerator ids_;
};

}  // namespace ad
