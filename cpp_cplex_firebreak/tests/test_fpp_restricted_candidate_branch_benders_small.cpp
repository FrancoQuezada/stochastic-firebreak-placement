#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "benders/FppRestrictedCandidateBranchBendersSolver.hpp"
#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include "benders/FppBranchBendersSolver.hpp"
#endif

namespace {

template <typename Fn>
void assert_throws(Fn fn, const std::string& label) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        std::cerr << "Expected exception was not thrown: " << label << "\n";
    }
    assert(threw);
}

firebreak::opt::OptimizationInstance make_path_instance(int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_restricted_branch_benders_path";
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
    opt.landscape_name = "synthetic_fpp_restricted_branch_benders_risk";
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

firebreak::benders::FppRestrictedCandidateBranchBendersOptions restricted_options(
    std::vector<int> initial_active_candidates,
    bool eventually_activate_all = true) {
    firebreak::benders::FppRestrictedCandidateBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.initial_active_candidates = std::move(initial_active_candidates);
    options.eventually_activate_all = eventually_activate_all;
    return options;
}

firebreak::risk::RiskMeasureConfig cvar_config() {
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::CVaR;
    config.cvarBeta = 0.5;
    config.cvarLambda = 1.0;
    return config;
}

firebreak::risk::RiskMeasureConfig mean_cvar_config() {
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    config.cvarBeta = 0.5;
    config.cvarLambda = 0.5;
    return config;
}

void test_restricted_expected_master_structure() {
    const auto opt = make_path_instance(1);
    const auto structure =
        firebreak::benders::analyze_fpp_restricted_candidate_branch_benders_master_structure(opt);

    assert(structure.y_variable_count == 3);
    assert(structure.eta_variable_count == 1);
    assert(structure.risk_threshold_variable_count == 0);
    assert(structure.cvar_excess_variable_count == 0);
    assert(structure.total_variable_count == 4);
    assert(structure.budget_constraint_count == 1);
    assert(structure.base_constraint_count == 1);
    assert(structure.risk_constraint_count == 0);
    assert(!structure.has_scenario_recourse_variables);
}

void test_restricted_cvar_master_structure() {
    const auto opt = make_two_scenario_risk_instance();
    const auto structure =
        firebreak::benders::analyze_fpp_restricted_candidate_branch_benders_master_structure(
            opt,
            cvar_config());

    assert(structure.y_variable_count == 2);
    assert(structure.eta_variable_count == 2);
    assert(structure.risk_threshold_variable_count == 1);
    assert(structure.cvar_excess_variable_count == 2);
    assert(structure.total_variable_count == 7);
    assert(structure.risk_constraint_count == 2);
    assert(!structure.has_scenario_recourse_variables);
}

void test_restricted_mean_cvar_master_structure() {
    const auto opt = make_two_scenario_risk_instance();
    const auto structure =
        firebreak::benders::analyze_fpp_restricted_candidate_branch_benders_master_structure(
            opt,
            mean_cvar_config());

    assert(structure.y_variable_count == 2);
    assert(structure.eta_variable_count == 2);
    assert(structure.risk_threshold_variable_count == 1);
    assert(structure.cvar_excess_variable_count == 2);
    assert(structure.total_variable_count == 7);
    assert(structure.risk_constraint_count == 2);
}

void test_invalid_risk_config_fails_before_solve() {
    const auto opt = make_path_instance(1);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;

    auto beta_zero = restricted_options({2});
    beta_zero.risk_config.type = firebreak::risk::RiskMeasureType::CVaR;
    beta_zero.risk_config.cvarBeta = 0.0;
    assert_throws(
        [&] {
            (void)solver.solve(opt, beta_zero);
        },
        "invalid CVaR beta");

    auto lambda_large = restricted_options({2});
    lambda_large.risk_config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    lambda_large.risk_config.cvarLambda = 1.5;
    assert_throws(
        [&] {
            (void)solver.solve(opt, lambda_large);
        },
        "invalid CVaR lambda");
}

void test_invalid_initial_candidate_set() {
    const auto opt = make_path_instance(2);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;

    assert_throws(
        [&] {
            (void)solver.solve(opt, restricted_options({0}));
        },
        "initial active set smaller than budget");
    assert_throws(
        [&] {
            (void)solver.solve(opt, restricted_options({0, 0}));
        },
        "duplicate initial active candidate");
    assert_throws(
        [&] {
            (void)solver.solve(opt, restricted_options({0, 3}));
        },
        "out-of-range initial active candidate");
}

void test_maintenance_rejected_outside_heuristic_mode() {
    const auto opt = make_path_instance(1);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;

    auto options = restricted_options({0, 1});
    options.activation_policy = "benders-coefficients";
    options.activation_batch_size = 1;
    options.max_candidate_rounds = 1;
    options.candidate_maintenance_policy = "benders-coefficients";

    assert_throws(
        [&] {
            (void)solver.solve(opt, options);
        },
        "maintenance outside heuristic mode");
}

void test_maintenance_rejects_incompatible_activation_policy() {
    const auto opt = make_path_instance(1);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;

    auto options = restricted_options({0, 1}, false);
    options.restricted_heuristic_mode = true;
    options.activation_policy = "burn-frequency";
    options.activation_batch_size = 1;
    options.max_candidate_rounds = 1;
    options.candidate_maintenance_policy = "benders-coefficients";

    assert_throws(
        [&] {
            (void)solver.solve(opt, options);
        },
        "maintenance incompatible activation policy");
}

void test_tail_aware_score_mode_validation() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;

    auto exact_mode = restricted_options({1});
    exact_mode.risk_config = cvar_config();
    exact_mode.activation_policy = "benders-coefficients";
    exact_mode.activation_batch_size = 1;
    exact_mode.max_candidate_rounds = 1;
    exact_mode.candidate_maintenance_policy = "benders-coefficients";
    exact_mode.candidate_score_mode = "cvar-tail-blend";
    assert_throws(
        [&] {
            (void)solver.solve(opt, exact_mode);
        },
        "tail-aware mode is heuristic-only");

    auto expected_risk = restricted_options({1}, false);
    expected_risk.restricted_heuristic_mode = true;
    expected_risk.activation_policy = "benders-coefficients";
    expected_risk.activation_batch_size = 1;
    expected_risk.max_candidate_rounds = 1;
    expected_risk.candidate_maintenance_policy = "benders-coefficients";
    expected_risk.candidate_score_mode = "cvar-tail-blend";
    assert_throws(
        [&] {
            (void)solver.solve(opt, expected_risk);
        },
        "tail-aware mode requires CVaR");

    auto invalid_gamma = expected_risk;
    invalid_gamma.risk_config = cvar_config();
    invalid_gamma.candidate_tail_score_gamma = 1.5;
    assert_throws(
        [&] {
            (void)solver.solve(opt, invalid_gamma);
        },
        "tail-aware gamma range");
}

void test_strengthening_option_validation() {
    const auto opt = make_path_instance(1);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;

    auto invalid_rounds = restricted_options({2});
    invalid_rounds.root_user_cut_max_rounds = 0;
    assert_throws(
        [&] {
            (void)solver.solve(opt, invalid_rounds);
        },
        "invalid root cut max rounds");

    auto invalid_tolerance = restricted_options({2});
    invalid_tolerance.root_user_cut_tolerance = -1.0e-6;
    assert_throws(
        [&] {
            (void)solver.solve(opt, invalid_tolerance);
        },
        "invalid root cut tolerance");
}

void test_non_cplex_compile_behavior() {
#ifndef FIREBREAK_WITH_CPLEX
    const auto opt = make_path_instance(1);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    assert_throws(
        [&] {
            (void)solver.solve(opt, restricted_options({2}));
        },
        "non-CPLEX solve should report unavailable CPLEX support");
    std::cout << "Skipping restricted FPP Branch-Benders solve checks because CPLEX is not enabled.\n";
#endif
}

#ifdef FIREBREAK_WITH_CPLEX

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-6);
}

firebreak::solver::ModelResult solve_baseline(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config = firebreak::risk::RiskMeasureConfig()) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    return solver.solve(opt, options);
}

void enable_combinatorial_options(
    firebreak::benders::FppRestrictedCandidateBranchBendersOptions& options) {
    options.combinatorial_options.enabled = true;
    options.combinatorial_options.lift_mode =
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic;
    options.combinatorial_options.cut_sampling_ratio = 0.10;
    options.combinatorial_options.separate_fractional = true;
    options.combinatorial_options.initial_cuts = true;
}

void assert_restricted_combinatorial_diagnostics(
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

void test_restricted_stage_only_status() {
    const auto opt = make_path_instance(1);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, restricted_options({2}, false));

    assert(result.status == "RestrictedFeasible");
    assert(result.restricted_stage_status == "Optimal");
    assert(result.last_restricted_stage_result.status == "Optimal");
    assert(result.final_stage_status.empty());
    assert(!result.final_lower_bound_is_global);
    assert(!result.full_activation_performed);
    assert(!result.eventually_activated_all);
    assert(result.active_candidate_count_final == 1);
}

void test_exact_mode_tiny_solve_matches_baseline() {
    const auto opt = make_path_instance(1);
    const auto baseline = solve_baseline(opt);

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, restricted_options({2}));

    assert(baseline.status == "Optimal");
    assert(result.status == "Optimal");
    assert(result.restricted_stage_status == "Optimal");
    assert(result.final_stage_status == "Optimal");
    assert(result.eventually_activated_all);
    assert(result.full_activation_performed);
    assert(result.final_lower_bound_is_global);
    assert(result.global_optimality_certified);
    assert(!result.stopped_before_full_activation);
    assert(result.active_candidate_count_final == static_cast<int>(opt.eligible_indices.size()));
    assert_close(result.final_full_objective, baseline.objective_value);
    assert_close(result.final_stage_result.objective_value, baseline.objective_value);
    assert(result.final_stage_result.selected_firebreak_indices.size() <=
           static_cast<std::size_t>(opt.budget));
    assert(result.final_stage_result.branch_benders_max_cut_violation <= 1.0e-6);
    assert(result.final_stage_result.branch_benders_candidate_callback_calls >=
           result.final_stage_result.branch_benders_candidate_incumbents_checked);
    assert(result.final_stage_result.branch_benders_subproblems_attempted >=
           result.final_stage_result.branch_benders_subproblems_solved);
    assert(result.final_stage_result.branch_benders_subproblems_solved > 0);
    assert(result.final_stage_result.branch_benders_subproblem_time_sec >= 0.0);
    assert(result.final_stage_result.branch_benders_average_subproblem_time_sec >= 0.0);
    assert(result.final_stage_result.branch_benders_max_subproblem_time_sec >= 0.0);
    assert(result.final_stage_result.branch_benders_callback_time_sec >= 0.0);
    assert(result.final_stage_result.branch_benders_cut_construction_time_sec >= 0.0);
    assert(result.final_stage_result.branch_benders_lazy_cut_insertion_time_sec >= 0.0);
    assert(result.final_stage_result.branch_benders_violated_cuts >= 0);
    assert(result.final_stage_result.branch_benders_nonviolated_cuts >= 0);
    assert(result.final_stage_result.branch_benders_skipped_cuts >= 0);
    assert(result.final_stage_result.branch_benders_duplicate_cuts >= 0);
    assert(result.candidate_rounds == 2);
    assert(!result.activation_history.empty());
    assert((result.activation_history.back().activated == std::vector<int>{0, 1}));
    assert(result.global_time_budget_enabled);
    assert(!result.time_budget_exhausted);
    assert(result.global_time_limit_seconds == 30.0);
    assert(result.elapsed_time_total_seconds > 0.0);
    assert(result.restricted_initial_stage_runtime > 0.0);
    assert(result.restricted_final_full_stage_runtime > 0.0);
    assert(result.restricted_final_stage_time_limit > 0.0);
    assert(result.restricted_final_stage_time_limit <= result.global_time_limit_seconds);
    assert(result.round_log.size() == 2);
    assert(result.round_log.front().stage_type == "initial-restricted");
    assert(result.round_log.back().stage_type == "final-full");
    assert(result.round_log.back().time_limit_seconds <=
           result.round_log.front().remaining_global_time_after_stage + 1.0e-6);
    assert(result.round_log.back().remaining_global_time_after_stage <=
           result.round_log.back().remaining_global_time_before_stage + 1.0e-6);
    assert(!result.round_log.back().selected_firebreaks.empty());
    assert(result.persistent_subproblems_enabled);
    assert(result.subproblem_model_build_count == static_cast<int>(opt.scenarios.size()));
    assert(result.subproblem_model_rebuild_count == 0);
    assert(result.subproblem_fixed_y_update_count == result.subproblem_solve_count);
    assert(result.subproblem_solve_count > 0);
    assert(result.subproblem_total_build_time >= 0.0);
    assert(result.subproblem_total_update_time >= 0.0);
    assert(result.subproblem_total_solve_time >= 0.0);
    assert(!result.persistent_master_enabled);
    assert(result.master_model_build_count == result.candidate_rounds);
    assert(result.master_model_rebuild_count == result.master_model_build_count - 1);
    assert(result.master_bound_update_count == 0);
    assert(result.master_cut_insertions == result.cuts_reused_in_full_stage);
    assert(result.master_total_build_time >= 0.0);
    assert(result.master_total_cut_insertion_time >= 0.0);
}

void test_global_time_budget_exhaustion_before_stage_is_non_certifying() {
    const auto opt = make_path_instance(1);

    auto options = restricted_options({2});
    options.time_limit_seconds = 1.0e-12;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(result.status == "RestrictedTimeLimit");
    assert(result.global_time_budget_enabled);
    assert(result.time_budget_exhausted);
    assert(!result.global_optimality_certified);
    assert(!result.final_lower_bound_is_global);
    assert(!result.full_activation_performed);
    assert(!result.eventually_activated_all);
    assert(result.candidate_rounds == 0);
    assert(result.round_log.empty());
}

void test_cut_reuse_across_restricted_and_full_stages() {
    const auto opt = make_path_instance(1);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, restricted_options({2}));

    assert(result.full_space_cuts_generated);
    assert(result.cuts_reused_across_stages);
    assert(!result.cut_reuse_postponed);
    assert(result.cut_reuse_enabled);
    assert(result.restricted_stage_lazy_cuts > 0);
    assert(result.cut_pool_size > 0);
    assert(result.cuts_reused_in_full_stage > 0);
    assert(result.final_stage_result.benders_cuts_added == result.cuts_reused_in_full_stage);
    assert(result.master_cut_insertions == result.cuts_reused_in_full_stage);
    assert(!result.cuts_by_round.empty());
    assert(!result.cuts_by_scenario.empty());
    assert(result.final_stage_lazy_cuts >= 0);
}

void test_zero_budget() {
    const auto opt = make_path_instance(0);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, restricted_options({}));

    assert(result.status == "Optimal");
    assert(result.eventually_activated_all);
    assert(result.full_activation_performed);
    assert(result.final_lower_bound_is_global);
    assert(result.final_stage_result.selected_firebreak_indices.empty());
    assert_close(result.final_full_objective, 3.0);
    assert(result.final_stage_result.branch_benders_max_cut_violation <= 1.0e-6);
}

void test_burn_frequency_initialization_and_activation_matches_baseline() {
    const auto opt = make_path_instance(1);
    const auto baseline = solve_baseline(opt);

    firebreak::benders::FppRestrictedCandidateBranchBendersOptions options =
        restricted_options({});
    options.initial_candidate_policy = "burn-frequency";
    options.initial_candidate_size = 1;
    options.activation_policy = "burn-frequency";
    options.activation_batch_size = 1;
    options.max_candidate_rounds = 1;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(result.status == "Optimal");
    assert(result.initial_candidate_policy == "burn-frequency");
    assert(result.activation_policy == "burn-frequency");
    assert(result.burn_frequency_score_available);
    assert((result.initial_candidates_from_burn_frequency == std::vector<int>{0}));
    assert((result.candidates_activated_by_burn_frequency == std::vector<int>{1}));
    assert(result.eventually_activated_all);
    assert(result.final_lower_bound_is_global);
    assert(result.candidate_rounds == 3);
    assert_close(result.final_full_objective, baseline.objective_value);
    assert(result.final_stage_result.branch_benders_max_cut_violation <= 1.0e-6);
    assert(result.round_log[1].cut_pool_size_before_stage > 0);
    assert(result.round_log[1].cuts_reused_at_stage > 0);
    assert(result.round_log[2].cut_pool_size_before_stage >=
           result.round_log[1].cut_pool_size_before_stage);
    assert(result.round_log[2].cuts_reused_at_stage > 0);
    assert(result.master_model_build_count == result.candidate_rounds);
    assert(result.master_cut_insertions ==
           result.restricted_stage_cuts_reused + result.cuts_reused_in_full_stage);
}

void test_benders_coefficient_activation_matches_baseline() {
    const auto opt = make_path_instance(1);
    const auto baseline = solve_baseline(opt);

    auto options = restricted_options({2});
    options.activation_policy = "benders-coefficients";
    options.activation_batch_size = 1;
    options.max_candidate_rounds = 1;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(result.status == "Optimal");
    assert(result.activation_policy == "benders-coefficients");
    assert(result.benders_coefficient_scores_available);
    assert(!result.candidates_activated_by_benders_coefficients.empty());
    assert(result.number_of_cuts_used_for_activation >= 0);
    assert(result.number_of_nonzero_inactive_coefficients >= 0);
    assert(!result.top_benders_coefficient_candidates.empty());
    assert(result.eventually_activated_all);
    assert(result.final_lower_bound_is_global);
    assert(result.candidate_rounds == 3);
    assert_close(result.final_full_objective, baseline.objective_value);
    assert(result.final_stage_result.branch_benders_max_cut_violation <= 1.0e-6);
    assert(result.round_log[1].cut_pool_size_before_stage > 0);
    assert(result.round_log[1].cuts_reused_at_stage > 0);
    assert(result.round_log[2].cuts_reused_at_stage > 0);
    assert(result.master_model_build_count == result.candidate_rounds);
}

void test_cvar_exact_mode_tiny_solve_matches_baseline() {
    const auto opt = make_two_scenario_risk_instance();
    const auto config = cvar_config();
    const auto baseline = solve_baseline(opt, config);

    auto options = restricted_options({1});
    options.risk_config = config;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(baseline.status == "Optimal");
    assert(result.status == "Optimal");
    assert(result.risk_measure == "cvar");
    assert(result.final_stage_result.risk_measure == "cvar");
    assert(result.eventually_activated_all);
    assert(result.final_lower_bound_is_global);
    assert_close(result.final_full_objective, baseline.objective_value);
    assert_close(result.final_stage_result.objective_value, baseline.objective_value);
    assert_close(result.final_stage_result.expected_loss_component, baseline.expected_loss_component);
    assert_close(result.final_stage_result.cvar_loss_component, baseline.cvar_loss_component);
    assert(result.final_stage_result.branch_benders_max_cut_violation <= 1.0e-6);
    assert(result.final_stage_result.num_variables == 7);
}

void test_mean_cvar_exact_mode_tiny_solve_matches_baseline() {
    const auto opt = make_two_scenario_risk_instance();
    const auto config = mean_cvar_config();
    const auto baseline = solve_baseline(opt, config);

    auto options = restricted_options({1});
    options.risk_config = config;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(baseline.status == "Optimal");
    assert(result.status == "Optimal");
    assert(result.risk_measure == "mean-cvar");
    assert(result.final_stage_result.risk_measure == "mean-cvar");
    assert(result.eventually_activated_all);
    assert(result.final_lower_bound_is_global);
    assert_close(result.final_full_objective, baseline.objective_value);
    assert_close(result.final_stage_result.objective_value, baseline.objective_value);
    assert_close(result.final_stage_result.expected_loss_component, baseline.expected_loss_component);
    assert_close(result.final_stage_result.cvar_loss_component, baseline.cvar_loss_component);
    assert(result.final_stage_result.branch_benders_max_cut_violation <= 1.0e-6);
    assert(result.final_stage_result.num_variables == 7);
}

void test_combinatorial_exact_mode_expected_cvar_and_mean_cvar() {
    {
        const auto opt = make_path_instance(1);
        const auto baseline = solve_baseline(opt);
        auto options = restricted_options({2});
        enable_combinatorial_options(options);

        firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
        const auto result = solver.solve(opt, options);

        assert(baseline.status == "Optimal");
        assert(result.status == "Optimal");
        assert(result.final_lower_bound_is_global);
        assert_close(result.final_full_objective, baseline.objective_value);
        assert_close(result.final_stage_result.objective_value, baseline.objective_value);
        assert_restricted_combinatorial_diagnostics(
            result.final_stage_result,
            opt.scenarios.size());
    }

    {
        const auto opt = make_two_scenario_risk_instance();
        const auto config = cvar_config();
        const auto baseline = solve_baseline(opt, config);
        auto options = restricted_options({1});
        options.risk_config = config;
        enable_combinatorial_options(options);

        firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
        const auto result = solver.solve(opt, options);

        assert(baseline.status == "Optimal");
        assert(result.status == "Optimal");
        assert(result.final_lower_bound_is_global);
        assert(result.final_stage_result.risk_measure == "cvar");
        assert_close(result.final_full_objective, baseline.objective_value);
        assert_close(result.final_stage_result.expected_loss_component, 1.5);
        assert_close(result.final_stage_result.cvar_loss_component, 2.0);
        assert_restricted_combinatorial_diagnostics(
            result.final_stage_result,
            opt.scenarios.size());
    }

    {
        const auto opt = make_two_scenario_risk_instance();
        const auto config = mean_cvar_config();
        const auto baseline = solve_baseline(opt, config);
        auto options = restricted_options({1});
        options.risk_config = config;
        enable_combinatorial_options(options);

        firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
        const auto result = solver.solve(opt, options);

        assert(baseline.status == "Optimal");
        assert(result.status == "Optimal");
        assert(result.final_lower_bound_is_global);
        assert(result.final_stage_result.risk_measure == "mean-cvar");
        assert_close(result.final_full_objective, baseline.objective_value);
        assert_close(result.final_stage_result.objective_value, 1.75);
        assert_close(result.final_stage_result.expected_loss_component, 1.5);
        assert_close(result.final_stage_result.cvar_loss_component, 2.0);
        assert_restricted_combinatorial_diagnostics(
            result.final_stage_result,
            opt.scenarios.size());
    }
}

void test_strengthening_options_expected_cvar_and_mean_cvar() {
    struct Case {
        firebreak::opt::OptimizationInstance opt;
        firebreak::risk::RiskMeasureConfig risk_config;
        std::vector<int> initial_candidates;
    };

    std::vector<Case> cases;
    cases.push_back({make_path_instance(1), firebreak::risk::RiskMeasureConfig(), {2}});
    cases.push_back({make_two_scenario_risk_instance(), cvar_config(), {1}});
    cases.push_back({make_two_scenario_risk_instance(), mean_cvar_config(), {1}});

    for (const auto& item : cases) {
        const auto baseline = solve_baseline(item.opt, item.risk_config);

        auto options = restricted_options(item.initial_candidates);
        options.risk_config = item.risk_config;
        options.use_lifted_lower_bounds = true;
        options.use_root_user_cuts = true;
        options.root_user_cut_max_rounds = 1;
        options.root_user_cut_tolerance = 1.0e-7;
        options.strengthening_options.use_coverage_llbi = true;
        options.strengthening_options.use_path_llbi = true;
        options.strengthening_options.use_conditional_zero_benefit_fixing = true;

        firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
        const auto result = solver.solve(item.opt, options);

        assert(baseline.status == "Optimal");
        assert(result.status == "Optimal");
        assert(result.final_lower_bound_is_global);
        assert_close(result.final_full_objective, baseline.objective_value);
        assert(result.final_stage_result.benders_use_lifted_lower_bounds);
        assert(result.final_stage_result.benders_lifted_lower_bound_count ==
               static_cast<int>(item.opt.scenarios.size()));
        assert(result.final_stage_result.branch_benders_use_root_user_cuts);
        assert(result.final_stage_result.branch_benders_root_user_cut_max_rounds == 1);
        assert_close(
            result.final_stage_result.branch_benders_root_user_cut_tolerance,
            1.0e-7);
        assert(result.final_stage_result.branch_benders_root_user_cut_only_at_root_confirmed);
        assert(result.final_stage_result.branch_benders_root_user_cut_callback_calls >=
               result.final_stage_result.branch_benders_root_user_cut_rounds_executed);
        assert(result.final_stage_result.branch_benders_max_cut_violation <= 1.0e-6);
        assert(result.final_stage_result.coverage_llbi_enabled);
        assert(result.final_stage_result.coverage_llbi_num_zeta_vars > 0);
        assert(result.final_stage_result.coverage_llbi_num_constraints > 0);
        assert(result.final_stage_result.coverage_llbi_precompute_time_sec >= 0.0);
        assert(result.final_stage_result.path_llbi_enabled);
        assert(result.final_stage_result.path_llbi_num_b_vars > 0);
        assert(result.final_stage_result.path_llbi_num_path_constraints > 0);
        assert(result.final_stage_result.path_llbi_num_paths_used > 0);
        assert(result.final_stage_result.path_llbi_precompute_time_sec >= 0.0);
        assert(result.final_stage_result.conditional_zero_benefit_enabled);
        assert(result.final_stage_result.conditional_zero_benefit_fixings_attempted == 0);
        assert(result.final_stage_result.conditional_zero_benefit_fixings_applied == 0);
        assert(result.final_stage_result.conditional_zero_benefit_time_sec >= 0.0);
    }
}

void test_cvar_heuristic_mode_status_semantics() {
    const auto opt = make_two_scenario_risk_instance();

    auto options = restricted_options({1}, false);
    options.risk_config = cvar_config();
    options.restricted_heuristic_mode = true;
    options.stop_after_candidate_rounds = 0;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(result.status == "RestrictedHeuristic");
    assert(result.risk_measure == "cvar");
    assert(result.restricted_stage_result.risk_measure == "cvar");
    assert(result.last_restricted_stage_result.risk_measure == "cvar");
    assert(result.heuristic_mode_enabled);
    assert(result.stopped_before_full_activation);
    assert(!result.global_optimality_certified);
    assert(!result.eventually_activated_all);
    assert(!result.final_lower_bound_is_global);
    assert(!result.restricted_bound_is_global);
    assert(result.restricted_lower_bound_is_global == false);
    assert(result.active_candidate_fraction_at_stop < 1.0);
    assert(result.final_stage_status.empty());
    assert_close(result.restricted_objective, result.last_restricted_stage_result.objective_value);
}

void test_heuristic_mode_status_semantics() {
    const auto opt = make_path_instance(1);

    auto options = restricted_options({2}, false);
    options.restricted_heuristic_mode = true;
    options.stop_after_candidate_rounds = 0;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(result.status == "RestrictedHeuristic");
    assert(result.heuristic_mode_enabled);
    assert(result.stopped_before_full_activation);
    assert(!result.global_optimality_certified);
    assert(!result.eventually_activated_all);
    assert(!result.final_lower_bound_is_global);
    assert(!result.restricted_bound_is_global);
    assert(result.restricted_lower_bound_is_global == false);
    assert(result.active_candidate_fraction_at_stop < 1.0);
    assert(result.final_stage_status.empty());
    assert_close(result.restricted_objective, result.last_restricted_stage_result.objective_value);
    assert(result.restricted_stage_result.selected_firebreak_indices.size() <=
           static_cast<std::size_t>(opt.budget));
    assert(result.reason_for_heuristic_stop == "stop-after-candidate-rounds");
}

void test_heuristic_activation_reuses_previous_stage_cuts() {
    const auto opt = make_path_instance(1);

    auto options = restricted_options({2}, false);
    options.restricted_heuristic_mode = true;
    options.activation_policy = "benders-coefficients";
    options.activation_batch_size = 1;
    options.max_candidate_rounds = 1;
    options.stop_after_candidate_rounds = 1;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(result.status == "RestrictedHeuristic");
    assert(result.heuristic_mode_enabled);
    assert(result.stopped_before_full_activation);
    assert(!result.global_optimality_certified);
    assert(result.round_log.size() == 2);
    assert(result.round_log[1].stage_type == "activation");
    assert(result.round_log[1].cut_pool_size_before_stage > 0);
    assert(result.round_log[1].cuts_reused_at_stage > 0);
    assert(result.restricted_stage_cuts_reused > 0);
    assert(result.master_model_build_count == result.candidate_rounds);
    assert(result.subproblem_model_build_count == static_cast<int>(opt.scenarios.size()));
    assert(result.subproblem_model_rebuild_count == 0);
}

void test_benders_coefficient_maintenance_heuristic_deactivates_and_preserves_cuts() {
    const auto opt = make_path_instance(1);

    auto options = restricted_options({0, 1}, false);
    options.restricted_heuristic_mode = true;
    options.activation_policy = "benders-coefficients";
    options.activation_batch_size = 1;
    options.max_candidate_rounds = 1;
    options.stop_after_candidate_rounds = 1;
    options.candidate_maintenance_policy = "benders-coefficients";
    options.candidate_min_active_size = 2;
    options.candidate_max_active_size = 2;
    options.candidate_deactivation_batch_size = 1;
    options.candidate_deactivation_min_age = 1;
    options.candidate_reactivation_cooldown_rounds = 1;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(result.status == "RestrictedHeuristic");
    assert(result.deactivation_enabled);
    assert(result.candidate_maintenance_policy == "benders-coefficients");
    assert(result.deactivation_rounds == 1);
    assert(result.active_candidate_count_final <= result.candidate_max_active_size);
    assert(result.active_candidate_count_final >= result.candidate_min_active_size);
    assert(!result.deactivated_candidates_by_round.empty());
    assert(!result.deactivated_candidates_by_round[0].empty());
    assert(result.round_log.size() == 2);
    assert(result.round_log[1].stage_type == "maintenance");
    assert(!result.round_log[1].deactivated_candidates.empty());
    assert(result.round_log[1].active_count_after_activation == 3);
    assert(result.round_log[1].active_count_after_deactivation == 2);
    assert(result.round_log[1].protected_selected_count >= 0);
    assert(result.cut_pool_size > 0);
    assert(result.round_log[1].cut_pool_size_before_stage > 0);
    assert(result.round_log[1].cuts_reused_at_stage > 0);
    assert(!result.global_optimality_certified);
    assert(result.stopped_before_full_activation);
}

void test_cvar_tail_aware_maintenance_heuristic_runs() {
    const auto opt = make_two_scenario_risk_instance();

    auto options = restricted_options({1}, false);
    options.risk_config = cvar_config();
    options.restricted_heuristic_mode = true;
    options.activation_policy = "benders-coefficients";
    options.activation_batch_size = 1;
    options.max_candidate_rounds = 1;
    options.stop_after_candidate_rounds = 1;
    options.candidate_maintenance_policy = "benders-coefficients";
    options.candidate_min_active_size = 1;
    options.candidate_max_active_size = 1;
    options.candidate_deactivation_batch_size = 1;
    options.candidate_deactivation_min_age = 1;
    options.candidate_reactivation_cooldown_rounds = 1;
    options.candidate_score_mode = "cvar-tail-blend";
    options.candidate_tail_score_gamma = 0.5;
    options.candidate_tail_protection_size = 1;

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result = solver.solve(opt, options);

    assert(result.status == "RestrictedHeuristic");
    assert(result.risk_measure == "cvar");
    assert(result.heuristic_mode_enabled);
    assert(!result.global_optimality_certified);
    assert(result.candidate_score_mode == "cvar-tail-blend");
    assert(result.candidate_tail_score_gamma == 0.5);
    assert(result.candidate_tail_protection_size == 1);
    assert(result.tail_score_diagnostics_enabled);
    assert(result.activated_by_tail_blend_count >= 0);
    assert(result.deactivation_blocked_by_tail_protection_count >= 0);
    assert(!result.round_log.empty());
    assert(result.round_log.back().candidate_score_mode == "cvar-tail-blend");
    assert(!result.tail_score_diagnostics.empty());
    assert(!result.tail_score_diagnostics.back().top_tail_blend_candidates.empty());
}

#endif

}  // namespace

int main() {
    test_restricted_expected_master_structure();
    test_restricted_cvar_master_structure();
    test_restricted_mean_cvar_master_structure();
    test_invalid_risk_config_fails_before_solve();
    test_invalid_initial_candidate_set();
    test_maintenance_rejected_outside_heuristic_mode();
    test_maintenance_rejects_incompatible_activation_policy();
    test_tail_aware_score_mode_validation();
    test_strengthening_option_validation();
    test_non_cplex_compile_behavior();
#ifdef FIREBREAK_WITH_CPLEX
    test_restricted_stage_only_status();
    test_exact_mode_tiny_solve_matches_baseline();
    test_global_time_budget_exhaustion_before_stage_is_non_certifying();
    test_cut_reuse_across_restricted_and_full_stages();
    test_zero_budget();
    test_burn_frequency_initialization_and_activation_matches_baseline();
    test_benders_coefficient_activation_matches_baseline();
    test_cvar_exact_mode_tiny_solve_matches_baseline();
    test_mean_cvar_exact_mode_tiny_solve_matches_baseline();
    test_combinatorial_exact_mode_expected_cvar_and_mean_cvar();
    test_strengthening_options_expected_cvar_and_mean_cvar();
    test_cvar_heuristic_mode_status_semantics();
    test_heuristic_mode_status_semantics();
    test_heuristic_activation_reuses_previous_stage_cuts();
    test_benders_coefficient_maintenance_heuristic_deactivates_and_preserves_cuts();
    test_cvar_tail_aware_maintenance_heuristic_runs();
#endif
    std::cout << "All FPP restricted candidate Branch-Benders tests passed.\n";
    return 0;
}
