#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

void assert_close(double actual, double expected, double tol = 1.0e-8) {
    assert(std::fabs(actual - expected) <= tol);
}

firebreak::opt::OptimizationInstance make_weighted_chain() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_initial_chain";
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};
    opt.compact_cell_weights = {100.0, 2.0, 50.0, 7.0};
    opt.cell_weight_map.deterministic_hash = "initial-test-hash";

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 7;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = static_cast<int>(scenario.arcs.size());
    return opt;
}

double coefficient_for(const firebreak::benders::BendersCut& cut, int compact_node) {
    for (const auto& [node, coefficient] : cut.coefficients_by_compact_index) {
        if (node == compact_node) {
            return coefficient;
        }
    }
    return 0.0;
}

void test_initial_cut_from_binary_solution_is_weighted_and_tight() {
    const auto opt = make_weighted_chain();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);

    const std::vector<int> y = {0, 1};
    const auto losses = separator.evaluateScenarioLosses(y);
    assert_close(losses.at(0), 102.0);

    for (const auto mode : {
             firebreak::benders::FppCombinatorialBendersLiftMode::None,
             firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
             firebreak::benders::FppCombinatorialBendersLiftMode::Posterior,
         }) {
        const auto cuts = separator.initialCutsFromSolution(y, mode);
        assert(cuts.size() == 1);
        const auto& cut = cuts.front();
        assert_close(cut.incumbent_weighted_loss, 102.0);
        assert_close(cut.cut.rhs_constant, 102.0);
        assert_close(cut.rhs_at_ybar, 102.0);
        assert_close(cut.tightness_error, 0.0);
        assert_close(coefficient_for(cut.cut, 1), -2.0);
        assert_close(coefficient_for(cut.cut, 2), 0.0);
        assert(cut.cut.scenario_id == 7);
    }
}

void test_greedy_initial_solution_and_binary_validity() {
    const auto opt = make_weighted_chain();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto greedy = separator.greedyInitialSolution();
    assert(greedy.size() == 2);
    assert(greedy.at(0) == 1);
    assert(greedy.at(1) == 0);

    const auto cuts = separator.initialCutsFromSolution(
        greedy,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic);
    const auto& cut = cuts.front().cut;
    for (int a = 0; a <= 1; ++a) {
        for (int b = 0; b <= 1; ++b) {
            if (a + b > opt.budget) {
                continue;
            }
            const std::vector<int> y = {a, b};
            const auto losses = separator.evaluateScenarioLosses(y);
            std::vector<double> compact(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
            compact[1] = static_cast<double>(a);
            compact[2] = static_cast<double>(b);
            assert(cut.evaluateAt(compact) <= losses.at(0) + 1.0e-8);
        }
    }
}

}  // namespace

int main() {
    test_initial_cut_from_binary_solution_is_weighted_and_tight();
    test_greedy_initial_solution_and_binary_validity();
    std::cout << "All weighted FPP combinatorial initial cut tests passed.\n";
    return 0;
}
