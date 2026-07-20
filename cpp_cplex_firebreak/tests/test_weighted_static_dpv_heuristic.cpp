#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benchmarks/StaticDpvBenchmark.hpp"
#include "opt/DpvIndexBuilder.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-9);
}

firebreak::opt::OptimizationInstance make_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_weighted_static_dpv";
    opt.n_cells = 4;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.compact_cell_weights = {1000.0, 1.0, 100.0, 10.0};
    for (const int node : {1, 2, 4}) {
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
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{0, 3, 1, 4},
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
    firebreak::benchmarks::StaticDpvBenchmark benchmark;
    firebreak::benchmarks::StaticDpvBenchmarkOptions options;
    options.ignition_policy = firebreak::opt::WeightedDpvIgnitionPolicy::FppIgnitionNoProtection;
    const auto result = benchmark.run(opt, opt.budget, options);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{2}));
    assert_close(result.selected_scores.front(), 101.0);
    assert(result.dpv_weight_profile == "provided_compact_weights");
    assert(result.dpv_ignition_policy == "fpp_ignition_no_protection");

    std::cout << "Weighted Static-DPV heuristic test passed.\n";
    return 0;
}
