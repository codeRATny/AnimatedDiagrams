#include "Catalog.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "Utils/I18n.hpp"

namespace ad
{

namespace
{

// built on first use: labels are translated with the UI language installed at startup
std::string T(const char *text) { return Tr("catalog", text); }

const std::vector<NodeStateInfo> &NodeStateTable()
{
    static const std::vector<NodeStateInfo> kTable{
        {"ok", T(AD_TR_NOOP("catalog", "Normal")), Color::Rgb(0x3b4a63), Color::Rgb(0x5a6b86)},
        {"active", T(AD_TR_NOOP("catalog", "Active")), Color::Rgb(0x1d4ed8), Color::Rgb(0x60a5fa)},
        {"busy", T(AD_TR_NOOP("catalog", "Busy")), Color::Rgb(0xa16207), Color::Rgb(0xf59e0b)},
        {"warn", T(AD_TR_NOOP("catalog", "Warning")), Color::Rgb(0xb45309), Color::Rgb(0xfb923c)},
        {"down", T(AD_TR_NOOP("catalog", "Down")), Color::Rgb(0x7f1d1d), Color::Rgb(0xef4444)},
        {"success", T(AD_TR_NOOP("catalog", "Success")), Color::Rgb(0x166534), Color::Rgb(0x22c55e)},
        {"disabled", T(AD_TR_NOOP("catalog", "Disabled")), Color::Rgb(0x1f2937), Color::Rgb(0x4b5563)},
    };
    return kTable;
}

const std::vector<MsgVariantInfo> &MsgVariantTable()
{
    static const std::vector<MsgVariantInfo> kTable{
        {"request", T(AD_TR_NOOP("catalog", "Request")), Color::Rgb(0x60a5fa)},
        {"response", T(AD_TR_NOOP("catalog", "Response")), Color::Rgb(0x34d399)},
        {"retry", T(AD_TR_NOOP("catalog", "Retry")), Color::Rgb(0xfbbf24), 6, 5},
        {"error", T(AD_TR_NOOP("catalog", "Error")), Color::Rgb(0xf87171), 2, 5},
        {"success", T(AD_TR_NOOP("catalog", "Success")), Color::Rgb(0x4ade80)},
        {"event", T(AD_TR_NOOP("catalog", "Event")), Color::Rgb(0xc084fc), 1, 6},
    };
    return kTable;
}

const std::vector<LinkAnimInfo> &LinkAnimTable()
{
    static const std::vector<LinkAnimInfo> kTable{
        {"flow", T(AD_TR_NOOP("catalog", "Running dashes")), true, true, false},
        {"dash", T(AD_TR_NOOP("catalog", "Dashed")), true, false, false},
        {"solid", T(AD_TR_NOOP("catalog", "Solid")), false, false, false},
        {"pulse", T(AD_TR_NOOP("catalog", "Pulse")), false, false, true},
    };
    return kTable;
}

const std::vector<TimeUnitInfo> &TimeUnitTable()
{
    static const std::vector<TimeUnitInfo> kTable{
        {"s", T(AD_TR_NOOP("catalog", "Seconds")), T(AD_TR_NOOP("catalog", "s"))},
        {"m", T(AD_TR_NOOP("catalog", "Minutes")), T(AD_TR_NOOP("catalog", "min"))},
        {"h", T(AD_TR_NOOP("catalog", "Hours")), T(AD_TR_NOOP("catalog", "h"))},
        {"d", T(AD_TR_NOOP("catalog", "Days")), T(AD_TR_NOOP("catalog", "d"))},
    };
    return kTable;
}

const std::vector<StepTypeInfo> &StepTypeTable()
{
    static const std::vector<StepTypeInfo> kTable{
        {StepType::Message, "message", T(AD_TR_NOOP("catalog", "Message"))},
        {StepType::Timer, "timer", T(AD_TR_NOOP("catalog", "Timer"))},
        {StepType::State, "state", T(AD_TR_NOOP("catalog", "State change"))},
        {StepType::Action, "action", T(AD_TR_NOOP("catalog", "Action"))},
        {StepType::Link, "link", T(AD_TR_NOOP("catalog", "Edge animation"))},
        {StepType::Note, "note", T(AD_TR_NOOP("catalog", "Note"))},
        {StepType::Effect, "effect", T(AD_TR_NOOP("catalog", "Effect"))},
    };
    return kTable;
}

const std::vector<OptionInfo> &ShapeTable()
{
    static const std::vector<OptionInfo> kTable{
        {"rounded", T(AD_TR_NOOP("catalog", "Rounded"))},
        {"rect", T(AD_TR_NOOP("catalog", "Rectangle"))},
        {"ellipse", T(AD_TR_NOOP("catalog", "Ellipse"))},
        {"diamond", T(AD_TR_NOOP("catalog", "Diamond"))},
        {"hexagon", T(AD_TR_NOOP("catalog", "Hexagon"))},
        {"parallelogram", T(AD_TR_NOOP("catalog", "Parallelogram"))},
        {"cylinder", T(AD_TR_NOOP("catalog", "Cylinder (DB)"))},
        {"queue", T(AD_TR_NOOP("catalog", "Queue"))},
        {"document", T(AD_TR_NOOP("catalog", "Document"))},
        {"cloud", T(AD_TR_NOOP("catalog", "Cloud"))},
        {"note", T(AD_TR_NOOP("catalog", "Note"))},
        {"custom", T(AD_TR_NOOP("catalog", "Custom outline (SVG)"))},
    };
    return kTable;
}

const std::vector<OptionInfo> &ArrowHeadTable()
{
    static const std::vector<OptionInfo> kTable{
        {"triangle", T(AD_TR_NOOP("catalog", "Triangle"))}, {"open", T(AD_TR_NOOP("catalog", "Open"))},
        {"diamond", T(AD_TR_NOOP("catalog", "Diamond"))},   {"circle", T(AD_TR_NOOP("catalog", "Circle"))},
        {"none", T(AD_TR_NOOP("catalog", "None"))},
    };
    return kTable;
}

const std::vector<OptionInfo> &StrokeStyleTable()
{
    static const std::vector<OptionInfo> kTable{
        {"solid", T(AD_TR_NOOP("catalog", "Solid"))},
        {"dashed", T(AD_TR_NOOP("catalog", "Dashed"))},
        {"dotted", T(AD_TR_NOOP("catalog", "Dotted"))},
    };
    return kTable;
}

const std::vector<OptionInfo> &PacketShapeTable()
{
    static const std::vector<OptionInfo> kTable{
        {"capsule", T(AD_TR_NOOP("catalog", "Capsule"))},   {"dot", T(AD_TR_NOOP("catalog", "Dot"))},
        {"square", T(AD_TR_NOOP("catalog", "Square"))},     {"diamond", T(AD_TR_NOOP("catalog", "Diamond"))},
        {"envelope", T(AD_TR_NOOP("catalog", "Envelope"))}, {"arrow", T(AD_TR_NOOP("catalog", "Arrow"))},
    };
    return kTable;
}

const std::vector<OptionInfo> &RoutingTable()
{
    static const std::vector<OptionInfo> kTable{
        {"curved", T(AD_TR_NOOP("catalog", "Curved"))},
        {"straight", T(AD_TR_NOOP("catalog", "Straight"))},
        {"orthogonal", T(AD_TR_NOOP("catalog", "Orthogonal"))},
    };
    return kTable;
}

template <class Range>
const auto &FindOr(const Range &r, std::string_view id)
{
    const auto it = std::ranges::find(r, id, &std::ranges::range_value_t<Range>::id);
    return it != std::ranges::end(r) ? *it : *std::ranges::begin(r);
}

const StepTypeInfo &StepInfo(StepType t) { return *std::ranges::find(StepTypeTable(), t, &StepTypeInfo::type); }

} // namespace

std::span<const NodeStateInfo>  NodeStates() { return NodeStateTable(); }
std::span<const MsgVariantInfo> MsgVariants() { return MsgVariantTable(); }
std::span<const LinkAnimInfo>   LinkAnims() { return LinkAnimTable(); }
std::span<const TimeUnitInfo>   TimeUnits() { return TimeUnitTable(); }
std::span<const StepTypeInfo>   StepTypes() { return StepTypeTable(); }
std::span<const OptionInfo>     Shapes() { return ShapeTable(); }
std::span<const OptionInfo>     ArrowHeads() { return ArrowHeadTable(); }
std::span<const OptionInfo>     StrokeStyles() { return StrokeStyleTable(); }
std::span<const OptionInfo>     PacketShapes() { return PacketShapeTable(); }
std::span<const OptionInfo>     Routings() { return RoutingTable(); }

const NodeStateInfo  &NodeState(std::string_view id) { return FindOr(NodeStateTable(), id); }
const MsgVariantInfo &MsgVariant(std::string_view id) { return FindOr(MsgVariantTable(), id); }
const LinkAnimInfo   &LinkAnim(std::string_view id) { return FindOr(LinkAnimTable(), id); }
const TimeUnitInfo   &TimeUnit(std::string_view id) { return FindOr(TimeUnitTable(), id); }

bool IsKnownOption(std::span<const OptionInfo> options, std::string_view id)
{
    return std::ranges::any_of(options,
                               [id](const OptionInfo &o)
                               {
                                   return o.id == id;
                               });
}

std::string_view   ToString(StepType t) { return StepInfo(t).id; }
const std::string &StepTypeLabel(StepType t) { return StepInfo(t).label; }

std::optional<StepType> StepTypeFromString(std::string_view s)
{
    if (s == "pulse") // legacy (v1/v2 files): pulse step is now the "pulse" effect
    {
        return StepType::Effect;
    }
    const auto &table = StepTypeTable();
    const auto  it    = std::ranges::find(table, s, &StepTypeInfo::id);
    if (it == table.end())
    {
        return std::nullopt;
    }
    return it->type;
}

} // namespace ad
