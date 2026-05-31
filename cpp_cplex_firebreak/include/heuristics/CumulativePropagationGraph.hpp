#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::heuristics {

class CumulativePropagationGraph {
public:
    void buildFromOptimizationInstance(const opt::OptimizationInstance& opt);

    const std::vector<int>& nodes() const;
    const std::vector<int>& eligibleNodes() const;
    const std::vector<int>& successors(int node) const;
    const std::vector<int>& predecessors(int node) const;

    bool hasNode(int node) const;
    int originalNode(int compact_index) const;
    double arcWeight(int u, int v) const;
    double inverseArcWeight(int u, int v) const;
    std::vector<int> reachableFrom(int node, const std::unordered_set<int>& blocked_nodes) const;
    std::size_t numArcs() const;
    int numNodes() const;

private:
    int node_count_ = 0;
    std::vector<int> nodes_;
    std::vector<int> original_nodes_;
    std::vector<int> eligible_nodes_;
    std::vector<std::vector<int>> successors_;
    std::vector<std::vector<int>> predecessors_;
    std::vector<std::unordered_map<int, double>> weights_;
    std::vector<int> empty_;
    std::size_t arc_count_ = 0;
};

}  // namespace firebreak::heuristics
