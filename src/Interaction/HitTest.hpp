#ifndef _INTERACTION_HIT_TEST_HPP_
#define _INTERACTION_HIT_TEST_HPP_

#include <cstddef>
#include <string>

#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file HitTest.hpp
/// @brief Picking diagram elements under the cursor (world coordinates).

namespace ad
{

struct Hit
{
    enum class Kind
    {
        None,
        Node,
        Edge,
        Port,
        Waypoint
    };
    Kind        kind = Kind::None;
    std::string id;      // node / edge id
    std::string port_id; // for Port
    size_t      waypoint = 0;

    [[nodiscard]] bool Empty() const { return kind == Kind::None; }
};

/// Priority: ports > waypoints of the selected edge > nodes (topmost) > edges.
/// `tolerance` is in world units (usually px / zoom).
Hit HitTest(const Model &m, Vec2 p, const Selection &sel, double tolerance, const Registry &reg = Registry::Default());

} // namespace ad

#endif // _INTERACTION_HIT_TEST_HPP_
