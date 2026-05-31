#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

#include "opt/DpvIndexBuilder.hpp"
#include "solver/DpvSaaCplexModel.hpp"

namespace {

firebreak::opt::OptimizationInstance make_structure_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
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
    scenario.message_filename = "synthetic.csv";
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

void test_variable_and_constraint_counts() {
    const auto opt = make_structure_instance();
    const auto structure = firebreak::solver::analyze_dpv_saa_model_structure(opt);

    assert(structure.x_variable_count == 3);
    assert(structure.y_variable_count == 3);
    assert(structure.z_variable_count == 8);
    assert(structure.total_variable_count == 14);
    assert(structure.budget_constraint_count == 1);
    assert(structure.ignition_constraint_count == 1);
    assert(structure.propagation_constraint_count == 3);
    assert(structure.linearization_constraint_count == 24);
    assert(structure.total_constraint_count == 29);
}

void test_propagation_descriptors_and_y_mapping() {
    const auto opt = make_structure_instance();
    const auto structure = firebreak::solver::analyze_dpv_saa_model_structure(opt);

    assert((structure.y_indices == std::vector<int>{0, 1, 2}));
    assert(structure.has_y_for_node_index(0));
    assert(structure.has_y_for_node_index(1));
    assert(structure.has_y_for_node_index(2));
    assert(!structure.has_y_for_node_index(3));

    assert(structure.propagation_constraints.size() == 3);
    for (const auto& descriptor : structure.propagation_constraints) {
        assert(descriptor.target_has_firebreak_variable);
        assert(descriptor.target_y_position >= 0);
    }
}

void test_objective_pair_multiplicity_is_preserved() {
    const auto opt = make_structure_instance();
    const auto structure = firebreak::solver::analyze_dpv_saa_model_structure(opt);

    assert(structure.objective_terms.size() == 8);

    std::map<std::pair<int, int>, int> successor_descendant_counts;
    for (const auto& term : structure.objective_terms) {
        successor_descendant_counts[{term.successor_index, term.descendant_index}] += 1;
    }

    assert(successor_descendant_counts[std::make_pair(2, 1)] == 2);
    assert(successor_descendant_counts[std::make_pair(2, 2)] == 2);
}

#ifdef FIREBREAK_WITH_CPLEX
void test_optional_tiny_cplex_solve() {
    const auto opt = make_structure_instance();
    firebreak::solver::DpvSaaCplexModel model;
    const auto result = model.solve(opt, 30.0, 0.0, 1, false);

    assert(!result.status.empty());
    assert(result.objective_value >= 0.0);
    assert(result.selected_firebreak_original_nodes.size() <= 1);
}
#endif

}  // namespace

int main() {
    test_variable_and_constraint_counts();
    test_propagation_descriptors_and_y_mapping();
    test_objective_pair_multiplicity_is_preserved();
#ifdef FIREBREAK_WITH_CPLEX
    test_optional_tiny_cplex_solve();
#endif
    std::cout << "All DPV-SAA model structure tests passed.\n";
    return 0;
}
