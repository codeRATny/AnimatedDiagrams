#include "Catalog.hpp"

#include <algorithm>
#include <array>

namespace ad
{

namespace
{

constexpr std::array kNodeStates{
    NodeStateInfo{"ok", "Норма", Color::Rgb(0x3b4a63), Color::Rgb(0x5a6b86)},
    NodeStateInfo{"active", "Активен", Color::Rgb(0x1d4ed8), Color::Rgb(0x60a5fa)},
    NodeStateInfo{"busy", "Занят", Color::Rgb(0xa16207), Color::Rgb(0xf59e0b)},
    NodeStateInfo{"warn", "Предупреждение", Color::Rgb(0xb45309), Color::Rgb(0xfb923c)},
    NodeStateInfo{"down", "Недоступен", Color::Rgb(0x7f1d1d), Color::Rgb(0xef4444)},
    NodeStateInfo{"success", "Успех", Color::Rgb(0x166534), Color::Rgb(0x22c55e)},
    NodeStateInfo{"disabled", "Отключён", Color::Rgb(0x1f2937), Color::Rgb(0x4b5563)},
};

constexpr std::array kMsgVariants{
    MsgVariantInfo{"request", "Запрос", Color::Rgb(0x60a5fa)},     MsgVariantInfo{"response", "Ответ", Color::Rgb(0x34d399)},
    MsgVariantInfo{"retry", "Ретрай", Color::Rgb(0xfbbf24), 6, 5}, MsgVariantInfo{"error", "Ошибка", Color::Rgb(0xf87171), 2, 5},
    MsgVariantInfo{"success", "Успех", Color::Rgb(0x4ade80)},      MsgVariantInfo{"event", "Событие", Color::Rgb(0xc084fc), 1, 6},
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
    StepTypeInfo{StepType::Message, "message", "Сообщение"},   StepTypeInfo{StepType::Timer, "timer", "Таймер"},
    StepTypeInfo{StepType::State, "state", "Смена состояния"}, StepTypeInfo{StepType::Action, "action", "Действие"},
    StepTypeInfo{StepType::Link, "link", "Соединение"},        StepTypeInfo{StepType::Note, "note", "Заметка"},
    StepTypeInfo{StepType::Effect, "effect", "Эффект"},
};

constexpr std::array kShapes{
    OptionInfo{"rounded", "Скруглённый"},   OptionInfo{"rect", "Прямоугольник"},    OptionInfo{"ellipse", "Эллипс"},
    OptionInfo{"diamond", "Ромб"},          OptionInfo{"hexagon", "Шестиугольник"}, OptionInfo{"parallelogram", "Параллелограмм"},
    OptionInfo{"cylinder", "Цилиндр (БД)"}, OptionInfo{"queue", "Очередь"},         OptionInfo{"document", "Документ"},
    OptionInfo{"cloud", "Облако"},          OptionInfo{"note", "Заметка"},          OptionInfo{"custom", "Свой контур (SVG)"},
};

constexpr std::array kArrowHeads{
    OptionInfo{"triangle", "Треугольник"}, OptionInfo{"open", "Открытая"}, OptionInfo{"diamond", "Ромб"},
    OptionInfo{"circle", "Круг"},          OptionInfo{"none", "Нет"},
};

constexpr std::array kStrokeStyles{
    OptionInfo{"solid", "Сплошная"},
    OptionInfo{"dashed", "Пунктир"},
    OptionInfo{"dotted", "Точки"},
};

constexpr std::array kPacketShapes{
    OptionInfo{"capsule", "Капсула"}, OptionInfo{"dot", "Точка"},        OptionInfo{"square", "Квадрат"},
    OptionInfo{"diamond", "Ромб"},    OptionInfo{"envelope", "Конверт"}, OptionInfo{"arrow", "Стрелка"},
};

constexpr std::array kRoutings{
    OptionInfo{"curved", "Кривая"},
    OptionInfo{"straight", "Прямая"},
    OptionInfo{"orthogonal", "Ортогональная"},
};

template <class Range>
const auto &FindOr(const Range &r, std::string_view id)
{
    const auto it = std::ranges::find(r, id, &std::ranges::range_value_t<Range>::id);
    return it != std::ranges::end(r) ? *it : *std::ranges::begin(r);
}

const StepTypeInfo &StepInfo(StepType t) { return *std::ranges::find(kStepTypes, t, &StepTypeInfo::type); }

} // namespace

std::span<const NodeStateInfo>  NodeStates() { return kNodeStates; }
std::span<const MsgVariantInfo> MsgVariants() { return kMsgVariants; }
std::span<const LinkAnimInfo>   LinkAnims() { return kLinkAnims; }
std::span<const TimeUnitInfo>   TimeUnits() { return kTimeUnits; }
std::span<const StepTypeInfo>   StepTypes() { return kStepTypes; }
std::span<const OptionInfo>     Shapes() { return kShapes; }
std::span<const OptionInfo>     ArrowHeads() { return kArrowHeads; }
std::span<const OptionInfo>     StrokeStyles() { return kStrokeStyles; }
std::span<const OptionInfo>     PacketShapes() { return kPacketShapes; }
std::span<const OptionInfo>     Routings() { return kRoutings; }

const NodeStateInfo  &NodeState(std::string_view id) { return FindOr(kNodeStates, id); }
const MsgVariantInfo &MsgVariant(std::string_view id) { return FindOr(kMsgVariants, id); }
const LinkAnimInfo   &LinkAnim(std::string_view id) { return FindOr(kLinkAnims, id); }
const TimeUnitInfo   &TimeUnit(std::string_view id) { return FindOr(kTimeUnits, id); }

bool IsKnownOption(std::span<const OptionInfo> options, std::string_view id)
{
    return std::ranges::any_of(options,
                               [id](const OptionInfo &o)
                               {
                                   return o.id == id;
                               });
}

std::string_view ToString(StepType t) { return StepInfo(t).id; }
std::string_view StepTypeLabel(StepType t) { return StepInfo(t).label; }

std::optional<StepType> StepTypeFromString(std::string_view s)
{
    if (s == "pulse") // legacy (v1/v2 files): pulse step is now the "pulse" effect
    {
        return StepType::Effect;
    }
    const auto it = std::ranges::find(kStepTypes, s, &StepTypeInfo::id);
    if (it == kStepTypes.end())
    {
        return std::nullopt;
    }
    return it->type;
}

} // namespace ad
