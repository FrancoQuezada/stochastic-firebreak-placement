#include "core/Scenario.hpp"

namespace firebreak::core {

const Graph& Scenario::graph() const {
    return propagation_graph;
}

Graph& Scenario::graph() {
    return propagation_graph;
}

std::size_t Scenario::num_edges() const {
    return propagation_graph.num_edges();
}

std::size_t Scenario::num_observed_nodes() const {
    return propagation_graph.num_nodes_observed();
}

}  // namespace firebreak::core
