#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/DpvBendersSolver.hpp"
#include "benders/DpvBranchBendersSolver.hpp"
#include "benders/DpvLiftedLowerBound.hpp"
#include "benders/DpvScenarioSubproblem.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "io/ExperimentResultWriter.hpp"
#include "opt/DpvIndexBuilder.hpp"
#include "solver/DpvSaaCplexModel.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_dpv_path";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};
    opt.compact_cell_weights = {1.0, 2.0, 5.0};
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        7,
        false,
        {
            {1, 1.0, 1.0, 0},
            {2, 2.0, 2.0, 0},
            {3, 5.0, 5.0, 1},
        });

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.message_filename = "weighted_dpv_path.csv";
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    };

    firebreak::opt::DpvIndexBuilder builder;
    scenario.dpv = builder.build_for_scenario(scenario, opt.node_mapper);
    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    return opt;
}

void test_weighted_dpv_saa_structure() {
    const auto opt = make_weighted_path_instance();
    const auto structure = firebreak::solver::analyze_dpv_saa_model_structure(opt);
    assert(structure.objective_terms.size() == 5);
    double total = 0.0;
    for (const auto& term : structure.objective_terms) {
        total += term.coefficient;
        assert_close(term.coefficient, opt.compact_cell_weights[term.descendant_index]);
    }
    assert_close(total, 15.0);
}

void test_weighted_dpv_llbi_values() {
    const auto opt = make_weighted_path_instance();
    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    const auto empty_loss = firebreak::benders::evaluate_fixed_y_dpv_loss(opt, 0, selected);
    assert_close(empty_loss.loss, 15.0);

    selected[2] = 1;
    const auto node3_loss = firebreak::benders::evaluate_fixed_y_dpv_loss(opt, 0, selected);
    assert_close(node3_loss.loss, 3.0);

    const auto singleton = firebreak::benders::evaluate_optimistic_singleton_dpv_loss(opt, 0, 2);
    assert_close(singleton.loss, 3.0);

    const auto inequality = firebreak::benders::build_dpv_lifted_lower_bound_for_scenario(opt, 0);
    assert_close(inequality.f_empty, 15.0);
    assert_close(inequality.evaluateAtCompact(selected), 3.0);

    const auto validation =
        firebreak::benders::validate_dpv_lifted_lower_bound_exhaustive(opt, 0, opt.budget);
    assert(validation.valid);
}

#ifdef FIREBREAK_WITH_CPLEX
void test_weighted_dpv_subproblem_and_cut() {
    const auto opt = make_weighted_path_instance();
    firebreak::benders::DpvScenarioSubproblem subproblem;
    const auto result = subproblem.solve(opt, 0, {0, 0, 1}, false);
    assert_close(result.objective_value, 3.0);
    const auto compact_y = std::vector<double>{0.0, 0.0, 1.0};
    assert_close(result.benders_cut.violationAt(3.0, compact_y), 0.0);
}

void test_weighted_dpv_cplex_solvers_agree() {
    const auto opt = make_weighted_path_instance();
    firebreak::solver::DpvSaaCplexModel direct;
    const auto saa = direct.solve(opt, 30.0, 0.0, 1, false);
    assert_close(saa.objective_value, 0.0);
    assert(saa.dpv_model_weighted);
    assert(saa.dpv_weight_profile == "heterogeneous");
    assert(saa.dpv_ignition_policy == "fpp_ignition_no_protection");

    firebreak::benders::DpvBendersOptions benders_options;
    benders_options.max_iterations = 10;
    benders_options.use_lifted_lower_bounds = true;
    firebreak::benders::DpvBendersSolver benders;
    const auto benders_result = benders.solve(opt, benders_options);
    assert_close(benders_result.objective_value, saa.objective_value);
    assert(benders_result.dpv_llbi_weighted);

    firebreak::benders::DpvBranchBendersOptions branch_options;
    branch_options.use_lifted_lower_bounds = true;
    branch_options.use_root_user_cuts = true;
    firebreak::benders::DpvBranchBendersSolver branch;
    const auto branch_result = branch.solve(opt, branch_options);
    assert_close(branch_result.objective_value, saa.objective_value);
    assert(branch_result.dpv_model_type == "callback_dpv_branch_benders");
}
#endif

void test_weighted_dpv_reporting_fields() {
    firebreak::io::StandardExperimentResult result;
    result.dpv_weighted = true;
    result.dpv_model_weighted = true;
    result.dpv_model_type = "direct_dpv_saa";
    result.dpv_variant = "solution_dependent_product_pair_loss";
    result.dpv_risk_measure = "expected";
    result.dpv_surrogate_objective = 1.25;
    result.dpv_surrogate_best_bound = 1.0;
    result.dpv_surrogate_gap = 0.2;
    result.dpv_benders_cuts_added = 3;
    result.dpv_llbi_weighted = true;
    assert(result.dpv_model_weighted);
    assert(result.dpv_benders_cuts_added == 3);
}

}  // namespace

int main() {
    test_weighted_dpv_saa_structure();
    test_weighted_dpv_llbi_values();
#ifdef FIREBREAK_WITH_CPLEX
    test_weighted_dpv_subproblem_and_cut();
    test_weighted_dpv_cplex_solvers_agree();
#endif
    test_weighted_dpv_reporting_fields();
    std::cout << "All weighted DPV optimization tests passed.\n";
    return 0;
}
