#include "core/Graph.hpp"

#include <algorithm>
#include <stdexcept>

namespace firebreak::core {

void Graph::add_edge(int u, int v, double time, double ros, bool has_time, bool has_ros) {
    if (u <= 0 || v <= 0) {
        throw std::invalid_argument("Graph node IDs must be positive.");
    }
    edges_.push_back(Edge{u, v, time, ros, has_time, has_ros});
    adjacency_[u].push_back(v);
    observed_nodes_.insert(u);
    observed_nodes_.insert(v);
}

const std::vector<int>& Graph::successors(int u) const {
    static const std::vector<int> empty;
    const auto it = adjacency_.find(u);
    if (it == adjacency_.end()) {
        return empty;
    }
    return it->second;
}

const std::vector<Edge>& Graph::edges() const {
    return edges_;
}

bool Graph::has_node(int node) const {
    return observed_nodes_.find(node) != observed_nodes_.end();
}

std::size_t Graph::num_edges() const {
    return edges_.size();
}

std::size_t Graph::num_nodes_observed() const {
    return observed_nodes_.size();
}

int Graph::max_node_id_observed() const {
    if (observed_nodes_.empty()) {
        return 0;
    }
    return *std::max_element(observed_nodes_.begin(), observed_nodes_.end());
}

std::vector<int> Graph::observed_nodes() const {
    std::vector<int> nodes(observed_nodes_.begin(), observed_nodes_.end());
    std::sort(nodes.begin(), nodes.end());
    return nodes;
}

}  // namespace firebreak::core
