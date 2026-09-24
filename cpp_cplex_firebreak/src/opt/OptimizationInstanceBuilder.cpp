#include "opt/OptimizationInstanceBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

#include "core/LandscapeWeightMap.hpp"
#include "opt/DpvIndexBuilder.hpp"

namespace firebreak::opt {

namespace {

std::vector<int> sorted_unique(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

void insert_observed_nodes(const core::Instance& instance, std::vector<int>& nodes) {
    for (const auto& scenario : instance.scenarios) {
        nodes.push_back(scenario.ignition_node);
        const auto observed = scenario.graph().observed_nodes();
        nodes.insert(nodes.end(), observed.begin(), observed.end());
    }
}

std::vector<int> build_eligible_original_nodes(const core::Instance& instance, std::vector<std::string>& notes) {
    if (instance.available_nodes_known && !instance.available_nodes.empty()) {
        notes.push_back("Eligible firebreak nodes were read from Instance.available_nodes.");
        return sorted_unique(instance.available_nodes);
    }

    std::vector<int> eligible;
    insert_observed_nodes(instance, eligible);
    notes.push_back(
        "Eligible firebreak nodes fell back to the union of observed scenario graph nodes and ignition nodes.");
    return sorted_unique(std::move(eligible));
}

int compute_budget(double alpha, int n_cells) {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Alpha must be finite.");
    }
    if (alpha < 0.0) {
        throw std::runtime_error("Alpha must be nonnegative.");
    }
    if (n_cells <= 0) {
        throw std::runtime_error("NCells must be positive to compute a firebreak budget.");
    }
    const double raw_budget = std::floor(alpha * static_cast<double>(n_cells));
    if (raw_budget > static_cast<double>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Computed budget is too large for an int.");
    }
    return static_cast<int>(raw_budget);
}

}  // namespace

OptimizationInstance OptimizationInstanceBuilder::build(
    const core::Instance& instance,
    double alpha,
    bool build_dpv_indices) const {
    if (instance.scenarios.empty()) {
        throw std::runtime_error("Cannot build an optimization instance without scenarios.");
    }

    OptimizationInstance output;
    output.landscape_name = instance.landscape_name;
    output.alpha = alpha;
    output.n_cells = instance.n_cells;
    output.budget = compute_budget(alpha, instance.n_cells);

    output.eligible_original_nodes = build_eligible_original_nodes(instance, output.metadata_notes);

    std::vector<int> node_universe = output.eligible_original_nodes;
    insert_observed_nodes(instance, node_universe);
    node_universe = sorted_unique(std::move(node_universe));
    output.node_mapper.build_from_nodes(node_universe);
    output.cell_weight_map = core::make_homogeneous_weight_map(
        output.node_mapper.original_nodes());
    output.compact_cell_weights = core::build_compact_weight_vector(
        output.cell_weight_map,
        output.node_mapper);

    output.eligible_indices.reserve(output.eligible_original_nodes.size());
    for (const int original_node : output.eligible_original_nodes) {
        output.eligible_indices.push_back(output.node_mapper.to_index(original_node));
    }

    if (output.budget < 0) {
        throw std::runtime_error("Computed budget is negative.");
    }
    if (output.budget > static_cast<int>(output.eligible_indices.size())) {
        throw std::runtime_error(
            "Computed budget " + std::to_string(output.budget) +
            " exceeds number of eligible firebreak nodes " + std::to_string(output.eligible_indices.size()) + ".");
    }

    const double probability = 1.0 / static_cast<double>(instance.scenarios.size());
    output.scenario_probabilities.assign(instance.scenarios.size(), probability);

    output.scenarios.reserve(instance.scenarios.size());
    for (std::size_t scenario_pos = 0; scenario_pos < instance.scenarios.size(); ++scenario_pos) {
        const auto& scenario = instance.scenarios[scenario_pos];

        OptimizationScenario opt_scenario;
        opt_scenario.scenario_id = scenario.scenario_id;
        opt_scenario.probability = probability;
        opt_scenario.ignition_original_node = scenario.ignition_node;
        opt_scenario.ignition_index = output.node_mapper.to_index(scenario.ignition_node);
        opt_scenario.message_filename = scenario.message_filename;

        std::vector<int> observed_nodes = scenario.graph().observed_nodes();
        observed_nodes.push_back(scenario.ignition_node);
        observed_nodes = sorted_unique(std::move(observed_nodes));
        opt_scenario.observed_node_indices.reserve(observed_nodes.size());
        for (const int original_node : observed_nodes) {
            opt_scenario.observed_node_indices.push_back(output.node_mapper.to_index(original_node));
        }

        opt_scenario.arcs.reserve(scenario.graph().edges().size());
        for (const auto& edge : scenario.graph().edges()) {
            const int u_index = output.node_mapper.to_index(edge.from);
            const int v_index = output.node_mapper.to_index(edge.to);
            opt_scenario.arcs.push_back(CompactArc{u_index, v_index, edge.from, edge.to});
        }
        output.total_arcs += opt_scenario.arcs.size();
        output.scenarios.push_back(std::move(opt_scenario));
        (void)scenario_pos;
    }

    if (build_dpv_indices) {
        DpvIndexBuilder dpv_builder;
        for (auto& scenario : output.scenarios) {
            scenario.dpv = dpv_builder.build_for_scenario(scenario, output.node_mapper);
            output.total_dpv_pairs += scenario.dpv.num_pairs();
        }
        output.metadata_notes.push_back("DPV descendant sets use closed reachability, so each node is its own descendant.");
    }

    output.metadata_notes.push_back("Scenario probabilities are uniform over the loaded scenarios.");
    output.metadata_notes.push_back("Budget is floor(alpha * NCells).");
    output.metadata_notes.push_back(
        "Cell weights default to a homogeneous map over the compact optimization node universe.");
    return output;
}

}  // namespace firebreak::opt
