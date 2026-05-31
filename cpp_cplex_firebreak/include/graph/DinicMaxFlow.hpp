#pragma once

#include <vector>

namespace firebreak::graph {

class DinicMaxFlow {
public:
    explicit DinicMaxFlow(int node_count);

    void addEdge(int from, int to, double capacity);
    double maxFlow(int source, int sink);
    std::vector<char> reachableFromSourceInResidual(int source) const;

    int phases() const;

private:
    struct Edge {
        int to = -1;
        int reverse_index = -1;
        double capacity = 0.0;
    };

    bool buildLevelGraph(int source, int sink);
    double sendFlow(int node, int sink, double pushed);
    void validateNode(int node) const;

    static constexpr double kEpsilon = 1.0e-9;

    std::vector<std::vector<Edge>> adjacency_;
    std::vector<int> level_;
    std::vector<int> next_edge_;
    int phases_ = 0;
};

}  // namespace firebreak::graph
