#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"
#include "core/LandscapeWeightMap.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_chain() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_lifting_chain";
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};

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
    opt.total_arcs = scenario.arcs.size();

    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        6201,
        false,
        {
            {1, 100.0, 100.0, 0},
            {2, 2.0, 2.0, 0},
            {3, 50.0, 50.0, 1},
            {4, 7.0, 7.0, 0},
        });
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

void test_manual_weighted_lifting_fields(
    firebreak::benders::FppCombinatorialBendersLiftMode mode) {
    const auto opt = make_weighted_chain();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto cut = separator.separateScenario(
        0,
        std::vector<double>{0.0, 0.0},
        0.0,
        false,
        mode,
        1.0e-7);

    assert_close(cut.incumbent_weighted_loss, 159.0);
    assert_close(cut.baseline_cut.rhs_constant, 159.0);
    assert_close(cut.cut.rhs_constant, 159.0);
    assert_close(cut.baseline_rhs_at_ybar, 159.0);
    assert_close(cut.lifted_rhs_at_ybar, 159.0);
    assert_close(cut.baseline_tightness_error, 0.0);
    assert_close(cut.lifted_tightness_error, 0.0);
    assert(cut.lifted_dominates_baseline);
    assert(cut.propagation_evaluations_for_lifting == 0);

    assert(cut.baseline_cut.coefficients_by_compact_index.size() == 2);
    assert(cut.cut.coefficients_by_compact_index.size() == 2);
    assert(cut.baseline_cut.coefficients_by_compact_index[0].first == 1);
    assert(cut.baseline_cut.coefficients_by_compact_index[1].first == 2);
    assert_close(cut.baseline_cut.coefficients_by_compact_index[0].second, -59.0);
    assert_close(cut.baseline_cut.coefficients_by_compact_index[1].second, -57.0);
    assert_close(cut.cut.coefficients_by_compact_index[0].second, -59.0);
    assert_close(cut.cut.coefficients_by_compact_index[1].second, -57.0);

    if (mode == firebreak::benders::FppCombinatorialBendersLiftMode::None) {
        assert(!cut.lifting_attempted);
        assert(!cut.lifting_success);
    } else {
        assert(cut.lifting_attempted);
        assert(cut.lifting_success);
        assert(!cut.lifting_failure);
        assert(cut.candidates_considered_for_lifting == 2);
    }
}

void test_lifting_summary_diagnostics() {
    const auto opt = make_weighted_chain();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto summary = separator.separateViolatedCuts(
        std::vector<double>{0.0, 0.0},
        std::vector<double>{0.0},
        false,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending,
        1.0,
        1.0e-7);
    assert(summary.scenarios_checked == 1);
    assert(summary.violated_cuts == 1);
    assert(summary.lifting_attempts == 1);
    assert(summary.lifting_successes == 1);
    assert(summary.lifting_failures == 0);
    assert(summary.candidates_considered_for_lifting == 2);
    assert(summary.baseline_cut_nonzeros == 2);
    assert(summary.lifted_cut_nonzeros == 2);
    assert(summary.lifted_cuts_dominating_baseline == 1);
    assert(summary.max_baseline_tightness_error <= 1.0e-7);
    assert(summary.max_lifted_tightness_error <= 1.0e-7);
}

}  // namespace

int main() {
    test_manual_weighted_lifting_fields(
        firebreak::benders::FppCombinatorialBendersLiftMode::None);
    test_manual_weighted_lifting_fields(
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic);
    test_manual_weighted_lifting_fields(
        firebreak::benders::FppCombinatorialBendersLiftMode::Posterior);
    test_lifting_summary_diagnostics();
    std::cout << "All weighted FPP combinatorial lifting tests passed.\n";
    return 0;
}
