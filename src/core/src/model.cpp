#include "ad/model.hpp"

#include <algorithm>

namespace ad {

namespace {

template <class Vec>
auto* findById(Vec& v, std::string_view id) {
    const auto it = std::ranges::find(v, id, [](const auto& e) -> std::string_view { return e.id; });
    return it != v.end() ? &*it : nullptr;
}

}  // namespace

const Port* Node::port(std::string_view portId) const { return portId.empty() ? nullptr : findById(ports, portId); }
Port* Node::port(std::string_view portId) { return portId.empty() ? nullptr : findById(ports, portId); }

const Node* Model::node(std::string_view id) const { return findById(nodes, id); }
Node* Model::node(std::string_view id) { return findById(nodes, id); }
const Edge* Model::edge(std::string_view id) const { return id.empty() ? nullptr : findById(edges, id); }
Edge* Model::edge(std::string_view id) { return id.empty() ? nullptr : findById(edges, id); }
const Step* Model::step(std::string_view id) const { return findById(scenario.steps, id); }
Step* Model::step(std::string_view id) { return findById(scenario.steps, id); }

const Edge* Model::edgeBetween(std::string_view a, std::string_view b) const {
    const auto it = std::ranges::find_if(
        edges, [&](const Edge& e) { return (e.from == a && e.to == b) || (e.from == b && e.to == a); });
    return it != edges.end() ? &*it : nullptr;
}

const Port* Model::port(std::string_view nodeId, std::string_view portId) const {
    const Node* n = node(nodeId);
    return n ? n->port(portId) : nullptr;
}

}  // namespace ad
