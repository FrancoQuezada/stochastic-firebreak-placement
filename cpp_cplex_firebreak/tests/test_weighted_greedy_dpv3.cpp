#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "heuristics/GreedyHeuristic.hpp"
#include "opt/DpvIndexBuilder.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-9);
}

firebreak::opt::OptimizationInstance make_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_weighted_greedy_dpv3";
    opt.n_cells = 5;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.compact_cell_weights = {100.0, 1.0, 1.0, 5.0, 200.0};
    for (const int node : {2, 3}) {
        opt.eligible_indices.push_back(opt.node_mapper.to_index(node));
        opt.eligible_original_nodes.push_back(node);
    }

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = opt.node_mapper.to_index(1);
    scenario.ignition_original_node = 1;
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 4, 3, 5},
    };
    for (int index = 0; index < opt.node_mapper.size(); ++index) {
        scenario.observed_node_indices.push_back(index);
    }
    firebreak::opt::DpvIndexBuilder builder;
    scenario.dpv = builder.build_for_scenario(scenario, opt.node_mapper);
    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.num_pairs();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

}  // namespace

int main() {
    const auto opt = make_instance();
    firebreak::heuristics::GreedyHeuristic greedy;
    firebreak::heuristics::GreedyHeuristicOptions options;
    options.dpv_ignition_policy = firebreak::opt::WeightedDpvIgnitionPolicy::FppIgnitionNoProtection;
    const auto result = greedy.runGreedy(
        opt,
        firebreak::heuristics::GreedyMetricType::DPV3,
        true,
        false,
        options);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{3}));
    assert_close(result.selected_scores.front(), 101.0);
    assert(result.dpv_variant == "greedy_dpv3_cumulative_inverse_frequency_distance");
    assert(result.dpv_candidates_selected == 1);

    std::cout << "Weighted Greedy-DPV3 test passed.\n";
    return 0;
}
