#pragma once
// Попадание курсора в элементы диаграммы (мировые координаты).

#include <cstddef>
#include <string>

#include "ad/model.hpp"

namespace ad {

struct Hit {
    enum class Kind { None, Node, Edge, Port, Waypoint };
    Kind kind = Kind::None;
    std::string id;      // node / edge id
    std::string portId;  // для Port
    std::size_t waypoint = 0;

    [[nodiscard]] bool empty() const { return kind == Kind::None; }
};

/// Приоритет: порты > точки изгиба выделенной связи > узлы (верхний) > связи.
/// tolerance — допуск в мировых единицах (обычно px / zoom).
Hit hitTest(const Model& m, Vec2 p, const Selection& sel, double tolerance);

}  // namespace ad
