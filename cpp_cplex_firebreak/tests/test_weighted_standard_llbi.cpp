#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

#include "benders/FppLiftedLowerBound.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-9) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_standard_llbi_path";
    opt.alpha = 0.25;
    opt.n_cells = 4;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {0, 1, 2, 3};
    opt.eligible_original_nodes = {1, 2, 3, 4};
    opt.compact_cell_weights = {1.0, 2.0, 10.0, 5.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 11;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

void test_weighted_downstream_coefficients() {
    const auto opt = make_weighted_path_instance();
    const auto inequality =
        firebreak::benders::build_fpp_lifted_lower_bound_for_scenario(opt, 0);

    assert_close(inequality.f_empty, 18.0);
    assert_close(inequality.rhs_constant, 18.0);
    assert(inequality.nonzero_coefficients == 3);
    assert((inequality.coefficients_by_compact_index[0] == std::pair<int, double>{1, -17.0}));
    assert((inequality.coefficients_by_compact_index[1] == std::pair<int, double>{2, -10.0}));
    assert((inequality.coefficients_by_compact_index[2] == std::pair<int, double>{3, -5.0}));
    assert_close(inequality.evaluateAt(std::vector<int>{0, 1, 0, 0}, opt), 1.0);
}

void test_fixed_y_loss_is_weighted_and_preserves_root_convention() {
    const auto opt = make_weighted_path_instance();
    std::vector<char> none(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    const auto empty_loss = firebreak::benders::evaluate_fixed_y_fpp_loss(opt, 0, none);
    assert_close(empty_loss.burned_area, 4.0);
    assert_close(empty_loss.weighted_burn_loss, 18.0);

    std::vector<char> root_selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    root_selected[0] = 1;
    const auto root_loss =
        firebreak::benders::evaluate_fixed_y_fpp_loss(opt, 0, root_selected);
    assert_close(root_loss.weighted_burn_loss, 18.0);

    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    selected[1] = 1;
    const auto actual = firebreak::benders::evaluate_fixed_y_fpp_loss(opt, 0, selected);
    const auto optimistic =
        firebreak::benders::evaluate_optimistic_singleton_fpp_loss(opt, 0, 1);
    assert_close(actual.weighted_burn_loss, 1.0);
    assert_close(optimistic.weighted_burn_loss, 1.0);
}

void test_precompute_diagnostics() {
    const auto opt = make_weighted_path_instance();
    const auto result = firebreak::benders::build_fpp_lifted_lower_bounds(opt);
    assert(result.weighted);
    assert(!result.weight_map_hash.empty());
    assert(result.scenarios_precomputed == 1);
    assert(result.singletons_evaluated == 4);
    assert_close(result.no_firebreak_loss_min, 18.0);
    assert_close(result.no_firebreak_loss_max, 18.0);
    assert_close(result.singleton_benefit_min, 0.0);
    assert_close(result.singleton_benefit_max, 17.0);
    assert(!result.cache_hit);
    assert(result.validity_mode == "downstream-union-bound");
}

}  // namespace

int main() {
    test_weighted_downstream_coefficients();
    test_fixed_y_loss_is_weighted_and_preserves_root_convention();
    test_precompute_diagnostics();
    std::cout << "All weighted standard LLBI tests passed.\n";
    return 0;
}
