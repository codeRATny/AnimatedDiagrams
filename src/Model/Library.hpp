#ifndef _MODEL_LIBRARY_HPP_
#define _MODEL_LIBRARY_HPP_

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Model/Easing.hpp"
#include "Model/Step.hpp"
#include "Utils/I18n.hpp"

/// @file Library.hpp
/// @brief User-extensible definitions: element types, effects and animation templates.
///
/// Definitions come from three sources (in lookup order): the document itself,
/// installed plugins and the built-in set (see Registry.hpp).

namespace ad
{

/// Visual style of a node. Every field is optional: unset fields are taken from
/// the element type, and finally from the editor defaults.
struct NodeStyle
{
    std::optional<std::string> shape;        // see Shapes()
    std::optional<std::string> custom_path;  // SVG path data in the unit square (shape == "custom")
    std::optional<std::string> fill;         // body color in the default state
    std::optional<std::string> stroke;       // border color in the default state
    std::optional<double>      stroke_width; // border width
    std::optional<std::string> stroke_style; // solid | dashed | dotted
    std::optional<double>      corner_radius;
    std::optional<std::string> text_color;
    std::optional<double>      font_size; // title font size
    std::optional<double>      opacity;   // 0..1
    std::optional<bool>        shadow;
    std::optional<bool>        show_icon;

    /// Copy of *this where every field set in `over` replaces ours.
    [[nodiscard]] NodeStyle Merged(const NodeStyle &over) const;
    [[nodiscard]] bool      Empty() const;
    friend bool             operator==(const NodeStyle &, const NodeStyle &) = default;
};

struct EdgeStyle
{
    std::optional<std::string> color;
    std::optional<double>      width;
    std::optional<std::string> stroke_style; // solid | dashed | dotted
    std::optional<std::string> arrow_end;    // triangle | open | diamond | circle | none
    std::optional<std::string> arrow_start;
    std::optional<std::string> routing; // curved | straight | orthogonal
    std::optional<std::string> label_color;
    /// Copy of *this where every field set in `over` replaces ours.
    [[nodiscard]] EdgeStyle Merged(const EdgeStyle &over) const;
    friend bool             operator==(const EdgeStyle &, const EdgeStyle &) = default;
};

/// Element type: a reusable node template (a palette entry).
struct ElementType
{
    std::string id;
    std::string label;
    std::string icon;
    std::string category = Tr("library", "General");
    std::string description;
    std::string accent = "#4f8cff"; // accent stripe / palette color
    double      width  = 140;
    double      height = 64;
    NodeStyle   style;
    friend bool operator==(const ElementType &, const ElementType &) = default;
};

enum class EffectProperty
{
    Opacity, // multiplier, default 1
    Scale,   // multiplier, default 1
    Rotate,  // degrees, default 0
    OffsetX, // px, default 0
    OffsetY, // px, default 0
    Glow,    // ring intensity 0..1, default 0
    Tint     // blend of the body towards the effect color 0..1, default 0
};

std::string_view                ToString(EffectProperty p);
std::optional<EffectProperty>   EffectPropertyFromString(std::string_view s);
double                          DefaultValue(EffectProperty p);
std::span<const EffectProperty> EffectProperties();

struct Keyframe
{
    double      t                                              = 0; // 0..1 inside one repetition
    double      value                                          = 0;
    Easing      easing                                         = Easing::Linear; // curve from the previous keyframe to this one
    friend bool operator==(const Keyframe &, const Keyframe &) = default;
};

struct EffectTrack
{
    EffectProperty        property = EffectProperty::Opacity;
    std::vector<Keyframe> keys;
    friend bool           operator==(const EffectTrack &, const EffectTrack &) = default;
};

/// Keyframe effect applied to a node by an "effect" step.
struct EffectDef
{
    std::string              id;
    std::string              label;
    std::string              category = Tr("library", "General");
    std::string              description;
    std::string              color  = "#22d3ee"; // glow / tint color (a step may override)
    int                      repeat = 1;         // repetitions within the step duration
    std::vector<EffectTrack> tracks;
    friend bool              operator==(const EffectDef &, const EffectDef &) = default;
};

struct AnimationRole
{
    std::string id; // referenced from template steps (from / to / node_id)
    std::string label;
    friend bool operator==(const AnimationRole &, const AnimationRole &) = default;
};

/// Reusable sequence of steps with role placeholders instead of node ids.
/// Steps reference roles by id in from / to / node_id; a link step references
/// an edge as "roleA>roleB". Times are relative to the insertion point.
struct AnimationTemplate
{
    std::string                id;
    std::string                label;
    std::string                category = Tr("library", "General");
    std::string                description;
    std::vector<AnimationRole> roles;
    std::vector<Step>          steps;
    friend bool                operator==(const AnimationTemplate &, const AnimationTemplate &) = default;
};

/// Colors of a node state (see NodeStates()) inside a design system.
struct StateColors
{
    std::optional<std::string> fill;
    std::optional<std::string> ring;
    friend bool                operator==(const StateColors &, const StateColors &) = default;
};

/// Design system overrides for one element type (applied above the type style).
struct ElementOverride
{
    std::optional<std::string> accent;
    NodeStyle                  style;
    friend bool                operator==(const ElementOverride &, const ElementOverride &) = default;
};

/// A design system: color tokens, typography and default styles applied to a whole
/// document. Colors anywhere in styles and steps may reference a token as "$name"
/// (e.g. "$primary"); unknown tokens fall back to the standard set (see DefaultTokens()).
struct DesignSystem
{
    std::string id;
    std::string label;
    std::string category = Tr("library", "General");
    std::string description;

    // canvas (copied into the scene settings when the design system is applied)
    std::optional<std::string> background;
    std::optional<std::string> edge_color;
    std::optional<std::string> text_color;
    std::optional<bool>        grid;
    std::optional<double>      grid_size;

    std::string                        font_family;    // empty -- the application font
    std::optional<std::string>         subtitle_color; // node second line
    std::map<std::string, std::string> colors;         // token name -> #rrggbb[aa]
    std::map<std::string, StateColors> states;         // node state id -> colors
    std::map<std::string, std::string> variants;       // message variant id -> color

    NodeStyle                              node; // below the element type styles
    EdgeStyle                              edge; // below the edge styles
    std::map<std::string, ElementOverride> elements;

    friend bool operator==(const DesignSystem &, const DesignSystem &) = default;
};

/// Standard token names with their values in the default (dark) look:
/// surface, surface-alt, border, text, muted, primary, secondary, success, warning,
/// danger, info, accent.
const std::map<std::string, std::string> &DefaultTokens();

/// "$name" -> token value from `ds` (or DefaultTokens()); other strings are returned as is.
std::string ResolveColorToken(std::string_view value, const DesignSystem *ds);

/// A set of definitions: the document library, a plugin or the built-ins.
struct LibrarySet
{
    std::vector<ElementType>       elements;
    std::vector<EffectDef>         effects;
    std::vector<AnimationTemplate> animations;
    std::vector<DesignSystem>      design_systems;

    [[nodiscard]] bool Empty() const { return elements.empty() && effects.empty() && animations.empty() && design_systems.empty(); }
    [[nodiscard]] const ElementType       *Element(std::string_view id) const;
    [[nodiscard]] const EffectDef         *Effect(std::string_view id) const;
    [[nodiscard]] const AnimationTemplate *Animation(std::string_view id) const;
    [[nodiscard]] const DesignSystem      *Design(std::string_view id) const;
    /// Insert or replace (by id).
    void        Upsert(const ElementType &e);
    void        Upsert(const EffectDef &e);
    void        Upsert(const AnimationTemplate &a);
    void        Upsert(const DesignSystem &d);
    friend bool operator==(const LibrarySet &, const LibrarySet &) = default;
};

/// Built-in element types, effects and animation templates.
const LibrarySet &BuiltinLibrary();

} // namespace ad

#endif // _MODEL_LIBRARY_HPP_
