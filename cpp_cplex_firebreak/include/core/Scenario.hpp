#pragma once

#include <cstddef>
#include <string>

#include "core/Graph.hpp"

namespace firebreak::core {

struct Scenario {
    int scenario_id = 0;
    std::string message_filename;
    int ignition_node = 0;
    std::string weather_metadata;
    Graph propagation_graph;

    const Graph& graph() const;
    Graph& graph();
    std::size_t num_edges() const;
    std::size_t num_observed_nodes() const;
};

}  // namespace firebreak::core
