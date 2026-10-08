#pragma once
// Справочники: типы узлов, состояния, варианты сообщений, анимации связи, единицы времени.

#include <optional>
#include <span>
#include <string_view>

#include "ad/color.hpp"

namespace ad {

enum class StepType { Message, Timer, State, Action, Link, Note, Pulse };

struct NodeKindInfo {
    std::string_view id;
    std::string_view label;
    Color color;
    std::string_view shape;  // round | db | queue
    std::string_view icon;
};

struct NodeStateInfo {
    std::string_view id;
    std::string_view label;
    Color fill;
    Color ring;
};

struct MsgVariantInfo {
    std::string_view id;
    std::string_view label;
    Color color;
    double dash = 0;  // 0 — сплошная линия, иначе пунктир "dash gap"
    double gap = 0;
};

struct LinkAnimInfo {
    std::string_view id;
    std::string_view label;
    bool dashed = false;
    bool flow = false;
    bool pulse = false;
};

struct TimeUnitInfo {
    std::string_view id;
    std::string_view label;
    std::string_view shortLabel;
};

struct StepTypeInfo {
    StepType type;
    std::string_view id;
    std::string_view label;
};

std::span<const NodeKindInfo> nodeKinds();
std::span<const NodeStateInfo> nodeStates();
std::span<const MsgVariantInfo> msgVariants();
std::span<const LinkAnimInfo> linkAnims();
std::span<const TimeUnitInfo> timeUnits();
std::span<const StepTypeInfo> stepTypes();

/// Поиск по id с запасным значением (первый элемент справочника).
const NodeKindInfo& nodeKind(std::string_view id);
const NodeStateInfo& nodeState(std::string_view id);
const MsgVariantInfo& msgVariant(std::string_view id);
const LinkAnimInfo& linkAnim(std::string_view id);
const TimeUnitInfo& timeUnit(std::string_view id);

std::string_view toString(StepType t);
std::string_view stepTypeLabel(StepType t);
std::optional<StepType> stepTypeFromString(std::string_view s);

}  // namespace ad
