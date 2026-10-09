#ifndef _MODEL_MODEL_HPP_
#define _MODEL_MODEL_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Geometry/Geometry.hpp"
#include "Model/Library.hpp"
#include "Model/Step.hpp"

/// @file Model.hpp
/// @brief Document model: nodes, edges, the scenario and the embedded library.
///        The JSON format is backward compatible with the web version (see JsonIo.hpp).

namespace ad
{

inline constexpr int    kModelVersion  = 3;
inline constexpr double kDefaultNodeW  = 140;
inline constexpr double kDefaultNodeH  = 64;
inline constexpr char   kDefaultType[] = "service";

struct Port
{
    std::string id;
    double      dx                                     = 0; // offset from the node's top-left corner
    double      dy                                     = 0;
    friend bool operator==(const Port &, const Port &) = default;
};

struct Node
{
    std::string       id;
    std::string       label;
    std::string       type = kDefaultType; // element type id (JSON key "kind")
    std::string       subtitle;
    std::string       accent; // accent stripe color; empty -- from the element type
    double            x = 0;
    double            y = 0;
    double            w = kDefaultNodeW;
    double            h = kDefaultNodeH;
    std::vector<Port> ports;
    NodeStyle         style; // per-node overrides of the element type style

    [[nodiscard]] Rect        Bounds() const { return {x, y, w, h}; }
    [[nodiscard]] Vec2        Center() const { return {x + w / 2, y + h / 2}; }
    [[nodiscard]] const Port *FindPort(std::string_view port_id) const;
    [[nodiscard]] Port       *FindPort(std::string_view port_id);
    friend bool               operator==(const Node &, const Node &) = default;
};

struct Edge
{
    std::string           id;
    std::string           from;
    std::string           to;
    std::string           from_port; // empty -- automatic (node border)
    std::string           to_port;
    std::string           label;
    double                curve = 0;
    std::vector<Vec2>     waypoints;
    std::optional<double> label_pos;
    std::optional<double> label_off;
    std::optional<double> label_size;
    EdgeStyle             style;
    friend bool           operator==(const Edge &, const Edge &) = default;
};

struct View
{
    double      zoom                                   = 1;
    double      pan_x                                  = 0;
    double      pan_y                                  = 0;
    friend bool operator==(const View &, const View &) = default;
};

struct Meta
{
    std::string name = "Новая диаграмма";
    std::string description;
    int64_t     created_at                             = 0;
    friend bool operator==(const Meta &, const Meta &) = default;
};

/// Scene-wide settings (canvas and export defaults).
struct SceneSettings
{
    std::string background                                               = "#0a111f";
    bool        grid                                                     = true;
    double      grid_size                                                = 26;
    std::string edge_color                                               = "#5f7196";
    std::string text_color                                               = "#f2f6ff";
    friend bool operator==(const SceneSettings &, const SceneSettings &) = default;
};

struct Scenario
{
    double            duration      = 12000; // ms -- end of the scene
    bool              user_duration = false; // set by the user (never shrink automatically)
    std::vector<Step> steps;
    friend bool       operator==(const Scenario &, const Scenario &) = default;
};

struct Model
{
    int               version = kModelVersion;
    Meta              meta;
    View              view;
    SceneSettings     scene;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    Scenario          scenario;
    LibrarySet        library;       // definitions created in / embedded into this document
    std::string       design_system; // active design system id (empty -- none)

    [[nodiscard]] const Node *FindNode(std::string_view id) const;
    [[nodiscard]] Node       *FindNode(std::string_view id);
    [[nodiscard]] const Edge *FindEdge(std::string_view id) const;
    [[nodiscard]] Edge       *FindEdge(std::string_view id);
    [[nodiscard]] const Step *FindStep(std::string_view id) const;
    [[nodiscard]] Step       *FindStep(std::string_view id);
    /// Any edge between a and b (either direction).
    [[nodiscard]] const Edge *EdgeBetween(std::string_view a, std::string_view b) const;
    [[nodiscard]] const Port *FindPort(std::string_view node_id, std::string_view port_id) const;

    friend bool operator==(const Model &, const Model &) = default;
};

struct Selection
{
    enum class Kind
    {
        None,
        Node,
        Edge,
        Step
    };
    Kind        kind = Kind::None;
    std::string id;

    [[nodiscard]] bool Empty() const { return kind == Kind::None || id.empty(); }
    [[nodiscard]] bool Is(Kind k, std::string_view i) const { return kind == k && id == i; }
    friend bool        operator==(const Selection &, const Selection &) = default;
};

} // namespace ad

#endif // _MODEL_MODEL_HPP_
