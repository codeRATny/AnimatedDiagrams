#pragma once
// Детерминированный движок: всё, что видно в момент t, — чистая функция от (модель, t).

#include <optional>
#include <string>
#include <vector>

#include "ad/color.hpp"
#include "ad/geometry.hpp"
#include "ad/model.hpp"

namespace ad {

/// Длина маркера-стрелки в мировых единицах.
inline constexpr double kArrowLen = 12;

/// Действующий state-шаг узла в момент t (или nullptr — состояние по умолчанию).
/// Шаг действует на [start, end); если активны несколько — побеждает начавшийся позже.
/// Шаг, доживающий до конца сцены, действует и в сам момент t == конец сцены
/// (иначе на последнем кадре состояние «сбрасывается»).
const Step* stateAt(const Model& m, std::string_view nodeId, double t);

/// Шаги, активные в момент t (start <= t <= end), в порядке сценария.
std::vector<const Step*> activeSteps(const Model& m, double t);

struct ResolvedState {
    std::string id = "ok";
    std::string label;
    Color fill;
    Color ring;
    std::optional<double> labelSize;
    bool customLabel = false;
};

/// Пресет состояния + переопределения шага (подпись, цвет-акцент, размер шрифта).
ResolvedState resolveNodeState(const Step* step);

struct EdgeGeometry {
    Vec2 start;     // фактическое начало линии (с отступом под стрелку)
    Vec2 end;       // фактический конец линии (основание стрелки)
    Vec2 rawStart;  // точка, куда указывает стрелка в начале
    Vec2 rawEnd;    // кончик стрелки на конце
    std::vector<Vec2> points;  // start, waypoints..., end
    Path path;
};

/// Геометрия связи с учётом портов, точек изгиба и стрелок; nullopt, если нет узлов.
std::optional<EdgeGeometry> edgeGeometry(const Model& m, const Edge& e);

/// Индекс для вставки точки изгиба: ближайший к p сегмент ломаной связи.
std::size_t nearestSegmentIndex(const Model& m, const Edge& e, Vec2 p);

}  // namespace ad
