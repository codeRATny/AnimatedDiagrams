#pragma once
// Модель документа: узлы, связи, сценарий (шаги на таймлайне).
// Формат JSON совместим с веб-версией (см. json_io.hpp).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ad/catalog.hpp"
#include "ad/geometry.hpp"

namespace ad {

inline constexpr double kDefaultNodeW = 140;
inline constexpr double kDefaultNodeH = 64;
inline constexpr double kMinStepDuration = 100;

struct Port {
    std::string id;
    double dx = 0;  // смещение от левого-верхнего угла узла
    double dy = 0;
    friend bool operator==(const Port&, const Port&) = default;
};

struct Node {
    std::string id;
    std::string label;
    std::string kind = "service";
    std::string color;  // "#rrggbb" — акцент; пусто → цвет типа
    std::string shape = "round";
    std::string subtitle;
    double x = 0;
    double y = 0;
    double w = kDefaultNodeW;
    double h = kDefaultNodeH;
    std::vector<Port> ports;

    [[nodiscard]] Rect rect() const { return {x, y, w, h}; }
    [[nodiscard]] Vec2 center() const { return {x + w / 2, y + h / 2}; }
    [[nodiscard]] const Port* port(std::string_view portId) const;
    [[nodiscard]] Port* port(std::string_view portId);
    friend bool operator==(const Node&, const Node&) = default;
};

struct Edge {
    std::string id;
    std::string from;
    std::string to;
    std::string fromPort;  // пусто → авто (граница узла)
    std::string toPort;
    std::string label;
    std::string style = "solid";  // solid | dashed
    double curve = 0;
    std::vector<Vec2> waypoints;
    bool bidirectional = false;
    std::optional<double> labelPos;
    std::optional<double> labelOff;
    std::optional<double> labelSize;
    friend bool operator==(const Edge&, const Edge&) = default;
};

/// Шаг сценария. Поля общие для всех типов: при смене типа шага значения
/// сохраняются (как в веб-версии), а в JSON пишутся только относящиеся к типу.
struct Step {
    std::string id;
    StepType type = StepType::Message;
    double start = 0;     // мс
    double duration = 1200;

    std::string from;     // message
    std::string to;
    std::string edgeId;   // message, link
    std::string variant = "request";

    std::string nodeId;   // timer, state, action, pulse
    std::string label;    // message, timer, state
    std::string text;     // note, action, link
    std::string color;    // state, note, action, link, pulse

    double seconds = 0;   // timer: отсчёт (0 → из длительности)
    std::string unit = "s";

    std::string state = "down";  // state

    std::optional<double> labelSize;  // state, link
    std::optional<double> labelPos;   // link
    std::optional<double> labelOff;   // link

    double x = 0;         // note
    double y = 0;

    std::string anim = "flow";  // link

    [[nodiscard]] double end() const { return start + duration; }
    friend bool operator==(const Step&, const Step&) = default;
};

struct View {
    double zoom = 1;
    double panX = 0;
    double panY = 0;
    friend bool operator==(const View&, const View&) = default;
};

struct Meta {
    std::string name = "Новая диаграмма";
    std::int64_t createdAt = 0;
    friend bool operator==(const Meta&, const Meta&) = default;
};

struct Scenario {
    double duration = 12000;    // мс — конец сцены
    bool userDuration = false;  // длительность задана вручную (не уменьшать автоматически)
    std::vector<Step> steps;
    friend bool operator==(const Scenario&, const Scenario&) = default;
};

struct Model {
    int version = 2;
    Meta meta;
    View view;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    Scenario scenario;

    [[nodiscard]] const Node* node(std::string_view id) const;
    [[nodiscard]] Node* node(std::string_view id);
    [[nodiscard]] const Edge* edge(std::string_view id) const;
    [[nodiscard]] Edge* edge(std::string_view id);
    [[nodiscard]] const Step* step(std::string_view id) const;
    [[nodiscard]] Step* step(std::string_view id);
    /// Любая связь между a и b (в любом направлении).
    [[nodiscard]] const Edge* edgeBetween(std::string_view a, std::string_view b) const;
    [[nodiscard]] const Port* port(std::string_view nodeId, std::string_view portId) const;

    friend bool operator==(const Model&, const Model&) = default;
};

struct Selection {
    enum class Kind { None, Node, Edge, Step };
    Kind kind = Kind::None;
    std::string id;

    [[nodiscard]] bool empty() const { return kind == Kind::None || id.empty(); }
    [[nodiscard]] bool is(Kind k, std::string_view i) const { return kind == k && id == i; }
    friend bool operator==(const Selection&, const Selection&) = default;
};

}  // namespace ad
