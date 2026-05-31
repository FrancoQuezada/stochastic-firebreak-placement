#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "benders/DpvBranchBendersSolver.hpp"
#include "opt/DpvIndexBuilder.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include "benders/DpvBendersSolver.hpp"
#include "benders/DpvScenarioSubproblem.hpp"
#include "solver/DpvSaaCplexModel.hpp"
#endif

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

#ifdef FIREBREAK_WITH_CPLEX

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-6);
}

firebreak::opt::OptimizationInstance make_parallel_choice_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_parallel_choice";
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
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

#endif

void test_master_structure() {
    const auto opt = make_path_instance();
    const auto structure = firebreak::benders::analyze_dpv_branch_benders_master_structure(opt);

    assert(structure.y_variable_count == opt.eligible_indices.size());
    assert(structure.eta_variable_count == opt.scenarios.size());
    assert(structure.total_variable_count == opt.eligible_indices.size() + opt.scenarios.size());
    assert(structure.budget_constraint_count == 1);
    assert(structure.base_constraint_count == 1);
    assert(!structure.has_scenario_recourse_variables);
}

#ifdef FIREBREAK_WITH_CPLEX

firebreak::solver::ModelResult solve_branch_benders(
    const firebreak::opt::OptimizationInstance& opt,
    double tolerance = 1.0e-7,
    bool use_root_user_cuts = false) {
    firebreak::benders::DpvBranchBendersSolver solver;
    firebreak::benders::DpvBranchBendersOptions options;
    options.tolerance = tolerance;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.use_root_user_cuts = use_root_user_cuts;
    options.root_user_cut_max_rounds = 1;
    options.root_user_cut_tolerance = tolerance;
    return solver.solve(opt, options);
}

firebreak::solver::ModelResult solve_explicit_benders(
    const firebreak::opt::OptimizationInstance& opt,
    double tolerance = 1.0e-7) {
    firebreak::benders::DpvBendersSolver solver;
    firebreak::benders::DpvBendersOptions options;
    options.max_iterations = 20;
    options.tolerance = tolerance;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    return solver.solve(opt, options);
}

void assert_clean_branch_benders_result(const firebreak::solver::ModelResult& result) {
    assert(result.status == "Optimal");
    assert(result.branch_benders_enabled);
    assert(result.branch_benders_candidate_callback_calls >=
           result.branch_benders_candidate_incumbents_checked);
    assert(result.branch_benders_candidate_incumbents_checked > 0);
    assert(result.branch_benders_subproblems_attempted >=
           result.branch_benders_subproblems_solved);
    assert(result.branch_benders_subproblems_solved > 0);
    assert(result.branch_benders_subproblem_time_sec >= 0.0);
    assert(result.branch_benders_average_subproblem_time_sec >= 0.0);
    assert(result.branch_benders_max_subproblem_time_sec >= 0.0);
    assert(result.branch_benders_callback_time_sec >= 0.0);
    assert(result.branch_benders_cut_construction_time_sec >= 0.0);
    assert(result.branch_benders_lazy_cut_insertion_time_sec >= 0.0);
    assert(result.branch_benders_violated_cuts >= 0);
    assert(result.branch_benders_nonviolated_cuts >= 0);
    assert(result.branch_benders_skipped_cuts >= 0);
    assert(result.branch_benders_duplicate_cuts >= 0);
    assert(result.branch_benders_max_cut_violation <= 1.0e-6);
}

void test_callback_benders_matches_monolithic_and_explicit_on_tiny_path() {
    const auto opt = make_path_instance();
    firebreak::solver::DpvSaaCplexModel monolithic;
    const auto monolithic_result = monolithic.solve(opt, 30.0, 0.0, 1, false);
    const auto explicit_result = solve_explicit_benders(opt);
    const auto callback_result = solve_branch_benders(opt);

    assert_clean_branch_benders_result(callback_result);
    assert_close(callback_result.objective_value, monolithic_result.objective_value);
    assert_close(callback_result.objective_value, explicit_result.objective_value);
    assert(callback_result.selected_firebreak_original_nodes.size() <=
           static_cast<std::size_t>(opt.budget));
}

void test_root_user_cut_disabled_regression() {
    const auto result = solve_branch_benders(make_path_instance(), 1.0e-7, false);

    assert_clean_branch_benders_result(result);
    assert(!result.branch_benders_use_root_user_cuts);
    assert(result.branch_benders_root_user_cut_rounds_executed == 0);
    assert(result.branch_benders_root_user_cuts_added == 0);
    assert(result.branch_benders_root_user_cut_skipped_reason.find("disabled") != std::string::npos);
}

void test_root_user_cut_enabled_tiny_solve() {
    const auto opt = make_path_instance();
    const auto baseline = solve_branch_benders(opt, 1.0e-7, false);
    const auto root_cut_result = solve_branch_benders(opt, 1.0e-7, true);

    assert(root_cut_result.status == "Optimal");
    assert(root_cut_result.branch_benders_enabled);
    assert(root_cut_result.branch_benders_subproblems_solved > 0);
    assert(root_cut_result.branch_benders_max_cut_violation <= 1.0e-6);
    assert_close(root_cut_result.objective_value, baseline.objective_value);
    assert(root_cut_result.branch_benders_use_root_user_cuts);
    assert(root_cut_result.branch_benders_root_user_cut_max_rounds == 1);
    assert(std::fabs(root_cut_result.branch_benders_root_user_cut_tolerance - 1.0e-7) <= 1.0e-12);
    assert(root_cut_result.branch_benders_root_user_cut_rounds_executed <= 1);
    assert(root_cut_result.branch_benders_root_user_cut_only_at_root_confirmed);
    if (root_cut_result.branch_benders_root_user_cut_rounds_executed > 0) {
        assert(root_cut_result.branch_benders_root_user_cut_callback_calls > 0);
        assert(root_cut_result.branch_benders_root_user_cut_scenarios_solved > 0);
        assert(!root_cut_result.branch_benders_root_user_cut_round_log.empty());
    }
}

void test_fractional_subproblem_cut_algebra() {
    const auto opt = make_path_instance();
    firebreak::benders::DpvScenarioSubproblem subproblem;
    const std::vector<double> ybar = {0.5, 0.0, 0.0};
    const auto sub_result = subproblem.solveFractional(opt, 0, ybar, false);

    double dual_dot_ybar = 0.0;
    std::vector<double> compact_y(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        compact_y[static_cast<std::size_t>(opt.eligible_indices[pos])] = ybar[pos];
        dual_dot_ybar += sub_result.duals_for_y_copy[pos] * ybar[pos];
        assert_close(
            sub_result.benders_cut.coefficients_by_compact_index[pos].second,
            sub_result.duals_for_y_copy[pos]);
    }

    assert_close(
        sub_result.benders_cut.rhs_constant,
        sub_result.objective_value - dual_dot_ybar);
    assert_close(sub_result.benders_cut.evaluateAt(compact_y), sub_result.objective_value);
    assert_close(
        sub_result.benders_cut.violationAt(0.0, compact_y),
        sub_result.objective_value);
}

void test_lazy_cut_generation() {
    const auto result = solve_branch_benders(make_path_instance());

    assert_clean_branch_benders_result(result);
    assert(result.branch_benders_lazy_cuts_added > 0);
    assert(result.cuts_added == result.branch_benders_lazy_cuts_added);
}

void test_callback_benders_zero_budget() {
    auto opt = make_path_instance();
    opt.alpha = 0.0;
    opt.budget = 0;

    firebreak::solver::DpvSaaCplexModel monolithic;
    const auto monolithic_result = monolithic.solve(opt, 30.0, 0.0, 1, false);
    const auto callback_result = solve_branch_benders(opt);

    assert_clean_branch_benders_result(callback_result);
    assert(callback_result.selected_firebreak_original_nodes.empty());
    assert_close(callback_result.objective_value, monolithic_result.objective_value);
    assert_close(callback_result.objective_value, 5.0);
}

void test_callback_benders_alternative_optima() {
    const auto opt = make_parallel_choice_instance();
    firebreak::solver::DpvSaaCplexModel monolithic;
    const auto monolithic_result = monolithic.solve(opt, 30.0, 0.0, 1, false);
    const auto callback_result = solve_branch_benders(opt);

    assert_clean_branch_benders_result(callback_result);
    assert_close(callback_result.objective_value, monolithic_result.objective_value);
    assert_close(callback_result.objective_value, 2.0);
    assert(callback_result.selected_firebreak_original_nodes.size() == 1);
    const int selected = callback_result.selected_firebreak_original_nodes.front();
    assert(selected == 2 || selected == 3);
}

#else

void test_non_cplex_failure_is_clear() {
    firebreak::benders::DpvBranchBendersSolver solver;
    firebreak::benders::DpvBranchBendersOptions options;
    bool threw = false;
    try {
        (void)solver.solve(make_path_instance(), options);
    } catch (const std::runtime_error& exc) {
        threw = true;
        const std::string message = exc.what();
        assert(message.find("CPLEX") != std::string::npos ||
               message.find("cplex") != std::string::npos);
    }
    assert(threw);
}

#endif

}  // namespace

int main() {
    test_master_structure();
#ifdef FIREBREAK_WITH_CPLEX
    test_callback_benders_matches_monolithic_and_explicit_on_tiny_path();
    test_root_user_cut_disabled_regression();
    test_root_user_cut_enabled_tiny_solve();
    test_fractional_subproblem_cut_algebra();
    test_lazy_cut_generation();
    test_callback_benders_zero_budget();
    test_callback_benders_alternative_optima();
    std::cout << "All tiny DPV Branch-Benders tests passed.\n";
#else
    test_non_cplex_failure_is_clear();
    std::cout << "Skipping tiny DPV Branch-Benders solve tests because CPLEX is not enabled.\n";
#endif
    return 0;
}
