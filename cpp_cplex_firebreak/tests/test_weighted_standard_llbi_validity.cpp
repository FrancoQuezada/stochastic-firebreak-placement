#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppLiftedLowerBound.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-9) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_parallel_path_instance(int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_standard_llbi_parallel";
    opt.alpha = 0.25;
    opt.n_cells = 4;
    opt.budget = budget;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};
    opt.compact_cell_weights = {1.0, 2.0, 3.0, 50.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 21;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

firebreak::opt::OptimizationInstance make_weighted_cycle_instance(int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_standard_llbi_cycle";
    opt.alpha = 0.2;
    opt.n_cells = 5;
    opt.budget = budget;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 2, 3, 4};
    opt.eligible_original_nodes = {2, 3, 4, 5};
    opt.compact_cell_weights = {1.0, 4.0, 7.0, 11.0, 13.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 22;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 1, 3, 2},
        firebreak::opt::CompactArc{2, 3, 3, 4},
        firebreak::opt::CompactArc{0, 4, 1, 5},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

void test_parallel_path_complementarity_is_conservative() {
    const auto opt = make_weighted_parallel_path_instance(2);
    const auto inequality =
        firebreak::benders::build_fpp_lifted_lower_bound_for_scenario(opt, 0);
    assert_close(inequality.f_empty, 56.0);
    assert_close(inequality.evaluateAt(std::vector<int>{1, 1}, opt), -49.0);

    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    selected[1] = 1;
    selected[2] = 1;
    const auto actual = firebreak::benders::evaluate_fixed_y_fpp_loss(opt, 0, selected);
    assert_close(actual.weighted_burn_loss, 1.0);
    assert(actual.weighted_burn_loss + 1.0e-9 >= inequality.evaluateAtCompact(selected));
}

void test_exhaustive_weighted_validity_on_dag_and_cycle() {
    {
        const auto validation =
            firebreak::benders::validate_fpp_lifted_lower_bound_exhaustive(
                make_weighted_parallel_path_instance(2),
                0,
                2);
        assert(validation.valid);
    }
    {
        const auto validation =
            firebreak::benders::validate_fpp_lifted_lower_bound_exhaustive(
                make_weighted_cycle_instance(2),
                0,
                2);
        assert(validation.valid);
    }
}

}  // namespace

int main() {
    test_parallel_path_complementarity_is_conservative();
    test_exhaustive_weighted_validity_on_dag_and_cycle();
    std::cout << "All weighted standard LLBI validity tests passed.\n";
    return 0;
}
