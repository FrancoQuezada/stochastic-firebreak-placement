#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace firebreak::core {

struct Edge {
    int from = 0;
    int to = 0;
    double time = 0.0;
    double ros = 0.0;
    bool has_time = false;
    bool has_ros = false;
};

class Graph {
public:
    void add_edge(int u, int v, double time = 0.0, double ros = 0.0, bool has_time = false, bool has_ros = false);

    const std::vector<int>& successors(int u) const;
    const std::vector<Edge>& edges() const;

    bool has_node(int node) const;
    std::size_t num_edges() const;
    std::size_t num_nodes_observed() const;
    int max_node_id_observed() const;
    std::vector<int> observed_nodes() const;

private:
    std::vector<Edge> edges_;
    std::unordered_map<int, std::vector<int>> adjacency_;
    std::unordered_set<int> observed_nodes_;
};

}  // namespace firebreak::core
