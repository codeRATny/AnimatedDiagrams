#include "Model.hpp"

#include <algorithm>

namespace ad
{

namespace
{

template <class Vec>
auto FindById(Vec &v, std::string_view id) -> decltype(&v.front())
{
    if (id.empty())
    {
        return nullptr;
    }
    const auto it = std::ranges::find_if(v,
                                         [id](const auto &e)
                                         {
                                             return e.id == id;
                                         });
    return it != v.end() ? &*it : nullptr;
}

} // namespace

const Port *Node::FindPort(std::string_view port_id) const { return FindById(ports, port_id); }
Port       *Node::FindPort(std::string_view port_id) { return FindById(ports, port_id); }

const Node   *Model::FindNode(std::string_view id) const { return FindById(nodes, id); }
Node         *Model::FindNode(std::string_view id) { return FindById(nodes, id); }
const Edge   *Model::FindEdge(std::string_view id) const { return FindById(edges, id); }
Edge         *Model::FindEdge(std::string_view id) { return FindById(edges, id); }
const Step   *Model::FindStep(std::string_view id) const { return FindById(scenario.steps, id); }
Step         *Model::FindStep(std::string_view id) { return FindById(scenario.steps, id); }
const Marker *Model::FindMarker(std::string_view id) const { return FindById(scenario.markers, id); }
Marker       *Model::FindMarker(std::string_view id) { return FindById(scenario.markers, id); }

const Edge *Model::EdgeBetween(std::string_view a, std::string_view b) const
{
    const auto it = std::ranges::find_if(edges,
                                         [&](const Edge &e)
                                         {
                                             return (e.from == a && e.to == b) || (e.from == b && e.to == a);
                                         });
    return it != edges.end() ? &*it : nullptr;
}

const Port *Model::FindPort(std::string_view node_id, std::string_view port_id) const
{
    const Node *n = FindNode(node_id);
    return n != nullptr ? n->FindPort(port_id) : nullptr;
}

} // namespace ad
