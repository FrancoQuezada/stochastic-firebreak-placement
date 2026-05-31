#include "cuts/NodeSeparatorMinCut.hpp"

#include <algorithm>
#include <cmath>

#include "graph/DinicMaxFlow.hpp"

namespace firebreak::cuts {
namespace {

int split_in_node(int local_node) {
    return 2 * local_node;
}

int split_out_node(int local_node) {
    return 2 * local_node + 1;
}

void include_node(std::vector<int>& nodes, int compact_node) {
    if (compact_node >= 0) {
        nodes.push_back(compact_node);
    }
}

int max_compact_node_in_instance(const opt::OptimizationInstance& instance) {
    int max_node = -1;
    for (const int node : instance.eligible_indices) {
        max_node = std::max(max_node, node);
    }
    for (const auto& scenario : instance.scenarios) {
        max_node = std::max(max_node, scenario.ignition_index);
        for (const int node : scenario.observed_node_indices) {
            max_node = std::max(max_node, node);
        }
        for (const auto& arc : scenario.arcs) {
            max_node = std::max(max_node, std::max(arc.u_index, arc.v_index));
        }
    }
    return max_node;
}

}  // namespace

NodeSeparatorMinCut::NodeSeparatorMinCut(const opt::OptimizationInstance& instance)
    : instance_(instance) {
    const int max_node = max_compact_node_in_instance(instance_);
    if (max_node >= 0) {
        eligible_by_compact_node_.assign(static_cast<std::size_t>(max_node + 1), 0);
    }
    for (const int node : instance_.eligible_indices) {
        if (node < 0) {
            continue;
        }
        if (node >= static_cast<int>(eligible_by_compact_node_.size())) {
            eligible_by_compact_node_.resize(static_cast<std::size_t>(node + 1), 0);
        }
        eligible_by_compact_node_[static_cast<std::size_t>(node)] = 1;
    }

    topologies_.reserve(instance_.scenarios.size());
    for (std::size_t scenario_index = 0; scenario_index < instance_.scenarios.size(); ++scenario_index) {
        topologies_.push_back(buildScenarioTopology(static_cast<int>(scenario_index)));
    }
}

NodeSeparatorResult NodeSeparatorMinCut::compute(
    int scenario_index,
    int target_compact_node,
    const std::vector<double>& ybar_by_compact_node) const {
    NodeSeparatorResult result;
    result.target_compact_node = target_compact_node;

    if (scenario_index < 0 || scenario_index >= static_cast<int>(topologies_.size())) {
        result.feasible = false;
        result.notes = "Scenario index is out of range.";
        return result;
    }

    const ScenarioTopology& topology = topologies_[static_cast<std::size_t>(scenario_index)];
    result.source_compact_node = topology.root_compact_node;

    if (topology.root_compact_node < 0 || topology.split_node_count <= 0) {
        result.feasible = false;
        result.notes = "Scenario topology has no valid root.";
        return result;
    }
    if (target_compact_node == topology.root_compact_node) {
        result.feasible = false;
        result.cut_capacity = 0.0;
        result.notes = "Target is the scenario root; root separator cuts are skipped.";
        return result;
    }
    if (target_compact_node < 0 ||
        target_compact_node >= static_cast<int>(topology.observed_membership_by_compact_node.size()) ||
        !topology.observed_membership_by_compact_node[static_cast<std::size_t>(target_compact_node)]) {
        result.feasible = false;
        result.notes = "Target compact node is not observed in the scenario.";
        return result;
    }
    if (target_compact_node >= static_cast<int>(topology.local_by_compact_node.size()) ||
        topology.local_by_compact_node[static_cast<std::size_t>(target_compact_node)] < 0) {
        result.feasible = false;
        result.notes = "Target compact node is missing from the split graph.";
        return result;
    }

    graph::DinicMaxFlow flow(topology.split_node_count);

    // Split each compact node into an entrance and exit. The entrance-exit arc
    // is the node-capacity arc recovered later as the separator.
    for (int local = 0; local < static_cast<int>(topology.compact_nodes_by_local.size()); ++local) {
        const int compact_node = topology.compact_nodes_by_local[static_cast<std::size_t>(local)];
        double capacity = topology.inf_capacity;
        if (compact_node != topology.root_compact_node && isEligible(compact_node)) {
            capacity = 1.0 - yValue(ybar_by_compact_node, compact_node);
        }
        flow.addEdge(split_in_node(local), split_out_node(local), capacity);
    }

    for (const auto& arc : topology.propagation_arcs_local) {
        flow.addEdge(split_out_node(arc.first), split_in_node(arc.second), topology.inf_capacity);
    }

    const int root_local = topology.local_by_compact_node[static_cast<std::size_t>(topology.root_compact_node)];
    const int target_local = topology.local_by_compact_node[static_cast<std::size_t>(target_compact_node)];
    const int source = split_out_node(root_local);
    const int sink = split_out_node(target_local);

    result.cut_capacity = flow.maxFlow(source, sink);
    result.max_flow_iterations_or_phases = flow.phases();

    if (result.cut_capacity >= topology.inf_capacity / 2.0) {
        result.feasible = false;
        result.separator_compact_nodes.clear();
        result.notes = "No finite useful eligible node separator was found.";
        return result;
    }

    const std::vector<char> reachable = flow.reachableFromSourceInResidual(source);
    for (int local = 0; local < static_cast<int>(topology.compact_nodes_by_local.size()); ++local) {
        const int in_node = split_in_node(local);
        const int out_node = split_out_node(local);
        if (!reachable[static_cast<std::size_t>(in_node)] ||
            reachable[static_cast<std::size_t>(out_node)]) {
            continue;
        }

        const int compact_node = topology.compact_nodes_by_local[static_cast<std::size_t>(local)];
        if (compact_node == topology.root_compact_node || !isEligible(compact_node)) {
            continue;
        }
        result.separator_compact_nodes.push_back(compact_node);
    }

    std::sort(result.separator_compact_nodes.begin(), result.separator_compact_nodes.end());
    result.separator_compact_nodes.erase(
        std::unique(result.separator_compact_nodes.begin(), result.separator_compact_nodes.end()),
        result.separator_compact_nodes.end());
    return result;
}

std::vector<NodeSeparatorResult> NodeSeparatorMinCut::computeForTargets(
    int scenario_index,
    const std::vector<int>& target_compact_nodes,
    const std::vector<double>& ybar_by_compact_node) const {
    std::vector<NodeSeparatorResult> results;
    results.reserve(target_compact_nodes.size());
    for (const int target : target_compact_nodes) {
        results.push_back(compute(scenario_index, target, ybar_by_compact_node));
    }
    return results;
}

bool NodeSeparatorMinCut::isEligible(int compact_node) const {
    return compact_node >= 0 &&
           compact_node < static_cast<int>(eligible_by_compact_node_.size()) &&
           eligible_by_compact_node_[static_cast<std::size_t>(compact_node)] != 0;
}

double NodeSeparatorMinCut::yValue(
    const std::vector<double>& ybar_by_compact_node,
    int compact_node) const {
    if (compact_node < 0 || compact_node >= static_cast<int>(ybar_by_compact_node.size())) {
        return 0.0;
    }
    const double value = ybar_by_compact_node[static_cast<std::size_t>(compact_node)];
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, value));
}

NodeSeparatorMinCut::ScenarioTopology NodeSeparatorMinCut::buildScenarioTopology(int scenario_index) const {
    ScenarioTopology topology;
    topology.scenario_index = scenario_index;

    const opt::OptimizationScenario& scenario = instance_.scenarios[static_cast<std::size_t>(scenario_index)];
    topology.scenario_id = scenario.scenario_id;
    topology.root_compact_node = scenario.ignition_index;

    std::vector<int> compact_nodes = scenario.observed_node_indices;
    include_node(compact_nodes, scenario.ignition_index);
    for (const auto& arc : scenario.arcs) {
        include_node(compact_nodes, arc.u_index);
        include_node(compact_nodes, arc.v_index);
    }
    std::sort(compact_nodes.begin(), compact_nodes.end());
    compact_nodes.erase(std::unique(compact_nodes.begin(), compact_nodes.end()), compact_nodes.end());

    topology.compact_nodes_by_local = compact_nodes;
    topology.inf_capacity = static_cast<double>(compact_nodes.size()) + 1.0;
    topology.split_node_count = 2 * static_cast<int>(compact_nodes.size());

    int max_node = -1;
    for (const int node : compact_nodes) {
        max_node = std::max(max_node, node);
    }
    for (const int node : scenario.observed_node_indices) {
        max_node = std::max(max_node, node);
    }

    if (max_node >= 0) {
        topology.local_by_compact_node.assign(static_cast<std::size_t>(max_node + 1), -1);
        topology.observed_membership_by_compact_node.assign(static_cast<std::size_t>(max_node + 1), 0);
    }

    for (int local = 0; local < static_cast<int>(compact_nodes.size()); ++local) {
        const int compact_node = compact_nodes[static_cast<std::size_t>(local)];
        topology.local_by_compact_node[static_cast<std::size_t>(compact_node)] = local;
    }
    for (const int compact_node : scenario.observed_node_indices) {
        if (compact_node >= 0 &&
            compact_node < static_cast<int>(topology.observed_membership_by_compact_node.size())) {
            topology.observed_membership_by_compact_node[static_cast<std::size_t>(compact_node)] = 1;
        }
    }

    std::vector<std::pair<int, int>> arcs_local;
    arcs_local.reserve(scenario.arcs.size());
    for (const auto& arc : scenario.arcs) {
        if (arc.u_index < 0 || arc.v_index < 0 ||
            arc.u_index >= static_cast<int>(topology.local_by_compact_node.size()) ||
            arc.v_index >= static_cast<int>(topology.local_by_compact_node.size())) {
            continue;
        }
        const int u_local = topology.local_by_compact_node[static_cast<std::size_t>(arc.u_index)];
        const int v_local = topology.local_by_compact_node[static_cast<std::size_t>(arc.v_index)];
        if (u_local < 0 || v_local < 0) {
            continue;
        }
        arcs_local.emplace_back(u_local, v_local);
    }
    std::sort(arcs_local.begin(), arcs_local.end());
    arcs_local.erase(std::unique(arcs_local.begin(), arcs_local.end()), arcs_local.end());
    topology.propagation_arcs_local = std::move(arcs_local);

    return topology;
}

}  // namespace firebreak::cuts
