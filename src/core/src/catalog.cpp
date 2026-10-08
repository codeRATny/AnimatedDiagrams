#include "ad/catalog.hpp"

#include <algorithm>
#include <array>

namespace ad {

namespace {

constexpr std::array kNodeKinds{
    NodeKindInfo{"service", "Сервис", Color::rgb(0x4f8cff), "round", "▢"},
    NodeKindInfo{"client", "Клиент", Color::rgb(0x22c55e), "round", "◎"},
    NodeKindInfo{"db", "БД", Color::rgb(0xa855f7), "db", "◫"},
    NodeKindInfo{"queue", "Очередь", Color::rgb(0xf59e0b), "queue", "≣"},
    NodeKindInfo{"gateway", "Шлюз", Color::rgb(0x06b6d4), "round", "◇"},
    NodeKindInfo{"external", "Внешний", Color::rgb(0x94a3b8), "round", "◈"},
};

constexpr std::array kNodeStates{
    NodeStateInfo{"ok", "Норма", Color::rgb(0x3b4a63), Color::rgb(0x5a6b86)},
    NodeStateInfo{"active", "Активен", Color::rgb(0x1d4ed8), Color::rgb(0x60a5fa)},
    NodeStateInfo{"busy", "Занят", Color::rgb(0xa16207), Color::rgb(0xf59e0b)},
    NodeStateInfo{"warn", "Предупреждение", Color::rgb(0xb45309), Color::rgb(0xfb923c)},
    NodeStateInfo{"down", "Недоступен", Color::rgb(0x7f1d1d), Color::rgb(0xef4444)},
    NodeStateInfo{"success", "Успех", Color::rgb(0x166534), Color::rgb(0x22c55e)},
};

constexpr std::array kMsgVariants{
    MsgVariantInfo{"request", "Запрос", Color::rgb(0x60a5fa)},
    MsgVariantInfo{"response", "Ответ", Color::rgb(0x34d399)},
    MsgVariantInfo{"retry", "Ретрай", Color::rgb(0xfbbf24), 6, 5},
    MsgVariantInfo{"error", "Ошибка", Color::rgb(0xf87171), 2, 5},
    MsgVariantInfo{"success", "Успех", Color::rgb(0x4ade80)},
    MsgVariantInfo{"event", "Событие", Color::rgb(0xc084fc), 1, 6},
};

constexpr std::array kLinkAnims{
    LinkAnimInfo{"flow", "Бегущий пунктир", true, true, false},
    LinkAnimInfo{"dash", "Пунктир", true, false, false},
    LinkAnimInfo{"solid", "Сплошная", false, false, false},
    LinkAnimInfo{"pulse", "Пульсация", false, false, true},
};

constexpr std::array kTimeUnits{
    TimeUnitInfo{"s", "Секунды", "с"},
    TimeUnitInfo{"m", "Минуты", "мин"},
    TimeUnitInfo{"h", "Часы", "ч"},
    TimeUnitInfo{"d", "Дни", "дн"},
};

constexpr std::array kStepTypes{
    StepTypeInfo{StepType::Message, "message", "Сообщение"},
    StepTypeInfo{StepType::Timer, "timer", "Таймер"},
    StepTypeInfo{StepType::State, "state", "Смена состояния"},
    StepTypeInfo{StepType::Action, "action", "Действие"},
    StepTypeInfo{StepType::Link, "link", "Соединение"},
    StepTypeInfo{StepType::Note, "note", "Заметка"},
    StepTypeInfo{StepType::Pulse, "pulse", "Пульс/подсветка"},
};

template <class Range>
const auto& findOr(const Range& r, std::string_view id) {
    const auto it = std::ranges::find(r, id, &std::ranges::range_value_t<Range>::id);
    return it != std::ranges::end(r) ? *it : *std::ranges::begin(r);
}

const StepTypeInfo& stepInfo(StepType t) { return *std::ranges::find(kStepTypes, t, &StepTypeInfo::type); }

}  // namespace

std::span<const NodeKindInfo> nodeKinds() { return kNodeKinds; }
std::span<const NodeStateInfo> nodeStates() { return kNodeStates; }
std::span<const MsgVariantInfo> msgVariants() { return kMsgVariants; }
std::span<const LinkAnimInfo> linkAnims() { return kLinkAnims; }
std::span<const TimeUnitInfo> timeUnits() { return kTimeUnits; }
std::span<const StepTypeInfo> stepTypes() { return kStepTypes; }

const NodeKindInfo& nodeKind(std::string_view id) { return findOr(kNodeKinds, id); }
const NodeStateInfo& nodeState(std::string_view id) { return findOr(kNodeStates, id); }
const MsgVariantInfo& msgVariant(std::string_view id) { return findOr(kMsgVariants, id); }
const LinkAnimInfo& linkAnim(std::string_view id) { return findOr(kLinkAnims, id); }
const TimeUnitInfo& timeUnit(std::string_view id) { return findOr(kTimeUnits, id); }

std::string_view toString(StepType t) { return stepInfo(t).id; }
std::string_view stepTypeLabel(StepType t) { return stepInfo(t).label; }

std::optional<StepType> stepTypeFromString(std::string_view s) {
    const auto it = std::ranges::find(kStepTypes, s, &StepTypeInfo::id);
    if (it == kStepTypes.end()) return std::nullopt;
    return it->type;
}

}  // namespace ad
