#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benchmarks/StaticDpvBenchmark.hpp"
#include "opt/DpvIndexBuilder.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = budget;
    opt.node_mapper.build_from_nodes(original_nodes);
    opt.eligible_original_nodes = original_nodes;
    for (const int original_node : original_nodes) {
        opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
    }

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = original_nodes.front();
    scenario.message_filename = "synthetic.csv";
    scenario.arcs = arcs;
    for (int index = 0; index < opt.node_mapper.size(); ++index) {
        scenario.observed_node_indices.push_back(index);
    }

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.num_pairs();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

void test_chain_selects_root() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);

    firebreak::benchmarks::StaticDpvBenchmark benchmark;
    const auto result = benchmark.run(opt, opt.budget);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{1}));
    assert(std::fabs(result.selected_scores.front() - 3.0) < 1.0e-9);
}

void test_branch_selects_root() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
    }, 1);

    firebreak::benchmarks::StaticDpvBenchmark benchmark;
    const auto result = benchmark.run(opt, opt.budget);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{1}));
    assert(std::fabs(result.selected_scores.front() - 6.0) < 1.0e-9);
}

void test_tie_breaks_by_smaller_original_node() {
    const auto opt = make_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
    }, 1);

    firebreak::benchmarks::StaticDpvBenchmark benchmark;
    const auto result = benchmark.run(opt, opt.budget);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{1}));
    assert(std::fabs(result.selected_scores.front() - 2.0) < 1.0e-9);
}

void test_budget_two_selects_top_two() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 2);

    firebreak::benchmarks::StaticDpvBenchmark benchmark;
    const auto result = benchmark.run(opt, opt.budget);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{1, 2}));
    assert(std::fabs(result.total_static_dpv_score - 5.0) < 1.0e-9);
}

}  // namespace

int main() {
    test_chain_selects_root();
    test_branch_selects_root();
    test_tie_breaks_by_smaller_original_node();
    test_budget_two_selects_top_two();
    std::cout << "All Static-DPV benchmark tests passed.\n";
    return 0;
}
