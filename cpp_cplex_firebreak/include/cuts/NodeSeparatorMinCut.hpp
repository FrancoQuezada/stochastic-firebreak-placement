#pragma once

#include <string>
#include <utility>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::cuts {

struct NodeSeparatorResult {
    bool feasible = true;
    double cut_capacity = 0.0;
    std::vector<int> separator_compact_nodes;
    int source_compact_node = -1;
    int target_compact_node = -1;
    int max_flow_iterations_or_phases = 0;
    std::string notes;
};

class NodeSeparatorMinCut {
public:
    explicit NodeSeparatorMinCut(const opt::OptimizationInstance& instance);

    NodeSeparatorResult compute(
        int scenario_index,
        int target_compact_node,
        const std::vector<double>& ybar_by_compact_node) const;

    std::vector<NodeSeparatorResult> computeForTargets(
        int scenario_index,
        const std::vector<int>& target_compact_nodes,
        const std::vector<double>& ybar_by_compact_node) const;

private:
    struct ScenarioTopology {
        int scenario_index = -1;
        int scenario_id = -1;
        int root_compact_node = -1;
        double inf_capacity = 0.0;
        int split_node_count = 0;
        std::vector<int> compact_nodes_by_local;
        std::vector<int> local_by_compact_node;
        std::vector<char> observed_membership_by_compact_node;
        std::vector<std::pair<int, int>> propagation_arcs_local;
    };

    bool isEligible(int compact_node) const;
    double yValue(const std::vector<double>& ybar_by_compact_node, int compact_node) const;
    ScenarioTopology buildScenarioTopology(int scenario_index) const;

    const opt::OptimizationInstance& instance_;
    std::vector<char> eligible_by_compact_node_;
    std::vector<ScenarioTopology> topologies_;
};

}  // namespace firebreak::cuts
