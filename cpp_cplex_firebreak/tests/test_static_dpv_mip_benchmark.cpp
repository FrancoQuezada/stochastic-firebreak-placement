#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benchmarks/StaticDpvBenchmark.hpp"
#include "benchmarks/StaticDpvMipBenchmark.hpp"
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

double mip_score_for_original(
    const firebreak::benchmarks::StaticDpvMipBenchmarkResult& result,
    int original_node) {
    for (const auto& score : result.all_scores) {
        if (score.original_node == original_node) {
            return score.dpv_score;
        }
    }
    assert(false && "missing Static-DPV-MIP score");
    return -1.0;
}

double static_score_for_original(
    const firebreak::benchmarks::StaticDpvBenchmarkResult& result,
    int original_node) {
    for (const auto& score : result.all_scores) {
        if (score.original_node == original_node) {
            return score.score;
        }
    }
    assert(false && "missing Static-DPV score");
    return -1.0;
}

void test_unit_downstream_dpv_formula_without_out_degree_multiplier() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
    }, 1);

    firebreak::benchmarks::StaticDpvMipBenchmark benchmark;
    const auto result = benchmark.run(opt, opt.budget);

    assert(std::fabs(mip_score_for_original(result, 1) - 3.0) < 1.0e-9);
    assert(std::fabs(mip_score_for_original(result, 2) - 1.0) < 1.0e-9);
    assert(std::fabs(mip_score_for_original(result, 3) - 1.0) < 1.0e-9);
    assert((result.selected_firebreak_original_nodes == std::vector<int>{1}));
    assert(std::fabs(result.total_static_dpv_score - 3.0) < 1.0e-9);
    assert(result.solver_status == "ExactTopBudget");
}

void test_static_dpv_mip_differs_from_legacy_static_dpv() {
    const auto opt = make_instance({10, 11, 12, 13, 20, 21, 22, 23}, {
        firebreak::opt::CompactArc{0, 1, 10, 11},
        firebreak::opt::CompactArc{1, 2, 11, 12},
        firebreak::opt::CompactArc{2, 3, 12, 13},
        firebreak::opt::CompactArc{4, 5, 20, 21},
        firebreak::opt::CompactArc{4, 6, 20, 22},
        firebreak::opt::CompactArc{4, 7, 20, 23},
    }, 1);

    firebreak::benchmarks::StaticDpvBenchmark legacy_benchmark;
    const auto legacy = legacy_benchmark.run(opt, opt.budget);
    firebreak::benchmarks::StaticDpvMipBenchmark mip_benchmark;
    const auto mip = mip_benchmark.run(opt, opt.budget);

    assert(std::fabs(static_score_for_original(legacy, 10) - 4.0) < 1.0e-9);
    assert(std::fabs(static_score_for_original(legacy, 20) - 12.0) < 1.0e-9);
    assert(std::fabs(mip_score_for_original(mip, 10) - 4.0) < 1.0e-9);
    assert(std::fabs(mip_score_for_original(mip, 20) - 4.0) < 1.0e-9);
    assert((legacy.selected_firebreak_original_nodes == std::vector<int>{20}));
    assert((mip.selected_firebreak_original_nodes == std::vector<int>{10}));
}

void test_tie_breaks_by_smaller_original_node() {
    const auto opt = make_instance({20, 10}, {}, 1);

    firebreak::benchmarks::StaticDpvMipBenchmark benchmark;
    const auto result = benchmark.run(opt, opt.budget);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{10}));
    assert(std::fabs(result.selected_scores.front() - 1.0) < 1.0e-9);
}

void test_budget_limits_selected_size() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 2);

    firebreak::benchmarks::StaticDpvMipBenchmark benchmark;
    const auto result = benchmark.run(opt, opt.budget);

    assert(result.selected_firebreak_original_nodes.size() <= 2);
    assert((result.selected_firebreak_original_nodes == std::vector<int>{1, 2}));
    assert(std::fabs(result.total_static_dpv_score - 5.0) < 1.0e-9);
}

void test_zero_budget_selects_nothing() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 0);

    firebreak::benchmarks::StaticDpvMipBenchmark benchmark;
    const auto result = benchmark.run(opt, opt.budget);

    assert(result.selected_firebreak_original_nodes.empty());
    assert(result.selected_firebreak_indices.empty());
    assert(result.selected_scores.empty());
    assert(std::fabs(result.total_static_dpv_score) < 1.0e-9);
}

}  // namespace

int main() {
    test_unit_downstream_dpv_formula_without_out_degree_multiplier();
    test_static_dpv_mip_differs_from_legacy_static_dpv();
    test_tie_breaks_by_smaller_original_node();
    test_budget_limits_selected_size();
    test_zero_budget_selects_nothing();
    std::cout << "All Static-DPV-MIP benchmark tests passed.\n";
    return 0;
}
