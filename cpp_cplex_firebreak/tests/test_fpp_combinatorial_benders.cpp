#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-6);
}

firebreak::opt::OptimizationInstance make_tree_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "combinatorial_tree";
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {0, 1, 2, 3};
    opt.eligible_original_nodes = {1, 2, 3, 4};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 10;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});
    scenario.arcs.push_back(firebreak::opt::CompactArc{2, 3, 3, 4});
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

firebreak::opt::OptimizationInstance make_dag_instance(int scenario_count = 1) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "combinatorial_dag";
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {1, 2, 3};
    opt.eligible_original_nodes = {2, 3, 4};
    for (int s = 0; s < scenario_count; ++s) {
        firebreak::opt::OptimizationScenario scenario;
        scenario.scenario_id = 100 + s;
        scenario.probability = 1.0 / static_cast<double>(scenario_count);
        scenario.ignition_index = 0;
        scenario.ignition_original_node = 1;
        scenario.observed_node_indices = {0, 1, 2, 3};
        scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
        scenario.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
        scenario.arcs.push_back(firebreak::opt::CompactArc{1, 3, 2, 4});
        scenario.arcs.push_back(firebreak::opt::CompactArc{2, 3, 3, 4});
        opt.scenarios.push_back(scenario);
        opt.scenario_probabilities.push_back(scenario.probability);
        opt.total_arcs += scenario.arcs.size();
    }
    return opt;
}

void test_integer_tree_cut_matches_reachability() {
    const auto opt = make_tree_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);

    const std::vector<double> empty_y = {0.0, 0.0, 0.0, 0.0};
    const auto empty_cut = separator.separateScenario(
        0,
        empty_y,
        0.0,
        false,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        1.0e-7);
    assert_close(empty_cut.rhs_at_ybar, 4.0);
    assert(empty_cut.active_nodes == 4);

    const std::vector<int> block_middle = {0, 0, 1, 0};
    const auto losses = separator.evaluateScenarioLosses(block_middle);
    assert_close(losses[0], 2.0);
    const auto cut = separator.separateScenario(
        0,
        std::vector<double>{0.0, 0.0, 1.0, 0.0},
        0.0,
        false,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        1.0e-7);
    assert_close(cut.rhs_at_ybar, 2.0);
}

void test_integer_dag_two_paths() {
    const auto opt = make_dag_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);

    const auto one_path_open = separator.evaluateScenarioLosses({1, 0, 0});
    assert_close(one_path_open[0], 3.0);
    const auto both_paths_blocked = separator.evaluateScenarioLosses({1, 1, 0});
    assert_close(both_paths_blocked[0], 1.0);
}

void test_fractional_separation_on_dag() {
    const auto opt = make_dag_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto cut = separator.separateScenario(
        0,
        std::vector<double>{0.4, 0.4, 0.0},
        0.0,
        true,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        1.0e-7);
    assert(cut.lift_mode_fallback);
    assert_close(cut.rhs_at_ybar, 2.8);
    assert(cut.active_nodes == 4);
}

void test_heuristic_lift_counts_candidate_once_per_path() {
    const auto opt = make_dag_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto cut = separator.separateScenario(
        0,
        std::vector<double>{0.0, 0.0, 0.0},
        0.0,
        false,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        1.0e-7);
    for (const auto& [compact_node, coefficient] : cut.cut.coefficients_by_compact_index) {
        (void)compact_node;
        assert(coefficient <= 0.0);
        assert(std::fabs(coefficient) <= cut.activation_paths);
    }
}

void test_initial_cuts_and_sampling() {
    const auto opt = make_dag_instance(10);
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto initial_y = separator.greedyInitialSolution();
    const auto initial_cuts = separator.initialCutsFromSolution(
        initial_y,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic);
    assert(initial_cuts.size() == opt.scenarios.size());

    const std::vector<double> y(opt.eligible_indices.size(), 0.0);
    const std::vector<double> eta(opt.scenarios.size(), 0.0);
    const auto summary = separator.separateViolatedCuts(
        y,
        eta,
        false,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        0.20,
        1.0e-7);
    assert(summary.cuts.size() == 2);
    assert(summary.violated_cuts == 2);
    assert(summary.scenarios_checked == 2);
    assert(summary.scenarios_skipped == 8);
}

void test_lift_mode_parser() {
    assert(
        firebreak::benders::parse_fpp_combinatorial_benders_lift_mode("none") ==
        firebreak::benders::FppCombinatorialBendersLiftMode::None);
    assert(
        firebreak::benders::parse_fpp_combinatorial_benders_lift_mode("posterior") ==
        firebreak::benders::FppCombinatorialBendersLiftMode::Posterior);
    assert(
        firebreak::benders::parse_fpp_combinatorial_benders_lift_mode("heuristic") ==
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic);
}

}  // namespace

int main() {
    test_integer_tree_cut_matches_reachability();
    test_integer_dag_two_paths();
    test_fractional_separation_on_dag();
    test_heuristic_lift_counts_candidate_once_per_path();
    test_initial_cuts_and_sampling();
    test_lift_mode_parser();
    std::cout << "All FPP combinatorial Benders tests passed.\n";
    return 0;
}
