#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_chain() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_chain";
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {0, 1, 3};
    opt.eligible_original_nodes = {1, 2, 4};
    opt.compact_cell_weights = {100.0, 2.0, 50.0, 200.0, 7.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 10;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
        firebreak::opt::CompactArc{3, 4, 4, 5},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

double coefficient_for(
    const firebreak::benders::FppCombinatorialCut& cut,
    int compact_node) {
    for (const auto& [node, coefficient] : cut.cut.coefficients_by_compact_index) {
        if (node == compact_node) {
            return coefficient;
        }
    }
    return 0.0;
}

void test_manual_weighted_loss_and_cut_terms() {
    const auto opt = make_weighted_chain();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    assert(separator.weighted());

    const std::vector<int> y_int = {1, 0, 1};
    const auto losses = separator.evaluateScenarioLosses(y_int);
    assert_close(losses[0], 152.0);

    const auto cut = separator.separateScenario(
        0,
        std::vector<double>{1.0, 0.0, 1.0},
        0.0,
        false,
        firebreak::benders::FppCombinatorialBendersLiftMode::None,
        1.0e-7);

    assert_close(cut.incumbent_weighted_loss, 152.0);
    assert_close(cut.cut.subproblem_objective, 152.0);
    assert_close(cut.cut.rhs_constant, 152.0);
    assert_close(cut.rhs_at_ybar, 152.0);
    assert_close(cut.tightness_error, 0.0);
    assert(cut.active_nodes == 3);

    assert_close(coefficient_for(cut, 0), 0.0);
    assert_close(coefficient_for(cut, 1), -52.0);
    assert_close(coefficient_for(cut, 3), 0.0);
}

void test_ignition_and_reached_firebreak_semantics() {
    const auto opt = make_weighted_chain();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);

    const auto select_ignition_only = separator.evaluateScenarioLosses({1, 0, 0});
    const auto empty = separator.evaluateScenarioLosses({0, 0, 0});
    assert_close(select_ignition_only[0], empty[0]);
    assert_close(empty[0], 359.0);

    const auto block_reached_nonroot = separator.evaluateScenarioLosses({0, 0, 1});
    assert_close(block_reached_nonroot[0], 152.0);
}

}  // namespace

int main() {
    test_manual_weighted_loss_and_cut_terms();
    test_ignition_and_reached_firebreak_semantics();
    std::cout << "All weighted FPP combinatorial cut tests passed.\n";
    return 0;
}
