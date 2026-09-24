#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "core/LandscapeWeightMap.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "opt/OptimizationInstance.hpp"

namespace {

firebreak::opt::OptimizationInstance make_downstream_value_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_bound_downstream_counterexample";
    opt.alpha = 0.4;
    opt.n_cells = 5;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 3};
    opt.eligible_original_nodes = {2, 4};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{0, 3, 1, 4},
        firebreak::opt::CompactArc{0, 4, 1, 5},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = static_cast<int>(scenario.arcs.size());
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        5152,
        false,
        {
            {1, 1.0, 1.0, 0},
            {2, 0.01, 0.01, 0},
            {3, 100.0, 100.0, 1},
            {4, 10.0, 10.0, 0},
            {5, 1.0, 1.0, 0},
        });
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

double expected_weighted_loss(
    const firebreak::opt::OptimizationInstance& opt,
    std::vector<int> selected_compact_nodes) {
    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    return evaluator.evaluate(std::move(selected_compact_nodes), false).expected_weighted_burn_loss;
}

void test_direct_weight_is_not_downstream_benefit_bound() {
    const auto opt = make_downstream_value_instance();
    const double no_firebreak = expected_weighted_loss(opt, {});
    const double select_low_direct_weight = expected_weighted_loss(opt, {1});
    const double select_higher_direct_weight = expected_weighted_loss(opt, {3});

    const double low_direct_weight = opt.compact_cell_weights[1];
    const double low_candidate_improvement = no_firebreak - select_low_direct_weight;
    const double high_candidate_improvement = no_firebreak - select_higher_direct_weight;

    assert(std::fabs(low_direct_weight - 0.01) <= 1.0e-12);
    assert(low_candidate_improvement > 100.0);
    assert(low_candidate_improvement > low_direct_weight);
    assert(low_candidate_improvement > high_candidate_improvement);
}

void test_zero_current_score_is_not_a_permanent_certificate() {
    const auto opt = make_downstream_value_instance();
    const double no_firebreak = expected_weighted_loss(opt, {});
    const double select_low_direct_weight = expected_weighted_loss(opt, {1});
    assert(select_low_direct_weight < no_firebreak);
}

}  // namespace

int main() {
    test_direct_weight_is_not_downstream_benefit_bound();
    test_zero_current_score_is_not_a_permanent_certificate();
    std::cout << "All weighted restricted bound counterexample tests passed.\n";
    return 0;
}
