#pragma once
// Логика таймлайна без UI: упаковка шагов по дорожкам, деления шкалы, подписи.

#include <string>
#include <vector>

#include "ad/color.hpp"
#include "ad/model.hpp"

namespace ad {

struct LaneAssignment {
    std::string stepId;
    int lane = 0;
};

/// Непересекающиеся во времени шаги — на одну дорожку, параллельные — на разные.
std::vector<LaneAssignment> packLanes(const std::vector<Step>& steps);
int laneCount(const std::vector<LaneAssignment>& lanes);

/// «Круглые» деления на отрезке [start, stop] (как d3.ticks).
std::vector<double> niceTicks(double start, double stop, int count);
double tickStep(double start, double stop, int count);
/// Подпись деления с точностью, соответствующей шагу.
std::string formatTick(double value, double step);

/// Конец видимой шкалы: max(длительность сцены, конец последнего шага).
double contentEnd(const Model& m);

std::string stepTitle(const Model& m, const Step& s);
Color stepColor(const Step& s);

/// Привязка ко времени с шагом grid (мс), не меньше 0.
double snapTime(double ms, double grid = 50);

/// "1.25с" — как в веб-версии.
std::string formatTime(double ms);

}  // namespace ad
