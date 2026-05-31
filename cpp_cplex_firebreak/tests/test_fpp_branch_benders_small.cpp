#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/FppBranchBendersSolver.hpp"
#include "risk/RiskMeasure.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include "benders/FppBendersSolver.hpp"
#include "solver/FppSaaCplexModel.hpp"
#endif

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-6);
}

firebreak::opt::OptimizationInstance make_path_instance(int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_branch_benders_path";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = budget;
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

    opt.total_arcs = scenario.arcs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_two_scenario_risk_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_branch_benders_risk";
    opt.alpha = 2.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};

    firebreak::opt::OptimizationScenario first;
    first.scenario_id = 1;
    first.probability = 0.5;
    first.ignition_index = 0;
    first.ignition_original_node = 1;
    first.observed_node_indices = {0, 1, 2};
    first.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    first.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 2;
    second.probability = 0.5;
    second.ignition_index = 0;
    second.ignition_original_node = 1;
    second.observed_node_indices = {0, 1, 2};
    second.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
    second.arcs.push_back(firebreak::opt::CompactArc{2, 1, 3, 2});

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.5, 0.5};
    opt.total_arcs = first.arcs.size() + second.arcs.size();
    return opt;
}

void test_master_structure() {
    const auto opt = make_path_instance(1);
    const auto structure =
        firebreak::benders::analyze_fpp_branch_benders_master_structure(opt);

    assert(structure.y_variable_count == 3);
    assert(structure.eta_variable_count == 1);
    assert(structure.total_variable_count == 4);
    assert(structure.budget_constraint_count == 1);
    assert(structure.base_constraint_count == 1);
    assert(!structure.has_scenario_recourse_variables);
}

void test_cvar_master_structure() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::CVaR;
    config.cvarBeta = 0.5;

    const auto expected_structure =
        firebreak::benders::analyze_fpp_branch_benders_master_structure(opt);
    assert(expected_structure.y_variable_count == 2);
    assert(expected_structure.eta_variable_count == 2);
    assert(expected_structure.total_variable_count == 4);
    assert(expected_structure.risk_constraint_count == 0);

    const auto cvar_structure =
        firebreak::benders::analyze_fpp_branch_benders_master_structure(opt, config);
    assert(cvar_structure.y_variable_count == 2);
    assert(cvar_structure.eta_variable_count == 2);
    assert(cvar_structure.risk_threshold_variable_count == 1);
    assert(cvar_structure.cvar_excess_variable_count == 2);
    assert(cvar_structure.total_variable_count == 7);
    assert(cvar_structure.risk_constraint_count == 2);
    assert(!cvar_structure.has_scenario_recourse_variables);
}

void test_mean_cvar_master_structure() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    config.cvarBeta = 0.5;
    config.cvarLambda = 0.5;

    const auto structure =
        firebreak::benders::analyze_fpp_branch_benders_master_structure(opt, config);
    assert(structure.risk_threshold_variable_count == 1);
    assert(structure.cvar_excess_variable_count == 2);
    assert(structure.total_variable_count == 7);
    assert(structure.risk_constraint_count == 2);
}

void test_benders_cut_algebra_for_callback_form() {
    firebreak::benders::BendersCut cut;
    cut.subproblem_objective = 4.0;
    cut.coefficients_by_compact_index = {{0, -1.0}, {1, -2.0}, {2, 0.5}};
    const std::vector<double> ybar = {1.0, 0.0, 1.0};
    const double dual_dot_ybar = -1.0 + 0.5;
    cut.rhs_constant = cut.subproblem_objective - dual_dot_ybar;

    assert_close(cut.rhs_constant, 4.5);
    assert_close(cut.evaluateAt(ybar), cut.subproblem_objective);
    assert_close(cut.violationAt(3.0, ybar), 1.0);
}

#ifdef FIREBREAK_WITH_CPLEX
firebreak::opt::OptimizationInstance make_alternative_optima_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_branch_benders_alt";
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

    opt.total_arcs = scenario.arcs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::solver::ModelResult solve_callback(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config = firebreak::risk::RiskMeasureConfig(),
    bool use_lifted_lower_bounds = false,
    bool use_root_user_cuts = false) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    options.use_lifted_lower_bounds = use_lifted_lower_bounds;
    options.use_root_user_cuts = use_root_user_cuts;
    options.root_user_cut_max_rounds = 1;
    options.root_user_cut_tolerance = 1.0e-7;
    return solver.solve(opt, options);
}

firebreak::solver::ModelResult solve_combinatorial_callback(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config = firebreak::risk::RiskMeasureConfig()) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    options.combinatorial_options.enabled = true;
    options.combinatorial_options.lift_mode =
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic;
    options.combinatorial_options.cut_sampling_ratio = 0.10;
    options.combinatorial_options.separate_fractional = true;
    options.combinatorial_options.initial_cuts = true;
    return solver.solve(opt, options);
}

void assert_combinatorial_diagnostics(
    const firebreak::solver::ModelResult& result,
    std::size_t scenario_count) {
    assert(result.combinatorial_benders_enabled);
    assert(result.combinatorial_benders_lift_mode == "heuristic");
    assert_close(result.combinatorial_benders_cut_sampling_ratio, 0.10);
    assert(result.combinatorial_benders_fractional_separation_enabled);
    assert(result.combinatorial_benders_initial_cuts_enabled);
    assert(result.combinatorial_benders_initial_cuts_added ==
           static_cast<int>(scenario_count));
    assert(result.combinatorial_benders_integer_cuts_added >= 0);
    assert(result.combinatorial_benders_fractional_cuts_added >= 0);
    assert(result.combinatorial_benders_scenarios_checked >= 0);
    assert(result.combinatorial_benders_separation_time_sec >= 0.0);
    assert(result.combinatorial_benders_avg_paths_per_cut >= 0.0);
    assert(result.combinatorial_benders_avg_cut_nonzeros >= 0.0);
    assert(result.combinatorial_benders_num_violated_cuts >= 0);
    assert(result.branch_benders_subproblems_solved == 0);
}

void compare_callback_to_references(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config = firebreak::risk::RiskMeasureConfig(),
    bool use_lifted_lower_bounds = false,
    bool use_root_user_cuts = false) {
    firebreak::solver::FppSaaCplexModel monolithic;
    const auto monolithic_result =
        monolithic.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, risk_config);

    firebreak::benders::FppBendersSolver explicit_benders;
    firebreak::benders::FppBendersOptions explicit_options;
    explicit_options.max_iterations = 20;
    explicit_options.tolerance = 1.0e-7;
    explicit_options.time_limit_seconds = 30.0;
    explicit_options.mip_gap = 0.0;
    explicit_options.threads = 1;
    explicit_options.risk_config = risk_config;
    const auto explicit_result = explicit_benders.solve(opt, explicit_options);

    const auto callback_result = solve_callback(
        opt,
        risk_config,
        use_lifted_lower_bounds,
        use_root_user_cuts);

    assert(callback_result.status == "Optimal");
    assert_close(callback_result.objective_value, monolithic_result.objective_value);
    assert_close(callback_result.objective_value, explicit_result.objective_value);
    assert_close(callback_result.expected_loss_component, monolithic_result.expected_loss_component);
    assert_close(callback_result.expected_loss_component, explicit_result.expected_loss_component);
    if (risk_config.type != firebreak::risk::RiskMeasureType::Expected) {
        assert_close(callback_result.cvar_loss_component, monolithic_result.cvar_loss_component);
        assert_close(callback_result.cvar_loss_component, explicit_result.cvar_loss_component);
    }
    assert(callback_result.branch_benders_enabled);
    assert(callback_result.branch_benders_candidate_callback_calls >=
           callback_result.branch_benders_candidate_incumbents_checked);
    assert(callback_result.branch_benders_candidate_incumbents_checked > 0);
    assert(callback_result.branch_benders_subproblems_attempted >=
           callback_result.branch_benders_subproblems_solved);
    assert(callback_result.branch_benders_subproblems_solved > 0);
    assert(callback_result.branch_benders_subproblem_time_sec >= 0.0);
    assert(callback_result.branch_benders_average_subproblem_time_sec >= 0.0);
    assert(callback_result.branch_benders_max_subproblem_time_sec >= 0.0);
    assert(callback_result.branch_benders_callback_time_sec >= 0.0);
    assert(callback_result.branch_benders_cut_construction_time_sec >= 0.0);
    assert(callback_result.branch_benders_lazy_cut_insertion_time_sec >= 0.0);
    assert(callback_result.branch_benders_violated_cuts >= 0);
    assert(callback_result.branch_benders_nonviolated_cuts >= 0);
    assert(callback_result.branch_benders_skipped_cuts >= 0);
    assert(callback_result.branch_benders_duplicate_cuts >= 0);
    assert(callback_result.branch_benders_max_cut_violation <= 1.0e-6);
    assert(static_cast<int>(callback_result.selected_firebreak_indices.size()) <= opt.budget);
    assert(callback_result.benders_use_lifted_lower_bounds == use_lifted_lower_bounds);
    assert(callback_result.branch_benders_use_root_user_cuts == use_root_user_cuts);
    if (use_lifted_lower_bounds) {
        assert(callback_result.benders_lifted_lower_bound_count ==
               static_cast<int>(opt.scenarios.size()));
    }
    if (use_root_user_cuts) {
        assert(callback_result.branch_benders_root_user_cut_only_at_root_confirmed);
        assert(callback_result.branch_benders_root_user_cut_rounds_executed <= 1);
    }
}

void test_tiny_callback_solve_matches_references() {
    const auto opt = make_path_instance(1);
    const auto result = solve_callback(opt);
    assert(result.branch_benders_lazy_cuts_added > 0);
    compare_callback_to_references(opt);
    compare_callback_to_references(opt, firebreak::risk::RiskMeasureConfig(), true, false);
    compare_callback_to_references(opt, firebreak::risk::RiskMeasureConfig(), false, true);
    compare_callback_to_references(opt, firebreak::risk::RiskMeasureConfig(), true, true);
}

void test_zero_budget() {
    const auto opt = make_path_instance(0);
    const auto result = solve_callback(opt);

    assert(result.status == "Optimal");
    assert_close(result.objective_value, 3.0);
    assert(result.selected_firebreak_indices.empty());
    assert(result.branch_benders_max_cut_violation <= 1.0e-6);
}

void test_alternative_optima_objective() {
    const auto opt = make_alternative_optima_instance();
    compare_callback_to_references(opt);
}

void test_cvar_callback_matches_references() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::CVaR;
    config.cvarBeta = 0.5;
    compare_callback_to_references(opt, config);
    compare_callback_to_references(opt, config, true, false);
    compare_callback_to_references(opt, config, false, true);
    compare_callback_to_references(opt, config, true, true);

    const auto result = solve_callback(opt, config);
    assert_close(result.objective_value, 2.0);
    assert_close(result.expected_loss_component, 1.5);
    assert_close(result.cvar_loss_component, 2.0);
    assert(result.branch_benders_max_cut_violation <= 1.0e-6);
}

void test_mean_cvar_callback_matches_references() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    config.cvarBeta = 0.5;
    config.cvarLambda = 0.5;
    compare_callback_to_references(opt, config);
    compare_callback_to_references(opt, config, true, true);

    const auto result = solve_callback(opt, config);
    assert_close(result.objective_value, 1.75);
    assert_close(result.expected_loss_component, 1.5);
    assert_close(result.cvar_loss_component, 2.0);
    assert(result.branch_benders_max_cut_violation <= 1.0e-6);
}

void test_combinatorial_callback_expected_cvar_and_mean_cvar() {
    const auto expected_opt = make_path_instance(1);
    firebreak::solver::FppSaaCplexModel monolithic;

    const auto expected_reference =
        monolithic.solve(expected_opt, 30.0, 0.0, 1, false);
    const auto expected_result = solve_combinatorial_callback(expected_opt);
    assert(expected_result.status == "Optimal");
    assert_close(expected_result.objective_value, expected_reference.objective_value);
    assert_close(expected_result.objective_value, 1.0);
    assert_combinatorial_diagnostics(expected_result, expected_opt.scenarios.size());

    const auto risk_opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    const auto cvar_reference =
        monolithic.solve(risk_opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, cvar);
    const auto cvar_result = solve_combinatorial_callback(risk_opt, cvar);
    assert(cvar_result.status == "Optimal");
    assert_close(cvar_result.objective_value, cvar_reference.objective_value);
    assert_close(cvar_result.expected_loss_component, 1.5);
    assert_close(cvar_result.cvar_loss_component, 2.0);
    assert_combinatorial_diagnostics(cvar_result, risk_opt.scenarios.size());

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.5;
    const auto mean_reference =
        monolithic.solve(risk_opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, mean_cvar);
    const auto mean_result = solve_combinatorial_callback(risk_opt, mean_cvar);
    assert(mean_result.status == "Optimal");
    assert_close(mean_result.objective_value, mean_reference.objective_value);
    assert_close(mean_result.objective_value, 1.75);
    assert_close(mean_result.expected_loss_component, 1.5);
    assert_close(mean_result.cvar_loss_component, 2.0);
    assert_combinatorial_diagnostics(mean_result, risk_opt.scenarios.size());
}

void test_validated_strengthening_options() {
    const auto opt = make_two_scenario_risk_instance();
    const std::vector<firebreak::risk::RiskMeasureConfig> risk_configs = {
        firebreak::risk::RiskMeasureConfig(),
        [] {
            firebreak::risk::RiskMeasureConfig config;
            config.type = firebreak::risk::RiskMeasureType::CVaR;
            config.cvarBeta = 0.5;
            return config;
        }(),
        [] {
            firebreak::risk::RiskMeasureConfig config;
            config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
            config.cvarBeta = 0.5;
            config.cvarLambda = 0.5;
            return config;
        }(),
    };

    for (const auto& risk_config : risk_configs) {
        firebreak::benders::FppBranchBendersSolver solver;
        firebreak::benders::FppBranchBendersOptions options;
        options.tolerance = 1.0e-7;
        options.time_limit_seconds = 30.0;
        options.mip_gap = 0.0;
        options.threads = 1;
        options.risk_config = risk_config;
        options.strengthening_options.use_coverage_llbi = true;
        options.strengthening_options.use_path_llbi = true;
        options.strengthening_options.use_conditional_zero_benefit_fixing = true;

        const auto result = solver.solve(opt, options);
        assert(result.status == "Optimal");
        assert(result.coverage_llbi_enabled);
        assert(result.coverage_llbi_num_zeta_vars > 0);
        assert(result.coverage_llbi_num_constraints > 0);
        assert(result.coverage_llbi_precompute_time_sec >= 0.0);
        assert(result.path_llbi_enabled);
        assert(result.path_llbi_num_b_vars > 0);
        assert(result.path_llbi_num_path_constraints > 0);
        assert(result.path_llbi_num_paths_used > 0);
        assert(result.path_llbi_precompute_time_sec >= 0.0);
        assert(result.conditional_zero_benefit_enabled);
        assert(result.conditional_zero_benefit_fixings_attempted == 0);
        assert(result.conditional_zero_benefit_fixings_applied == 0);
        assert(result.conditional_zero_benefit_time_sec >= 0.0);
    }
}
#endif

}  // namespace

int main() {
    test_master_structure();
    test_cvar_master_structure();
    test_mean_cvar_master_structure();
    test_benders_cut_algebra_for_callback_form();
#ifdef FIREBREAK_WITH_CPLEX
    test_tiny_callback_solve_matches_references();
    test_zero_budget();
    test_alternative_optima_objective();
    test_cvar_callback_matches_references();
    test_mean_cvar_callback_matches_references();
    test_combinatorial_callback_expected_cvar_and_mean_cvar();
    test_validated_strengthening_options();
#else
    std::cout << "Skipping tiny FPP Branch-Benders solve tests because CPLEX is not enabled.\n";
#endif
    std::cout << "All FPP Branch-Benders tests passed.\n";
    return 0;
}
