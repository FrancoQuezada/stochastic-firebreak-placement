#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/DpvScenarioSubproblem.hpp"
#include "opt/DpvIndexBuilder.hpp"

namespace {

firebreak::opt::OptimizationInstance make_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_path";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

void test_structure_counts() {
    const auto opt = make_path_instance();
    const auto structure = firebreak::benders::analyze_dpv_scenario_subproblem_structure(opt, 0);

    assert(structure.scenario_id == 1);
    assert(structure.x_variable_count == 3);
    assert(structure.z_variable_count == 5);
    assert(structure.y_copy_variable_count == 3);
    assert(structure.total_variable_count == 11);
    assert(structure.y_fix_constraint_count == 3);
    assert(structure.ignition_constraint_count == 1);
    assert(structure.propagation_constraint_count == 2);
    assert(structure.linearization_constraint_count == 15);
    assert(structure.total_constraint_count == 21);
}

#ifdef FIREBREAK_WITH_CPLEX
void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-7);
}

void test_optional_cplex_subproblem_solve_and_cut_tightness() {
    const auto opt = make_path_instance();
    firebreak::benders::DpvScenarioSubproblem subproblem;
    const auto result = subproblem.solve(opt, 0, {0, 1, 0}, false);

    assert(!result.status.empty());
    assert_close(result.objective_value, 0.0);
    assert(result.duals_for_y_copy.size() == 3);

    std::vector<double> compact_y = {0.0, 1.0, 0.0};
    assert_close(result.benders_cut.evaluateAt(compact_y), result.objective_value);
}
#endif

}  // namespace

int main() {
    test_structure_counts();
#ifdef FIREBREAK_WITH_CPLEX
    test_optional_cplex_subproblem_solve_and_cut_tightness();
#endif
    std::cout << "All DPV Benders subproblem structure tests passed.\n";
    return 0;
}
