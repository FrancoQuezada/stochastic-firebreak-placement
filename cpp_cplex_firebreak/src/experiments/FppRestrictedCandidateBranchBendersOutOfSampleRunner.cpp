#include "experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "analysis/GraphDiagnostics.hpp"
#include "benders/FppRestrictedCandidateBranchBendersSolver.hpp"
#include "core/FirebreakSolution.hpp"
#include "eval/BurnedAreaEvaluator.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/ExperimentResultWriter.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "io/ScenarioSplitUtils.hpp"
#include "io/SolutionIO.hpp"
#include "opt/OptimizationInstance.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/CplexEnvironment.hpp"
#include "solver/FppWeightedLossUtils.hpp"

namespace firebreak::experiments {

namespace {

std::string format_compact_double(double value) {
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

risk::RiskMeasureConfig effective_risk_config_from(const risk::RiskMeasureConfig& config) {
    risk::RiskMeasureConfig effective = config;
    if (effective.type == risk::RiskMeasureType::CVaR) {
        effective.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective);
    return effective;
}

std::string method_label_for_options(
    const risk::RiskMeasureConfig& config,
    bool use_lifted_lower_bounds,
    bool use_root_user_cuts,
    const benders::FppCombinatorialBendersOptions& combinatorial_options,
    const benders::FppStrengtheningOptions& strengthening_options) {
    std::string label = "FPP-Restricted-Branch-Benders";
    if (combinatorial_options.enabled) {
        label += "-Combinatorial";
    }
    if (config.type == risk::RiskMeasureType::CVaR) {
        label += "-CVaR";
    } else if (config.type == risk::RiskMeasureType::MeanCVaR) {
        label += "-MeanCVaR";
    }
    if (combinatorial_options.enabled &&
        combinatorial_options.scenario_order ==
            benders::FppCombinatorialBendersScenarioOrder::EtaDescending) {
        label += "-EtaDesc";
    }
    if (use_lifted_lower_bounds) {
        label += "-LLBI";
    }
    if (use_root_user_cuts) {
        label += "-RootCuts";
    }
    if (strengthening_options.use_global_dominance_preprocessing) {
        label += "-DominancePreprocess";
    }
    if (strengthening_options.use_coverage_llbi) {
        label += "-CoverageLLBI";
    }
    if (strengthening_options.use_path_llbi) {
        label += "-PathLLBI";
    }
    if (strengthening_options.use_conditional_zero_benefit_fixing) {
        label += "-ConditionalZeroFixing";
    }
    return label;
}

std::string objective_metric_for_risk(const risk::RiskMeasureConfig& config) {
    const std::string base = firebreak::solver::weighted_objective_metric_label(config);
    if (config.type == risk::RiskMeasureType::Expected) {
        return base;
    }
    if (config.type == risk::RiskMeasureType::CVaR) {
        return base + "_beta_" + format_compact_double(config.cvarBeta);
    }
    return base + "_beta_" +
        format_compact_double(config.cvarBeta) +
        "_lambda_" +
        format_compact_double(config.cvarLambda);
}

bool has_nonunit_compact_weights(const opt::OptimizationInstance& opt) {
    if (opt.compact_cell_weights.empty()) {
        return false;
    }
    const auto& weights = firebreak::solver::direct_fpp_compact_weights(opt);
    for (const double weight : weights) {
        if (std::fabs(weight - 1.0) > 1.0e-9) {
            return true;
        }
    }
    return false;
}

bool uses_unconverted_weighted_restricted_module(
    bool use_lifted_lower_bounds,
    const benders::FppCombinatorialBendersOptions& combinatorial_options,
    const benders::FppStrengtheningOptions& strengthening_options) {
    return use_lifted_lower_bounds ||
           combinatorial_options.enabled ||
           strengthening_options.use_coverage_llbi ||
           strengthening_options.use_path_llbi ||
           strengthening_options.use_projected_coverage_llbi_exp ||
           strengthening_options.use_projected_path_llbi_exp ||
           strengthening_options.use_projected_coverage_llbi_poly ||
           strengthening_options.use_projected_path_llbi_poly ||
           strengthening_options.use_global_dominance_preprocessing ||
           strengthening_options.use_conditional_zero_benefit_fixing;
}

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

std::filesystem::path default_experiment_json_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + ".json");
}

std::filesystem::path default_experiment_csv_path() {
    return firebreak::io::resolve_output_path(
        "results/experiments/fpp_restricted_branch_benders_oos_results.csv");
}

std::filesystem::path default_solution_json_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + "_solution.json");
}

std::filesystem::path default_solution_csv_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + "_solution.csv");
}

void validate_disjoint(const std::vector<int>& train_ids, const std::vector<int>& test_ids) {
    std::unordered_set<int> train_set(train_ids.begin(), train_ids.end());
    for (const int id : test_ids) {
        if (train_set.find(id) != train_set.end()) {
            throw std::runtime_error("Train and test scenario IDs must be disjoint.");
        }
    }
}

std::vector<int> candidate_ids_from_cli_list(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& ids) {
    std::vector<int> candidates;
    candidates.reserve(ids.size());
    for (const int id : ids) {
        if (id >= 0 && id < static_cast<int>(opt.eligible_indices.size())) {
            candidates.push_back(id);
            continue;
        }
        const auto it = std::find(
            opt.eligible_original_nodes.begin(),
            opt.eligible_original_nodes.end(),
            id);
        if (it == opt.eligible_original_nodes.end()) {
            throw std::runtime_error(
                "Initial candidate list id " + std::to_string(id) +
                " is neither an eligible candidate position nor an eligible original node.");
        }
        candidates.push_back(static_cast<int>(std::distance(opt.eligible_original_nodes.begin(), it)));
    }
    return candidates;
}

int default_initial_candidate_size(const opt::OptimizationInstance& opt) {
    const int candidate_count = static_cast<int>(opt.eligible_indices.size());
    const int requested = std::max(5 * opt.budget, 50);
    return std::max(opt.budget, std::min(candidate_count, requested));
}

double total_stage_runtime(
    const benders::FppRestrictedCandidateBranchBendersResult& result) {
    double runtime = 0.0;
    for (const auto& round : result.round_log) {
        runtime += round.runtime_seconds;
    }
    return runtime;
}

io::RestrictedCandidateRoundResult to_io_round(
    const benders::FppRestrictedCandidateRoundLog& round) {
    io::RestrictedCandidateRoundResult out;
    out.round_index = round.round_index;
    out.stage_type = round.stage_type;
    out.risk_measure = round.risk_measure;
    out.active_candidate_count = round.active_candidate_count;
    out.active_candidate_fraction = round.active_candidate_fraction;
    out.activation_policy = round.activation_policy;
    out.newly_activated_candidates = round.newly_activated_candidates;
    out.solve_status = round.solve_status;
    out.objective = round.objective;
    out.best_bound = round.best_bound;
    out.mip_gap = round.mip_gap;
    out.runtime_seconds = round.runtime_seconds;
    out.time_limit_seconds = round.time_limit_seconds;
    out.remaining_global_time_before_stage = round.remaining_global_time_before_stage;
    out.remaining_global_time_after_stage = round.remaining_global_time_after_stage;
    out.cuts_added = round.cuts_added;
    out.candidate_incumbents_checked = round.candidate_incumbent_checks;
    out.subproblem_solves = round.subproblem_solves;
    out.callback_time_seconds = round.callback_time_seconds;
    out.subproblem_time_seconds = round.subproblem_time_seconds;
    out.cut_pool_size_before_stage = round.cut_pool_size_before_stage;
    out.cuts_reused_at_stage = round.cuts_reused_at_stage;
    out.new_cuts_added_to_pool = round.new_cuts_added_to_pool;
    out.duplicate_cuts_skipped = round.duplicate_cuts_skipped;
    out.max_cut_violation = round.max_cut_violation;
    out.selected_firebreaks = round.selected_firebreaks;
    out.active_count_before_maintenance = round.active_count_before_maintenance;
    out.active_count_after_activation = round.active_count_after_activation;
    out.active_count_after_deactivation = round.active_count_after_deactivation;
    out.deactivated_candidates = round.deactivated_candidates;
    out.protected_selected_count = round.protected_selected_count;
    out.protected_min_age_count = round.protected_min_age_count;
    out.protected_cooldown_count = round.protected_cooldown_count;
    out.protected_newly_activated_count = round.protected_newly_activated_count;
    out.protected_tail_count = round.protected_tail_count;
    out.deactivation_candidate_count = round.deactivation_candidate_count;
    out.reactivation_blocked_by_cooldown_count =
        round.reactivation_blocked_by_cooldown_count;
    out.oscillation_event_count = round.oscillation_event_count;
    out.selected_candidates_protected = round.selected_candidates_protected;
    out.tail_protected_candidates = round.tail_protected_candidates;
    out.candidate_score_mode = round.candidate_score_mode;
    out.candidate_tail_score_gamma = round.candidate_tail_score_gamma;
    out.candidate_tail_protection_size = round.candidate_tail_protection_size;
    out.top_blend_candidates = round.top_blend_candidates;
    out.top_generic_candidates_for_score_mode =
        round.top_generic_candidates_for_score_mode;
    out.top_tail_candidates = round.top_tail_candidates;
    out.top_blend_tail_overlap = round.top_blend_tail_overlap;
    out.top_blend_generic_overlap = round.top_blend_generic_overlap;
    out.activated_tail_top_k_overlap = round.activated_tail_top_k_overlap;
    out.deactivated_tail_top_k_warning_count =
        round.deactivated_tail_top_k_warning_count;
    return out;
}

std::vector<std::pair<std::string, std::string>> strengthening_summary_fields(
    const io::StandardExperimentResult& result) {
    return {
        {"coverage_llbi_enabled", result.coverage_llbi_enabled ? "true" : "false"},
        {"coverage_llbi_num_zeta_vars", std::to_string(result.coverage_llbi_num_zeta_vars)},
        {"coverage_llbi_num_constraints", std::to_string(result.coverage_llbi_num_constraints)},
        {"coverage_llbi_precompute_time_sec", format_compact_double(result.coverage_llbi_precompute_time_sec)},
        {"path_llbi_enabled", result.path_llbi_enabled ? "true" : "false"},
        {"path_llbi_num_b_vars", std::to_string(result.path_llbi_num_b_vars)},
        {"path_llbi_num_path_constraints", std::to_string(result.path_llbi_num_path_constraints)},
        {"path_llbi_num_paths_used", std::to_string(result.path_llbi_num_paths_used)},
        {"path_llbi_precompute_time_sec", format_compact_double(result.path_llbi_precompute_time_sec)},
        {"global_dominance_enabled", result.global_dominance_enabled ? "true" : "false"},
        {"global_dominance_candidates_removed", std::to_string(result.global_dominance_candidates_removed)},
        {"global_dominance_equivalence_classes", std::to_string(result.global_dominance_equivalence_classes)},
        {"global_dominance_precompute_time_sec", format_compact_double(result.global_dominance_precompute_time_sec)},
        {"conditional_zero_benefit_enabled", result.conditional_zero_benefit_enabled ? "true" : "false"},
        {"conditional_zero_benefit_fixings_attempted", std::to_string(result.conditional_zero_benefit_fixings_attempted)},
        {"conditional_zero_benefit_fixings_applied", std::to_string(result.conditional_zero_benefit_fixings_applied)},
        {"conditional_zero_benefit_time_sec", format_compact_double(result.conditional_zero_benefit_time_sec)},
    };
}

void export_strengthening_summaries(
    const benders::FppStrengtheningOptions& options,
    const io::StandardExperimentResult& result) {
    const auto fields = strengthening_summary_fields(result);
    benders::export_fpp_strengthening_summary(options.coverage_llbi_export_path, fields);
    benders::export_fpp_strengthening_summary(options.path_llbi_export_path, fields);
    benders::export_fpp_strengthening_summary(options.dominance_preprocessing_export_path, fields);
    benders::export_fpp_strengthening_summary(options.conditional_fixing_log_export_path, fields);
}

void attach_restricted_diagnostics(
    const benders::FppRestrictedCandidateBranchBendersResult& solve_result,
    io::StandardExperimentResult& result) {
    result.restricted_candidate_enabled = solve_result.restricted_candidate_enabled;
    result.restricted_candidate_exact_mode = solve_result.restricted_candidate_exact_mode;
    result.restricted_candidate_initial_policy = solve_result.initial_candidate_policy;
    result.restricted_candidate_activation_policy = solve_result.activation_policy;
    result.restricted_candidate_initial_active_count =
        solve_result.initial_active_candidate_count;
    result.restricted_candidate_final_active_count =
        solve_result.active_candidate_count_final;
    result.restricted_candidate_final_active_fraction =
        solve_result.active_candidate_fraction_final;
    result.restricted_candidate_eventually_activated_all =
        solve_result.eventually_activated_all;
    result.restricted_candidate_full_activation_performed =
        solve_result.full_activation_performed;
    result.restricted_candidate_restricted_lower_bound_is_global =
        solve_result.restricted_lower_bound_is_global;
    result.restricted_candidate_final_lower_bound_is_global =
        solve_result.final_lower_bound_is_global;
    result.restricted_candidate_cut_reuse_enabled = solve_result.cut_reuse_enabled;
    result.restricted_candidate_cut_pool_size = solve_result.cut_pool_size;
    result.restricted_candidate_rounds = solve_result.candidate_rounds;
    result.restricted_candidate_cuts_reused_in_full_stage =
        solve_result.cuts_reused_in_full_stage;
    result.restricted_candidate_duplicate_cuts_skipped =
        solve_result.duplicate_cuts_skipped;
    result.restricted_candidate_heuristic_mode_enabled =
        solve_result.heuristic_mode_enabled;
    result.restricted_candidate_stopped_before_full_activation =
        solve_result.stopped_before_full_activation;
    result.restricted_candidate_global_optimality_certified =
        solve_result.global_optimality_certified;
    result.restricted_candidate_global_time_budget_enabled =
        solve_result.global_time_budget_enabled;
    result.restricted_candidate_time_budget_exhausted =
        solve_result.time_budget_exhausted;
    result.restricted_candidate_global_time_limit_seconds =
        solve_result.global_time_limit_seconds;
    result.restricted_candidate_elapsed_time_total_seconds =
        solve_result.elapsed_time_total_seconds;
    result.restricted_candidate_initial_stage_runtime =
        solve_result.restricted_initial_stage_runtime;
    result.restricted_candidate_activation_stage_runtime_total =
        solve_result.restricted_activation_stage_runtime_total;
    result.restricted_candidate_final_full_stage_runtime =
        solve_result.restricted_final_full_stage_runtime;
    result.restricted_candidate_final_stage_time_limit =
        solve_result.restricted_final_stage_time_limit;
    result.restricted_candidate_reason_for_heuristic_stop =
        solve_result.reason_for_heuristic_stop;
    result.restricted_candidate_restricted_objective =
        solve_result.restricted_objective;
    result.restricted_candidate_restricted_best_bound =
        solve_result.restricted_best_bound;
    result.restricted_candidate_restricted_bound_is_global =
        solve_result.restricted_bound_is_global;
    result.restricted_candidate_active_fraction_at_stop =
        solve_result.active_candidate_fraction_at_stop;
    result.restricted_candidate_maintenance_policy =
        solve_result.candidate_maintenance_policy;
    result.restricted_candidate_deactivation_enabled =
        solve_result.deactivation_enabled;
    result.restricted_candidate_deactivation_rounds =
        solve_result.deactivation_rounds;
    result.restricted_candidate_min_active_size =
        solve_result.candidate_min_active_size;
    result.restricted_candidate_max_active_size =
        solve_result.candidate_max_active_size;
    result.restricted_candidate_deactivation_batch_size =
        solve_result.candidate_deactivation_batch_size;
    result.restricted_candidate_deactivation_min_age =
        solve_result.candidate_deactivation_min_age;
    result.restricted_candidate_reactivation_cooldown_rounds =
        solve_result.candidate_reactivation_cooldown_rounds;
    result.restricted_candidate_protect_selected_candidates =
        solve_result.protect_selected_candidates;
    result.restricted_candidate_protected_selected_count =
        solve_result.protected_selected_count;
    result.restricted_candidate_protected_min_age_count =
        solve_result.protected_min_age_count;
    result.restricted_candidate_protected_cooldown_count =
        solve_result.protected_cooldown_count;
    result.restricted_candidate_protected_newly_activated_count =
        solve_result.protected_newly_activated_count;
    result.restricted_candidate_protected_tail_count =
        solve_result.protected_tail_count;
    result.restricted_candidate_score_mode = solve_result.candidate_score_mode;
    result.restricted_candidate_tail_score_gamma =
        solve_result.candidate_tail_score_gamma;
    result.restricted_candidate_tail_protection_size =
        solve_result.candidate_tail_protection_size;
    result.restricted_candidate_scorer = solve_result.candidate_scorer;
    result.restricted_candidate_scorer_weighted =
        solve_result.candidate_scorer_weighted;
    result.restricted_candidate_score_map_hash =
        solve_result.candidate_score_map_hash;
    result.restricted_initial_candidate_ids =
        solve_result.initial_candidate_ids;
    result.restricted_initial_candidate_scores =
        solve_result.initial_candidate_scores;
    result.restricted_score_recomputations =
        solve_result.score_recomputations;
    result.restricted_candidates_activated_by_score =
        solve_result.candidates_activated_by_score;
    result.restricted_candidates_activated_by_full_fallback =
        solve_result.candidates_activated_by_full_fallback;
    result.restricted_candidate_deactivation_blocked_by_tail_protection_count =
        solve_result.deactivation_blocked_by_tail_protection_count;
    result.restricted_candidate_activated_by_tail_blend_count =
        solve_result.activated_by_tail_blend_count;
    result.restricted_candidate_activated_tail_top_k_overlap =
        solve_result.activated_tail_top_k_overlap;
    result.restricted_candidate_deactivated_tail_top_k_warning_count =
        solve_result.deactivated_tail_top_k_warning_count;
    result.restricted_candidate_tail_protected_candidates_by_round =
        solve_result.tail_protected_candidates_by_round;
    result.restricted_candidate_tail_protected_count_by_round =
        solve_result.tail_protected_count_by_round;
    result.restricted_candidate_deactivation_candidate_count =
        solve_result.deactivation_candidate_count;
    result.restricted_candidate_reactivation_blocked_by_cooldown_count =
        solve_result.reactivation_blocked_by_cooldown_count;
    result.restricted_candidate_oscillation_event_count =
        solve_result.oscillation_event_count;
    result.restricted_candidate_max_state_changes =
        solve_result.max_candidate_state_changes;
    result.restricted_candidate_average_state_changes =
        solve_result.average_candidate_state_changes;
    result.restricted_candidate_persistent_subproblems_enabled =
        solve_result.persistent_subproblems_enabled;
    result.restricted_candidate_subproblem_model_build_count =
        solve_result.subproblem_model_build_count;
    result.restricted_candidate_subproblem_fixed_y_update_count =
        solve_result.subproblem_fixed_y_update_count;
    result.restricted_candidate_subproblem_solve_count =
        solve_result.subproblem_solve_count;
    result.restricted_candidate_subproblem_model_rebuild_count =
        solve_result.subproblem_model_rebuild_count;
    result.restricted_candidate_subproblem_total_build_time =
        solve_result.subproblem_total_build_time;
    result.restricted_candidate_subproblem_total_update_time =
        solve_result.subproblem_total_update_time;
    result.restricted_candidate_subproblem_total_solve_time =
        solve_result.subproblem_total_solve_time;
    result.restricted_candidate_subproblem_average_update_time =
        solve_result.subproblem_average_update_time;
    result.restricted_candidate_subproblem_average_solve_time =
        solve_result.subproblem_average_solve_time;
    result.restricted_candidate_persistent_master_enabled =
        solve_result.persistent_master_enabled;
    result.restricted_candidate_master_model_build_count =
        solve_result.master_model_build_count;
    result.restricted_candidate_master_model_rebuild_count =
        solve_result.master_model_rebuild_count;
    result.restricted_candidate_master_bound_update_count =
        solve_result.master_bound_update_count;
    result.restricted_candidate_master_cut_insertions =
        solve_result.master_cut_insertions;
    result.restricted_candidate_master_duplicate_cut_insertions_skipped =
        solve_result.master_duplicate_cut_insertions_skipped;
    result.restricted_candidate_master_total_build_time =
        solve_result.master_total_build_time;
    result.restricted_candidate_master_total_bound_update_time =
        solve_result.master_total_bound_update_time;
    result.restricted_candidate_master_total_cut_insertion_time =
        solve_result.master_total_cut_insertion_time;
    result.restricted_candidate_persistent_master_note =
        solve_result.persistent_master_note;
    result.restricted_candidate_tail_score_diagnostics_enabled =
        solve_result.tail_score_diagnostics_enabled;
    result.restricted_candidate_tail_score_diagnostics =
        solve_result.tail_score_diagnostics;
    result.restricted_candidate_round_log.reserve(solve_result.round_log.size());
    for (const auto& round : solve_result.round_log) {
        result.restricted_candidate_round_log.push_back(to_io_round(round));
    }
}

void print_summary(
    const io::StandardExperimentResult& result,
    const std::filesystem::path& solution_json_path,
    const std::filesystem::path& solution_csv_path) {
    std::cout << "Run ID: " << result.run_id << "\n";
    std::cout << "Landscape: " << result.landscape << "\n";
    std::cout << "Method: " << result.method << "\n";
    std::cout << "Objective metric: " << result.objective_metric << "\n";
    std::cout << "Solver status: " << result.solver_status << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Objective in-sample: " << result.objective_in_sample << "\n";
    std::cout << "Weight profile: " << result.weight_profile << "\n";
    std::cout << "Weight map hash: " << result.weight_map_hash << "\n";
    std::cout << "Evaluator weighted objective: " << result.evaluator_weighted_objective << "\n";
    std::cout << "Objective validation diff: "
              << result.objective_validation_abs_difference << "\n";
    std::cout << "Risk measure: " << result.risk_measure << "\n";
    std::cout << "Expected loss component: " << result.expected_loss_component << "\n";
    if (result.risk_measure != "expected") {
        std::cout << "CVaR loss component: " << result.cvar_loss_component << "\n";
    }
    std::cout << "Final active candidates: "
              << result.restricted_candidate_final_active_count << "\n";
    std::cout << "Eventually activated all: "
              << (result.restricted_candidate_eventually_activated_all ? "true" : "false")
              << "\n";
    std::cout << "Cut pool size: " << result.restricted_candidate_cut_pool_size << "\n";
    std::cout << "Selected firebreaks:";
    for (const int node : result.selected_firebreaks) {
        std::cout << " " << node;
    }
    std::cout << "\n";
    std::cout << "Train expected burned area: " << result.train_expected_burned_area << "\n";
    std::cout << "Test expected burned area: " << result.test_expected_burned_area << "\n";
    std::cout << "Train expected weighted burn loss: "
              << result.train_expected_weighted_burn_loss << "\n";
    std::cout << "Test expected weighted burn loss: "
              << result.test_expected_weighted_burn_loss << "\n";
    std::cout << "Solution JSON: " << firebreak::io::path_to_string(solution_json_path) << "\n";
    std::cout << "Solution CSV: " << firebreak::io::path_to_string(solution_csv_path) << "\n";
}

}  // namespace

int FppRestrictedCandidateBranchBendersOutOfSampleRunner::run(
    const FppRestrictedCandidateBranchBendersOutOfSampleOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
    }
    if (options.run_id.empty()) {
        throw std::runtime_error("--run-id is required.");
    }
    if (options.tolerance < 0.0) {
        throw std::runtime_error("--tolerance must be nonnegative.");
    }
    if (options.root_user_cut_max_rounds <= 0) {
        throw std::runtime_error("--root-user-cut-max-rounds must be positive.");
    }
    if (!std::isnan(options.root_user_cut_tolerance) &&
        options.root_user_cut_tolerance < 0.0) {
        throw std::runtime_error("--root-user-cut-tolerance must be nonnegative.");
    }
    if (options.restricted_exact_mode && options.restricted_heuristic_mode) {
        throw std::runtime_error("--restricted-exact-mode and --restricted-heuristic-mode are mutually exclusive.");
    }
    const auto effective_risk_config = effective_risk_config_from(options.risk_config);
    const std::string method_label = method_label_for_options(
        effective_risk_config,
        options.use_lifted_lower_bounds,
        options.use_root_user_cuts,
        options.combinatorial_options,
        options.strengthening_options);
    const std::string objective_metric = objective_metric_for_risk(effective_risk_config);
    if (options.use_generated_split) {
        if (!options.train_ids.empty() || !options.test_ids.empty()) {
            throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
        }
        if (options.train_count == 0 || options.test_count == 0) {
            throw std::runtime_error("--train-count and --test-count must be positive for generated split mode.");
        }
    } else if (options.train_ids.empty() || options.test_ids.empty()) {
        throw std::runtime_error("Explicit mode requires --train-ids and --test-ids.");
    }
    if (!solver::cplex_support_enabled()) {
        throw std::runtime_error(solver::cplex_unavailable_message());
    }

    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());
    const auto output_json_path = options.output_json_path.empty()
        ? default_experiment_json_path(options.run_id)
        : firebreak::io::resolve_output_path(options.output_json_path.string());
    const auto output_csv_path = options.output_csv_path.empty()
        ? default_experiment_csv_path()
        : firebreak::io::resolve_output_path(options.output_csv_path.string());
    const auto solution_json_path = options.solution_json_path.empty()
        ? default_solution_json_path(options.run_id)
        : firebreak::io::resolve_output_path(options.solution_json_path.string());
    const auto solution_csv_path = options.solution_csv_path.empty()
        ? default_solution_csv_path(options.run_id)
        : firebreak::io::resolve_output_path(options.solution_csv_path.string());

    const auto inventory = firebreak::io::detect_message_files(results_path);
    io::ScenarioSplit split;
    std::vector<std::string> notes;
    if (options.use_generated_split) {
        split = firebreak::io::generate_train_test_split(
            inventory.ids(),
            options.seed,
            options.train_count,
            options.test_count);
        firebreak::io::save_train_test_split(
            firebreak::io::resolve_output_path("results/splits"),
            options.landscape,
            options.seed,
            options.train_count,
            options.test_count,
            split);
        notes.push_back("Generated deterministic train/test split with seed " + std::to_string(options.seed) + ".");
    } else {
        split.train_ids = options.train_ids;
        split.test_ids = options.test_ids;
        notes.push_back("Used explicit train/test scenario IDs.");
    }
    validate_disjoint(split.train_ids, split.test_ids);
    firebreak::io::validate_scenario_ids(inventory, split.train_ids);
    firebreak::io::validate_scenario_ids(inventory, split.test_ids);

    std::vector<std::string> train_warnings;
    firebreak::io::Cell2FireReader reader;
    auto train_instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        split.train_ids,
        train_warnings);

    opt::OptimizationInstanceBuilder builder;
    auto opt_instance = builder.build(train_instance, options.alpha, false);
    const auto resolved_weight_map_path = options.weight_map_file.empty()
        ? std::filesystem::path()
        : firebreak::io::resolve_input_path(options.weight_map_file.string());
    firebreak::solver::attach_weight_map_to_optimization_instance(
        opt_instance,
        resolved_weight_map_path);
    if (has_nonunit_compact_weights(opt_instance) &&
        uses_unconverted_weighted_restricted_module(
            options.use_lifted_lower_bounds,
            options.combinatorial_options,
            options.strengthening_options)) {
        throw std::runtime_error(
            "Non-homogeneous weighted run-fpp-restricted-branch-benders-oos Phase 5C1 supports only baseline LP lazy cuts and validated root user cuts; LLBI, projected LLBI, combinatorial Benders, dominance preprocessing, and conditional fixing are not yet weight-converted.");
    }
    const auto dominance_preprocess = benders::apply_fpp_global_dominance_preprocessing(
        opt_instance,
        options.strengthening_options.use_global_dominance_preprocessing);
    if (options.strengthening_options.use_global_dominance_preprocessing) {
        opt_instance = dominance_preprocess.reduced_instance;
    }

    benders::FppRestrictedCandidateBranchBendersOptions solver_options;
    solver_options.tolerance = options.tolerance;
    solver_options.time_limit_seconds = options.time_limit_seconds;
    solver_options.mip_gap = options.mip_gap;
    solver_options.threads = options.threads;
    solver_options.verbose = options.verbose;
    solver_options.risk_config = effective_risk_config;
    solver_options.use_lifted_lower_bounds = options.use_lifted_lower_bounds;
    solver_options.use_root_user_cuts = options.use_root_user_cuts;
    solver_options.root_user_cut_max_rounds = options.root_user_cut_max_rounds;
    solver_options.root_user_cut_tolerance = options.root_user_cut_tolerance;
    solver_options.combinatorial_options = options.combinatorial_options;
    solver_options.eventually_activate_all =
        options.restricted_heuristic_mode ? false : options.eventually_activate_all;
    solver_options.activation_policy = options.activation_policy;
    solver_options.activation_batch_size = options.activation_batch_size;
    solver_options.max_candidate_rounds = options.max_candidate_rounds;
    solver_options.candidate_maintenance_policy = options.candidate_maintenance_policy;
    solver_options.candidate_deactivation_batch_size =
        options.candidate_deactivation_batch_size;
    solver_options.candidate_min_active_size = options.candidate_min_active_size;
    solver_options.candidate_max_active_size = options.candidate_max_active_size;
    solver_options.candidate_deactivation_min_age =
        options.candidate_deactivation_min_age;
    solver_options.candidate_reactivation_cooldown_rounds =
        options.candidate_reactivation_cooldown_rounds;
    solver_options.protect_selected_candidates =
        options.protect_selected_candidates;
    solver_options.export_tail_score_diagnostics =
        options.export_tail_score_diagnostics;
    solver_options.candidate_score_mode = options.candidate_score_mode;
    solver_options.candidate_tail_score_gamma =
        options.candidate_tail_score_gamma;
    solver_options.candidate_tail_protection_size =
        options.candidate_tail_protection_size;
    solver_options.restricted_heuristic_mode = options.restricted_heuristic_mode;
    solver_options.stop_after_candidate_rounds = options.stop_after_candidate_rounds;
    solver_options.strengthening_options = options.strengthening_options;

    if (!options.initial_candidate_policy.empty()) {
        solver_options.initial_candidate_policy = options.initial_candidate_policy;
    } else if (!options.initial_active_candidates.empty()) {
        solver_options.initial_candidate_policy = "explicit-list";
    } else {
        solver_options.initial_candidate_policy = "burn-frequency";
    }

    if (solver_options.initial_candidate_policy == "explicit-list") {
        solver_options.initial_active_candidates =
            candidate_ids_from_cli_list(opt_instance, options.initial_active_candidates);
    } else if (solver_options.initial_candidate_policy == "burn-frequency") {
        solver_options.initial_candidate_size =
            options.initial_candidate_size >= 0
                ? options.initial_candidate_size
                : default_initial_candidate_size(opt_instance);
    }

    benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto restricted_result = solver.solve(opt_instance, solver_options);
    const auto& reported_stage = restricted_result.final_stage_status.empty()
        ? (restricted_result.last_restricted_stage_result.status.empty()
            ? restricted_result.restricted_stage_result
            : restricted_result.last_restricted_stage_result)
        : restricted_result.final_stage_result;
    auto reported_stage_for_validation = reported_stage;
    firebreak::solver::attach_direct_fpp_weight_metadata(
        reported_stage_for_validation,
        opt_instance,
        resolved_weight_map_path);
    reported_stage_for_validation.solver_weighted_objective =
        reported_stage_for_validation.objective_value;

    eval::FppRecourseEvaluator recourse_evaluator(opt_instance);
    const auto recourse_validation =
        recourse_evaluator.evaluate(
            reported_stage.selected_firebreak_indices,
            false,
            effective_risk_config.cvarBeta);
    const double evaluator_weighted_objective =
        firebreak::solver::weighted_objective_from_recourse(
            recourse_validation,
            effective_risk_config);
    firebreak::solver::attach_direct_fpp_validation(
        reported_stage_for_validation,
        evaluator_weighted_objective);
    if (reported_stage_for_validation.weight_map_hash != recourse_validation.weight_map_hash) {
        throw std::runtime_error(
            "Optimization and train evaluation weight map hashes differ in run-fpp-restricted-branch-benders-oos.");
    }
    const double evaluator_abs_diff =
        reported_stage_for_validation.objective_validation_abs_difference;
    const double evaluator_rel_diff =
        reported_stage_for_validation.objective_validation_rel_difference;
    const std::string validation_status = reported_stage_for_validation.validation_status;

    io::FirebreakSolutionRecord solution_record;
    solution_record.method = method_label;
    solution_record.landscape = options.landscape;
    solution_record.alpha = options.alpha;
    solution_record.budget = opt_instance.budget;
    solution_record.selected_firebreak_original_nodes =
        reported_stage.selected_firebreak_original_nodes;
    solution_record.selected_firebreak_indices = reported_stage.selected_firebreak_indices;
    solution_record.objective_metric = objective_metric;
    firebreak::io::save_firebreak_solution_json(solution_json_path, solution_record);
    firebreak::io::save_firebreak_solution_csv(
        solution_csv_path,
        reported_stage.selected_firebreak_original_nodes);

    const core::FirebreakSolution firebreaks(reported_stage.selected_firebreak_original_nodes);
    const auto train_eval = eval::evaluate_instance_burned_area(train_instance, firebreaks);

    std::vector<std::string> test_warnings;
    const auto test_load_start = std::chrono::steady_clock::now();
    auto test_instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        split.test_ids,
        test_warnings);
    const auto test_load_end = std::chrono::steady_clock::now();
    const double test_loading_seconds =
        std::chrono::duration<double>(test_load_end - test_load_start).count();
    const auto test_eval = eval::evaluate_instance_burned_area(test_instance, firebreaks);
    auto test_opt_instance = builder.build(test_instance, options.alpha, false);
    firebreak::solver::attach_weight_map_to_optimization_instance(
        test_opt_instance,
        resolved_weight_map_path);
    std::vector<int> selected_test_compact_indices;
    selected_test_compact_indices.reserve(reported_stage.selected_firebreak_original_nodes.size());
    for (const int original_node : reported_stage.selected_firebreak_original_nodes) {
        selected_test_compact_indices.push_back(
            test_opt_instance.node_mapper.to_index(original_node));
    }
    eval::FppRecourseEvaluator test_recourse_evaluator(test_opt_instance);
    const auto test_weighted_eval = test_recourse_evaluator.evaluate(
        selected_test_compact_indices,
        false,
        effective_risk_config.cvarBeta);
    if (reported_stage_for_validation.weight_map_hash != test_weighted_eval.weight_map_hash) {
        throw std::runtime_error(
            "Optimization and test evaluation weight map hashes differ in run-fpp-restricted-branch-benders-oos.");
    }

    io::StandardExperimentResult result;
    result.run_id = options.run_id;
    result.timestamp = io::current_timestamp_utc();
    result.landscape = options.landscape;
    result.method = method_label;
    result.objective_metric = objective_metric;
    result.alpha = options.alpha;
    result.budget = opt_instance.budget;
    result.train_scenario_count = static_cast<int>(split.train_ids.size());
    result.test_scenario_count = static_cast<int>(split.test_ids.size());
    result.train_ids = split.train_ids;
    result.test_ids = split.test_ids;
    result.solver_status = restricted_result.status;
    result.objective_in_sample = std::isfinite(restricted_result.final_full_objective)
        ? restricted_result.final_full_objective
        : restricted_result.restricted_objective;
    result.best_bound = reported_stage.best_bound;
    result.mip_gap = reported_stage.mip_gap;
    result.runtime_seconds =
        restricted_result.elapsed_time_total_seconds > 0.0
            ? restricted_result.elapsed_time_total_seconds
            : total_stage_runtime(restricted_result);
    result.solver_status_code = reported_stage.solver_status_code;
    result.explored_nodes = reported_stage.explored_nodes;
    result.num_variables = reported_stage.num_variables;
    result.num_constraints = reported_stage.num_constraints;
    result.solver_iterations = reported_stage.iterations;
    result.cuts_added =
        restricted_result.restricted_stage_lazy_cuts + restricted_result.final_stage_lazy_cuts;
    result.max_cut_violation = reported_stage.max_cut_violation;
    result.benders_use_lifted_lower_bounds =
        reported_stage.benders_use_lifted_lower_bounds;
    result.benders_lifted_lower_bound_count =
        reported_stage.benders_lifted_lower_bound_count;
    result.benders_lifted_lower_bound_precompute_time_sec =
        reported_stage.benders_lifted_lower_bound_precompute_time_sec;
    result.benders_lifted_lower_bound_nonzero_coefficients =
        reported_stage.benders_lifted_lower_bound_nonzero_coefficients;
    result.benders_lifted_lower_bound_min_rhs =
        reported_stage.benders_lifted_lower_bound_min_rhs;
    result.benders_lifted_lower_bound_max_rhs =
        reported_stage.benders_lifted_lower_bound_max_rhs;
    result.benders_lifted_lower_bound_notes =
        reported_stage.benders_lifted_lower_bound_notes;
    result.branch_benders_enabled = reported_stage.branch_benders_enabled;
    result.branch_benders_callback_calls = reported_stage.branch_benders_callback_calls;
    result.branch_benders_candidate_callback_calls =
        reported_stage.branch_benders_candidate_callback_calls;
    result.branch_benders_incumbent_callback_calls =
        reported_stage.branch_benders_incumbent_callback_calls;
    result.branch_benders_candidate_incumbents_checked =
        reported_stage.branch_benders_candidate_incumbents_checked;
    result.branch_benders_subproblems_attempted =
        reported_stage.branch_benders_subproblems_attempted;
    result.branch_benders_subproblems_solved =
        reported_stage.branch_benders_subproblems_solved;
    result.branch_benders_lazy_cuts_added =
        restricted_result.restricted_stage_lazy_cuts + restricted_result.final_stage_lazy_cuts;
    result.branch_benders_max_cut_violation = reported_stage.branch_benders_max_cut_violation;
    result.branch_benders_largest_incumbent_cut_violation =
        reported_stage.branch_benders_largest_incumbent_cut_violation;
    result.branch_benders_callback_time_sec = reported_stage.branch_benders_callback_time_sec;
    result.branch_benders_subproblem_time_sec = reported_stage.branch_benders_subproblem_time_sec;
    result.branch_benders_average_subproblem_time_sec =
        reported_stage.branch_benders_average_subproblem_time_sec;
    result.branch_benders_max_subproblem_time_sec =
        reported_stage.branch_benders_max_subproblem_time_sec;
    result.branch_benders_cut_construction_time_sec =
        reported_stage.branch_benders_cut_construction_time_sec;
    result.branch_benders_lazy_cut_insertion_time_sec =
        reported_stage.branch_benders_lazy_cut_insertion_time_sec;
    result.branch_benders_violated_cuts = reported_stage.branch_benders_violated_cuts;
    result.branch_benders_nonviolated_cuts = reported_stage.branch_benders_nonviolated_cuts;
    result.branch_benders_skipped_cuts = reported_stage.branch_benders_skipped_cuts;
    result.branch_benders_duplicate_cuts = reported_stage.branch_benders_duplicate_cuts;
    result.branch_benders_incumbent_log = reported_stage.branch_benders_incumbent_log;
    result.branch_benders_use_root_user_cuts =
        reported_stage.branch_benders_use_root_user_cuts;
    result.branch_benders_root_user_cut_max_rounds =
        reported_stage.branch_benders_root_user_cut_max_rounds;
    result.branch_benders_root_user_cut_tolerance =
        reported_stage.branch_benders_root_user_cut_tolerance;
    result.branch_benders_root_user_cut_rounds_executed =
        reported_stage.branch_benders_root_user_cut_rounds_executed;
    result.branch_benders_root_user_cut_callback_calls =
        reported_stage.branch_benders_root_user_cut_callback_calls;
    result.branch_benders_root_user_cuts_added =
        reported_stage.branch_benders_root_user_cuts_added;
    result.branch_benders_root_user_cut_scenarios_solved =
        reported_stage.branch_benders_root_user_cut_scenarios_solved;
    result.branch_benders_root_user_cut_max_violation =
        reported_stage.branch_benders_root_user_cut_max_violation;
    result.branch_benders_root_user_cut_total_time_sec =
        reported_stage.branch_benders_root_user_cut_total_time_sec;
    result.branch_benders_root_user_cut_subproblem_time_sec =
        reported_stage.branch_benders_root_user_cut_subproblem_time_sec;
    result.branch_benders_root_user_cut_skipped_reason =
        reported_stage.branch_benders_root_user_cut_skipped_reason;
    result.branch_benders_root_user_cut_only_at_root_confirmed =
        reported_stage.branch_benders_root_user_cut_only_at_root_confirmed;
    result.branch_benders_root_user_cut_round_log =
        reported_stage.branch_benders_root_user_cut_round_log;
    result.combinatorial_benders_enabled =
        reported_stage.combinatorial_benders_enabled;
    result.combinatorial_benders_lift_mode =
        reported_stage.combinatorial_benders_lift_mode;
    result.combinatorial_benders_scenario_order =
        reported_stage.combinatorial_benders_scenario_order;
    result.combinatorial_benders_cut_sampling_ratio =
        reported_stage.combinatorial_benders_cut_sampling_ratio;
    result.combinatorial_benders_fractional_separation_enabled =
        reported_stage.combinatorial_benders_fractional_separation_enabled;
    result.combinatorial_benders_initial_cuts_enabled =
        reported_stage.combinatorial_benders_initial_cuts_enabled;
    result.combinatorial_benders_integer_cuts_added =
        reported_stage.combinatorial_benders_integer_cuts_added;
    result.combinatorial_benders_fractional_cuts_added =
        reported_stage.combinatorial_benders_fractional_cuts_added;
    result.combinatorial_benders_initial_cuts_added =
        reported_stage.combinatorial_benders_initial_cuts_added;
    result.combinatorial_benders_scenarios_checked =
        reported_stage.combinatorial_benders_scenarios_checked;
    result.combinatorial_benders_separation_time_sec =
        reported_stage.combinatorial_benders_separation_time_sec;
    result.combinatorial_benders_avg_paths_per_cut =
        reported_stage.combinatorial_benders_avg_paths_per_cut;
    result.combinatorial_benders_avg_cut_nonzeros =
        reported_stage.combinatorial_benders_avg_cut_nonzeros;
    result.combinatorial_benders_num_violated_cuts =
        reported_stage.combinatorial_benders_num_violated_cuts;
    result.coverage_llbi_enabled = reported_stage.coverage_llbi_enabled;
    result.coverage_llbi_num_zeta_vars = reported_stage.coverage_llbi_num_zeta_vars;
    result.coverage_llbi_num_constraints = reported_stage.coverage_llbi_num_constraints;
    result.coverage_llbi_precompute_time_sec =
        reported_stage.coverage_llbi_precompute_time_sec;
    result.path_llbi_enabled = reported_stage.path_llbi_enabled;
    result.path_llbi_num_b_vars = reported_stage.path_llbi_num_b_vars;
    result.path_llbi_num_path_constraints = reported_stage.path_llbi_num_path_constraints;
    result.path_llbi_num_paths_used = reported_stage.path_llbi_num_paths_used;
    result.path_llbi_precompute_time_sec = reported_stage.path_llbi_precompute_time_sec;
    result.global_dominance_enabled = reported_stage.global_dominance_enabled;
    result.global_dominance_candidates_removed =
        reported_stage.global_dominance_candidates_removed;
    result.global_dominance_equivalence_classes =
        reported_stage.global_dominance_equivalence_classes;
    result.global_dominance_precompute_time_sec =
        reported_stage.global_dominance_precompute_time_sec;
    result.conditional_zero_benefit_enabled =
        reported_stage.conditional_zero_benefit_enabled;
    result.conditional_zero_benefit_fixings_attempted =
        reported_stage.conditional_zero_benefit_fixings_attempted;
    result.conditional_zero_benefit_fixings_applied =
        reported_stage.conditional_zero_benefit_fixings_applied;
    result.conditional_zero_benefit_time_sec =
        reported_stage.conditional_zero_benefit_time_sec;
    if (options.strengthening_options.use_global_dominance_preprocessing) {
        result.global_dominance_enabled = true;
        result.global_dominance_candidates_removed =
            dominance_preprocess.candidates_removed;
        result.global_dominance_equivalence_classes =
            dominance_preprocess.equivalence_classes;
        result.global_dominance_precompute_time_sec =
            dominance_preprocess.precompute_time_sec;
    }
    result.fpp_mode = "fpp_restricted_branch_benders";
    result.formulation = "restricted_branch_benders";
    result.compact_node_count = opt_instance.node_mapper.size();
    result.eligible_node_count = static_cast<int>(opt_instance.eligible_indices.size());
    for (const auto& scenario : opt_instance.scenarios) {
        result.total_observed_scenario_nodes += static_cast<int>(scenario.observed_node_indices.size());
    }
    result.total_scenario_arcs = static_cast<int>(opt_instance.total_arcs);
    result.evaluator_objective = reported_stage_for_validation.evaluator_objective;
    result.evaluator_abs_diff = evaluator_abs_diff;
    result.evaluator_rel_diff = evaluator_rel_diff;
    result.validation_status = validation_status;
    result.weight_profile = reported_stage_for_validation.weight_profile;
    result.weight_map_file = reported_stage_for_validation.weight_map_file;
    result.weight_map_hash = reported_stage_for_validation.weight_map_hash;
    result.weight_normalized = reported_stage_for_validation.weight_normalized;
    result.weight_mean = reported_stage_for_validation.weight_mean;
    result.weight_min = reported_stage_for_validation.weight_min;
    result.weight_max = reported_stage_for_validation.weight_max;
    result.weight_total = reported_stage_for_validation.weight_total;
    result.solver_weighted_objective =
        reported_stage_for_validation.solver_weighted_objective;
    result.evaluator_weighted_objective =
        reported_stage_for_validation.evaluator_weighted_objective;
    result.objective_validation_abs_difference =
        reported_stage_for_validation.objective_validation_abs_difference;
    result.objective_validation_rel_difference =
        reported_stage_for_validation.objective_validation_rel_difference;
    result.objective_validation_passed =
        reported_stage_for_validation.objective_validation_passed;
    result.selected_firebreaks = reported_stage.selected_firebreak_original_nodes;
    result.train_expected_burned_area = train_eval.expected_burned_area;
    result.train_worst_10pct_burned_area = train_eval.worst_10pct_burned_area;
    result.test_expected_burned_area = test_eval.expected_burned_area;
    result.test_worst_10pct_burned_area = test_eval.worst_10pct_burned_area;
    result.train_empirical_var_burned_area = train_eval.empirical_var_90pct_burned_area;
    result.train_empirical_cvar_burned_area = train_eval.empirical_cvar_90pct_burned_area;
    result.test_empirical_var_burned_area = test_eval.empirical_var_90pct_burned_area;
    result.test_empirical_cvar_burned_area = test_eval.empirical_cvar_90pct_burned_area;
    result.train_expected_weighted_burn_loss =
        recourse_validation.expected_weighted_burn_loss;
    result.test_expected_weighted_burn_loss =
        test_weighted_eval.expected_weighted_burn_loss;
    result.train_weighted_var = recourse_validation.weighted_loss_statistics.var;
    result.test_weighted_var = test_weighted_eval.weighted_loss_statistics.var;
    result.train_weighted_cvar = recourse_validation.weighted_loss_statistics.cvar;
    result.test_weighted_cvar = test_weighted_eval.weighted_loss_statistics.cvar;
    result.train_percentage_landscape_value_burned =
        recourse_validation.expected_percentage_landscape_value_burned;
    result.test_percentage_landscape_value_burned =
        test_weighted_eval.expected_percentage_landscape_value_burned;
    result.train_percentage_high_value_weight_burned =
        recourse_validation.expected_percentage_high_value_weight_burned;
    result.test_percentage_high_value_weight_burned =
        test_weighted_eval.expected_percentage_high_value_weight_burned;
    result.risk_measure = reported_stage.risk_measure;
    result.cvar_beta = reported_stage.cvar_beta;
    result.cvar_lambda = reported_stage.cvar_lambda;
    result.risk_threshold_value = reported_stage.risk_threshold_value;
    result.expected_loss_component = reported_stage.expected_loss_component;
    result.cvar_loss_component = reported_stage.cvar_loss_component;
    result.train_evaluation_runtime_seconds = train_eval.total_runtime_seconds;
    result.test_evaluation_runtime_seconds = test_eval.total_runtime_seconds;
    result.test_scenario_loading_runtime_seconds = test_loading_seconds;
    result.train_graph_classification_ratios =
        analysis::graph_classification_ratio_summary(train_instance.scenarios);
    result.test_graph_classification_ratios =
        analysis::graph_classification_ratio_summary(test_instance.scenarios);
    result.notes = restricted_result.notes;
    if (options.strengthening_options.use_global_dominance_preprocessing) {
        result.notes.insert(
            result.notes.end(),
            dominance_preprocess.notes.begin(),
            dominance_preprocess.notes.end());
    }
    result.notes.push_back(
        validation_status == "pass"
            ? "Weighted FppRecourseEvaluator objective validation passed for final selected firebreaks."
            : "Weighted FppRecourseEvaluator objective validation warning for final selected firebreaks.");
    result.notes.insert(result.notes.end(), notes.begin(), notes.end());
    result.notes.push_back("Restricted-candidate method label: " + method_label + ".");
    if (effective_risk_config.type == risk::RiskMeasureType::Expected) {
        result.notes.push_back("FPP restricted Branch-Benders risk measure: expected value.");
    } else if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
        result.notes.push_back("FPP restricted Branch-Benders risk measure: pure CVaR.");
    } else {
        result.notes.push_back("FPP restricted Branch-Benders risk measure: mean-CVaR blend.");
    }
    result.notes.push_back("DPV and DPV-CVaR are not supported by this command.");
    for (const auto& warning : train_warnings) {
        result.notes.push_back("Train reader warning: " + warning);
    }
    for (const auto& warning : test_warnings) {
        result.notes.push_back("Test reader warning: " + warning);
    }
    result.notes.push_back("Test scenarios were used only for out-of-sample evaluation.");
    attach_restricted_diagnostics(restricted_result, result);

    firebreak::io::write_experiment_result_json(output_json_path, result);
    firebreak::io::append_experiment_result_csv(output_csv_path, result);
    export_strengthening_summaries(options.strengthening_options, result);

    print_summary(result, solution_json_path, solution_csv_path);
    std::cout << "Wrote result JSON: " << firebreak::io::path_to_string(output_json_path) << "\n";
    std::cout << "Appended result CSV: " << firebreak::io::path_to_string(output_csv_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments
