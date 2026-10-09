#ifndef _MODEL_CATALOG_HPP_
#define _MODEL_CATALOG_HPP_

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "Model/Color.hpp"

/// @file Catalog.hpp
/// @brief Fixed enumerations of the editor: node states, message variants, link
///        animations, time units, step types, shapes, arrow heads, packet shapes.
///
/// Extensible definitions (element types, effects, animation templates) live in
/// Library.hpp and are resolved through the Registry.

namespace ad
{

enum class StepType
{
    Message,
    Timer,
    State,
    Action,
    Link,
    Note,
    Effect
};

struct NodeStateInfo
{
    std::string_view id;
    std::string      label; // in the UI language
    Color            fill;
    Color            ring;
};

struct MsgVariantInfo
{
    std::string_view id;
    std::string      label;
    Color            color;
    double           dash = 0; // 0 -- solid line, otherwise "dash gap"
    double           gap  = 0;
};

struct LinkAnimInfo
{
    std::string_view id;
    std::string      label;
    bool             dashed = false;
    bool             flow   = false;
    bool             pulse  = false;
};

struct TimeUnitInfo
{
    std::string_view id;
    std::string      label;
    std::string      short_label;
};

struct StepTypeInfo
{
    StepType         type;
    std::string_view id;
    std::string      label;
};

/// Simple (id, label) option used for shapes, arrows, stroke styles, packets, routing.
struct OptionInfo
{
    std::string_view id;
    std::string      label;
};

std::span<const NodeStateInfo>  NodeStates();
std::span<const MsgVariantInfo> MsgVariants();
std::span<const LinkAnimInfo>   LinkAnims();
std::span<const TimeUnitInfo>   TimeUnits();
std::span<const StepTypeInfo>   StepTypes();
std::span<const OptionInfo>     Shapes();       // rounded, rect, ellipse, diamond, ... custom
std::span<const OptionInfo>     ArrowHeads();   // triangle, open, diamond, circle, none
std::span<const OptionInfo>     StrokeStyles(); // solid, dashed, dotted
std::span<const OptionInfo>     PacketShapes(); // capsule, dot, square, diamond, envelope, arrow
std::span<const OptionInfo>     Routings();     // curved, straight, orthogonal

/// Lookup by id; unknown ids fall back to the first entry of the catalog.
const NodeStateInfo  &NodeState(std::string_view id);
const MsgVariantInfo &MsgVariant(std::string_view id);
const LinkAnimInfo   &LinkAnim(std::string_view id);
const TimeUnitInfo   &TimeUnit(std::string_view id);
bool                  IsKnownOption(std::span<const OptionInfo> options, std::string_view id);

std::string_view        ToString(StepType t);
const std::string      &StepTypeLabel(StepType t);
std::optional<StepType> StepTypeFromString(std::string_view s);

} // namespace ad

#endif // _MODEL_CATALOG_HPP_
