#include "experiments/MethodDispatcher.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "analysis/GraphDiagnostics.hpp"
#include "benchmarks/StaticDpvBenchmark.hpp"
#include "benchmarks/StaticDpvMipBenchmark.hpp"
#include "benders/DpvBendersSolver.hpp"
#include "benders/DpvBranchBendersSolver.hpp"
#include "core/FirebreakSolution.hpp"
#include "benders/FppBendersSolver.hpp"
#include "benders/FppBranchBendersSolver.hpp"
#include "benders/FppRestrictedCandidateBranchBendersSolver.hpp"
#include "cuts/DominatorCuts.hpp"
#include "eval/BurnedAreaEvaluator.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "experiments/BatchExperimentConfig.hpp"
#include "heuristics/GreedyHeuristic.hpp"
#include "heuristics/GreedyMetrics.hpp"
#include "heuristics/ReachabilityGreedyWarmStart.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/SolutionIO.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/CplexEnvironment.hpp"
#include "solver/DpvSaaCplexModel.hpp"
#include "solver/FppCutReachabilityCplexModel.hpp"
#include "solver/FppSaaCplexModel.hpp"
#include "solver/WarmStart.hpp"

namespace firebreak::experiments {

namespace {

bool is_solver_method(const std::string& method) {
    return is_fpp_solver_method(method) ||
           method == "DPV-SAA" ||
           method == "DPV-Benders" ||
           is_dpv_branch_benders_method(method);
}

bool is_greedy_method(const std::string& method) {
    return method == "Greedy-DPV3" ||
           method == "Greedy-DPV2" ||
           method == "Greedy-Betweenness" ||
           method == "Greedy-Closeness";
}

bool method_needs_dpv_indices(const std::string& method, const std::string& warm_start_policy) {
    if (method == "DPV-SAA" ||
        method == "DPV-Benders" ||
        method == "Static-DPV" ||
        method == "Static-DPV-MIP" ||
        is_dpv_branch_benders_method(method)) {
        return true;
    }
    if (fpp_method_variant_settings(method).is_fpp_saa &&
        warm_start_policy == "static-dpv-for-fpp") {
        return true;
    }
    if (method == "DPV-SAA" && warm_start_policy == "static-dpv-for-dpv") {
        return true;
    }
    return false;
}

void validate_fpp_plumbing_options(const MethodDispatchRequest& request) {
    if (request.enable_local_search) {
        throw std::runtime_error("Reachability-greedy local search is not implemented yet.");
    }
    (void)normalize_fpp_formulation(request.fpp_formulation);
}

std::string format_compact_double(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

risk::RiskMeasureConfig effective_fpp_risk_config_for_method(
    const MethodDispatchRequest& request,
    const std::string& method) {
    auto variant = fpp_method_variant_settings(method);
    auto config = variant.risk_config;
    if (request.risk_measure_specified) {
        if (request.risk_config.type != config.type) {
            throw std::runtime_error(
                "Manifest risk_measure conflicts with FPP method label " + method + ".");
        }
        if (request.cvar_beta_specified) {
            config.cvarBeta = request.risk_config.cvarBeta;
        }
        if (request.cvar_lambda_specified) {
            config.cvarLambda = request.risk_config.cvarLambda;
        }
    } else {
        if (request.cvar_beta_specified) {
            config.cvarBeta = request.risk_config.cvarBeta;
        }
        if (config.type == risk::RiskMeasureType::MeanCVaR &&
            request.cvar_lambda_specified) {
            config.cvarLambda = request.risk_config.cvarLambda;
        }
    }
    if (config.type == risk::RiskMeasureType::CVaR) {
        config.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(config);
    return config;
}

std::string objective_metric_for_fpp_risk(
    const risk::RiskMeasureConfig& config,
    const std::string& expected_metric) {
    if (config.type == risk::RiskMeasureType::Expected) {
        return expected_metric;
    }
    if (config.type == risk::RiskMeasureType::CVaR) {
        return "cvar_burned_area_beta_" + format_compact_double(config.cvarBeta);
    }
    return "mean_cvar_burned_area_beta_" +
        format_compact_double(config.cvarBeta) +
        "_lambda_" +
        format_compact_double(config.cvarLambda);
}

std::filesystem::path solution_json_path(
    const std::filesystem::path& output_dir,
    const std::string& run_id) {
    return output_dir / "solutions" / (run_id + "_solution.json");
}

std::filesystem::path solution_csv_path(
    const std::filesystem::path& output_dir,
    const std::string& run_id) {
    return output_dir / "solutions" / (run_id + "_solution.csv");
}

std::filesystem::path result_json_path(
    const std::filesystem::path& output_dir,
    const std::string& run_id) {
    return output_dir / "json" / (run_id + ".json");
}

solver::WarmStart build_policy_warm_start(
    const std::string& method,
    const std::string& warm_start_policy,
    const opt::OptimizationInstance& opt) {
    solver::WarmStart empty;
    if (warm_start_policy == "none") {
        return empty;
    }
    if (fpp_method_variant_settings(method).is_fpp_saa &&
        warm_start_policy != "greedy-dpv3-for-fpp" &&
        warm_start_policy != "static-dpv-for-fpp") {
        return empty;
    }
    if (method == "DPV-SAA" &&
        warm_start_policy != "greedy-dpv3-for-dpv" &&
        warm_start_policy != "static-dpv-for-dpv") {
        return empty;
    }

    std::vector<int> original_nodes;
    std::string source = "warm_start_policy:" + warm_start_policy;
    if (warm_start_policy == "greedy-dpv3-for-fpp" ||
        warm_start_policy == "greedy-dpv3-for-dpv") {
        heuristics::GreedyHeuristic heuristic;
        const auto result = heuristic.runGreedy(opt, heuristics::GreedyMetricType::DPV3, true, false);
        original_nodes = result.selected_firebreak_original_nodes;
    } else if (warm_start_policy == "static-dpv-for-fpp" ||
               warm_start_policy == "static-dpv-for-dpv") {
        benchmarks::StaticDpvBenchmark benchmark;
        const auto result = benchmark.run(opt, opt.budget);
        original_nodes = result.selected_firebreak_original_nodes;
    }

    if (original_nodes.empty()) {
        return empty;
    }

    return solver::prepare_warm_start_from_original_nodes(
        original_nodes,
        opt,
        opt.budget,
        source);
}

heuristics::ReachabilityGreedyWarmStartOptions greedy_warm_start_options_from_request(
    const MethodDispatchRequest& request) {
    heuristics::ReachabilityGreedyWarmStartOptions options;
    options.candidate_pool_size_multiplier = request.candidate_pool_size_multiplier;
    options.candidate_pool_min_size = request.candidate_pool_min_size;
    options.enable_greedy_exact_marginal = request.enable_greedy_exact_marginal;
    options.verbose = request.verbose;
    return options;
}

std::string format_double_for_note(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

int total_observed_scenario_nodes(const opt::OptimizationInstance& opt) {
    int total = 0;
    for (const auto& scenario : opt.scenarios) {
        total += static_cast<int>(scenario.observed_node_indices.size());
    }
    return total;
}

void attach_fpp_recourse_validation(
    const opt::OptimizationInstance& opt,
    solver::ModelResult& solver_result) {
    if (solver_result.status.find("No feasible") != std::string::npos) {
        solver_result.validation_status = "not_applicable";
        return;
    }

    eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate(solver_result.selected_firebreak_indices, false);
    const double validation_reference = std::isfinite(solver_result.expected_loss_component)
        ? solver_result.expected_loss_component
        : solver_result.objective_value;
    solver_result.evaluator_objective = recourse.expected_burned_area;
    solver_result.evaluator_abs_diff =
        std::fabs(recourse.expected_burned_area - validation_reference);
    const double scale = std::max(1.0, std::fabs(validation_reference));
    solver_result.evaluator_rel_diff = solver_result.evaluator_abs_diff / scale;

    if (solver_result.evaluator_abs_diff > 1.0e-5 &&
        solver_result.evaluator_rel_diff > 1.0e-6) {
        solver_result.validation_status = "warn";
        std::ostringstream note;
        note << "FppRecourseEvaluator validation warning: expected loss component "
             << validation_reference
             << " differs from evaluator objective "
             << recourse.expected_burned_area
             << " (abs_diff=" << solver_result.evaluator_abs_diff
             << ", rel_diff=" << solver_result.evaluator_rel_diff << ").";
        solver_result.notes.push_back(note.str());
    } else {
        solver_result.validation_status = "pass";
        solver_result.notes.push_back(
            "FppRecourseEvaluator validation passed for final selected firebreaks.");
    }
}

io::RestrictedCandidateRoundResult to_io_restricted_round(
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

void attach_restricted_candidate_diagnostics(
    const benders::FppRestrictedCandidateBranchBendersResult& restricted_result,
    io::StandardExperimentResult& result) {
    result.restricted_candidate_enabled = restricted_result.restricted_candidate_enabled;
    result.restricted_candidate_exact_mode = restricted_result.restricted_candidate_exact_mode;
    result.restricted_candidate_bounds_enabled =
        restricted_result.candidate_bounds_enabled;
    result.restricted_candidate_bounds_weighted =
        restricted_result.candidate_bounds_weighted;
    result.restricted_candidate_bound_type =
        restricted_result.candidate_bound_type;
    result.restricted_candidate_bound_map_hash =
        restricted_result.candidate_bound_map_hash;
    result.restricted_candidates_evaluated_by_bound =
        restricted_result.candidates_evaluated_by_bound;
    result.restricted_candidates_permanently_pruned =
        restricted_result.candidates_permanently_pruned;
    result.restricted_candidates_not_pruned_due_to_safety =
        restricted_result.candidates_not_pruned_due_to_safety;
    result.restricted_candidate_early_exactness_certificate_used =
        restricted_result.early_exactness_certificate_used;
    result.restricted_candidate_full_activation_avoided =
        restricted_result.full_activation_avoided;
    result.restricted_candidate_unvalidated_bound_rejected =
        restricted_result.unvalidated_bound_rejected;
    result.restricted_candidate_initial_policy = restricted_result.initial_candidate_policy;
    result.restricted_candidate_activation_policy = restricted_result.activation_policy;
    result.restricted_candidate_initial_active_count =
        restricted_result.initial_active_candidate_count;
    result.restricted_candidate_final_active_count =
        restricted_result.active_candidate_count_final;
    result.restricted_candidate_final_active_fraction =
        restricted_result.active_candidate_fraction_final;
    result.restricted_candidate_eventually_activated_all =
        restricted_result.eventually_activated_all;
    result.restricted_candidate_full_activation_performed =
        restricted_result.full_activation_performed;
    result.restricted_candidate_restricted_lower_bound_is_global =
        restricted_result.restricted_lower_bound_is_global;
    result.restricted_candidate_final_lower_bound_is_global =
        restricted_result.final_lower_bound_is_global;
    result.restricted_candidate_cut_reuse_enabled = restricted_result.cut_reuse_enabled;
    result.restricted_candidate_cut_pool_size = restricted_result.cut_pool_size;
    result.restricted_candidate_cut_pool_peak_size =
        restricted_result.cut_pool_peak_size;
    result.restricted_candidate_cut_pool_evictions =
        restricted_result.cut_pool_evictions;
    result.restricted_candidate_cut_pool_reinstantiations =
        restricted_result.cut_pool_reinstantiations;
    result.restricted_candidate_rounds = restricted_result.candidate_rounds;
    result.restricted_candidate_cuts_reused_in_full_stage =
        restricted_result.cuts_reused_in_full_stage;
    result.restricted_candidate_duplicate_cuts_skipped =
        restricted_result.duplicate_cuts_skipped;
    result.restricted_candidate_heuristic_mode_enabled =
        restricted_result.heuristic_mode_enabled;
    result.restricted_candidate_stopped_before_full_activation =
        restricted_result.stopped_before_full_activation;
    result.restricted_candidate_global_optimality_certified =
        restricted_result.global_optimality_certified;
    result.restricted_candidate_global_time_budget_enabled =
        restricted_result.global_time_budget_enabled;
    result.restricted_candidate_time_budget_exhausted =
        restricted_result.time_budget_exhausted;
    result.restricted_candidate_global_time_limit_seconds =
        restricted_result.global_time_limit_seconds;
    result.restricted_candidate_elapsed_time_total_seconds =
        restricted_result.elapsed_time_total_seconds;
    result.restricted_candidate_initial_stage_runtime =
        restricted_result.restricted_initial_stage_runtime;
    result.restricted_candidate_activation_stage_runtime_total =
        restricted_result.restricted_activation_stage_runtime_total;
    result.restricted_candidate_final_full_stage_runtime =
        restricted_result.restricted_final_full_stage_runtime;
    result.restricted_candidate_final_stage_time_limit =
        restricted_result.restricted_final_stage_time_limit;
    result.restricted_candidate_reason_for_heuristic_stop =
        restricted_result.reason_for_heuristic_stop;
    result.restricted_candidate_restricted_objective =
        restricted_result.restricted_objective;
    result.restricted_candidate_restricted_best_bound =
        restricted_result.restricted_best_bound;
    result.restricted_candidate_restricted_bound_is_global =
        restricted_result.restricted_bound_is_global;
    result.restricted_candidate_active_fraction_at_stop =
        restricted_result.active_candidate_fraction_at_stop;
    result.restricted_candidate_maintenance_policy =
        restricted_result.candidate_maintenance_policy;
    result.restricted_candidate_maintenance_weighted =
        restricted_result.maintenance_weighted;
    result.restricted_candidate_maintenance_map_hash =
        restricted_result.maintenance_map_hash;
    result.restricted_candidate_deactivation_enabled =
        restricted_result.deactivation_enabled;
    result.restricted_candidate_deactivation_rounds =
        restricted_result.deactivation_rounds;
    result.restricted_candidate_active_target =
        restricted_result.active_candidate_target;
    result.restricted_candidate_considered_for_deactivation =
        restricted_result.candidates_considered_for_deactivation;
    result.restricted_candidate_deactivated_total =
        restricted_result.candidates_deactivated;
    result.restricted_candidate_reactivated_total =
        restricted_result.candidates_reactivated;
    result.restricted_candidate_protected_from_deactivation_total =
        restricted_result.candidates_protected_from_deactivation;
    result.restricted_candidate_full_activation_overrode_maintenance =
        restricted_result.full_activation_overrode_maintenance;
    result.restricted_candidate_min_active_size =
        restricted_result.candidate_min_active_size;
    result.restricted_candidate_max_active_size =
        restricted_result.candidate_max_active_size;
    result.restricted_candidate_deactivation_batch_size =
        restricted_result.candidate_deactivation_batch_size;
    result.restricted_candidate_deactivation_min_age =
        restricted_result.candidate_deactivation_min_age;
    result.restricted_candidate_reactivation_cooldown_rounds =
        restricted_result.candidate_reactivation_cooldown_rounds;
    result.restricted_candidate_protect_selected_candidates =
        restricted_result.protect_selected_candidates;
    result.restricted_candidate_protected_selected_count =
        restricted_result.protected_selected_count;
    result.restricted_candidate_protected_min_age_count =
        restricted_result.protected_min_age_count;
    result.restricted_candidate_protected_cooldown_count =
        restricted_result.protected_cooldown_count;
    result.restricted_candidate_protected_newly_activated_count =
        restricted_result.protected_newly_activated_count;
    result.restricted_candidate_protected_tail_count =
        restricted_result.protected_tail_count;
    result.restricted_candidate_score_mode = restricted_result.candidate_score_mode;
    result.restricted_candidate_tail_score_gamma =
        restricted_result.candidate_tail_score_gamma;
    result.restricted_candidate_tail_protection_size =
        restricted_result.candidate_tail_protection_size;
    result.restricted_candidate_deactivation_blocked_by_tail_protection_count =
        restricted_result.deactivation_blocked_by_tail_protection_count;
    result.restricted_candidate_activated_by_tail_blend_count =
        restricted_result.activated_by_tail_blend_count;
    result.restricted_candidate_activated_tail_top_k_overlap =
        restricted_result.activated_tail_top_k_overlap;
    result.restricted_candidate_deactivated_tail_top_k_warning_count =
        restricted_result.deactivated_tail_top_k_warning_count;
    result.restricted_candidate_tail_protected_candidates_by_round =
        restricted_result.tail_protected_candidates_by_round;
    result.restricted_candidate_tail_protected_count_by_round =
        restricted_result.tail_protected_count_by_round;
    result.restricted_candidate_deactivation_candidate_count =
        restricted_result.deactivation_candidate_count;
    result.restricted_candidate_reactivation_blocked_by_cooldown_count =
        restricted_result.reactivation_blocked_by_cooldown_count;
    result.restricted_candidate_oscillation_event_count =
        restricted_result.oscillation_event_count;
    result.restricted_candidate_max_state_changes =
        restricted_result.max_candidate_state_changes;
    result.restricted_candidate_average_state_changes =
        restricted_result.average_candidate_state_changes;
    result.restricted_candidate_persistent_subproblems_enabled =
        restricted_result.persistent_subproblems_enabled;
    result.restricted_candidate_subproblem_model_build_count =
        restricted_result.subproblem_model_build_count;
    result.restricted_candidate_subproblem_fixed_y_update_count =
        restricted_result.subproblem_fixed_y_update_count;
    result.restricted_candidate_subproblem_solve_count =
        restricted_result.subproblem_solve_count;
    result.restricted_candidate_subproblem_model_rebuild_count =
        restricted_result.subproblem_model_rebuild_count;
    result.restricted_candidate_subproblem_total_build_time =
        restricted_result.subproblem_total_build_time;
    result.restricted_candidate_subproblem_total_update_time =
        restricted_result.subproblem_total_update_time;
    result.restricted_candidate_subproblem_total_solve_time =
        restricted_result.subproblem_total_solve_time;
    result.restricted_candidate_subproblem_average_update_time =
        restricted_result.subproblem_average_update_time;
    result.restricted_candidate_subproblem_average_solve_time =
        restricted_result.subproblem_average_solve_time;
    result.restricted_candidate_persistent_master_enabled =
        restricted_result.persistent_master_enabled;
    result.restricted_candidate_master_model_build_count =
        restricted_result.master_model_build_count;
    result.restricted_candidate_master_model_rebuild_count =
        restricted_result.master_model_rebuild_count;
    result.restricted_candidate_master_bound_update_count =
        restricted_result.master_bound_update_count;
    result.restricted_candidate_master_cut_insertions =
        restricted_result.master_cut_insertions;
    result.restricted_candidate_master_duplicate_cut_insertions_skipped =
        restricted_result.master_duplicate_cut_insertions_skipped;
    result.restricted_candidate_master_total_build_time =
        restricted_result.master_total_build_time;
    result.restricted_candidate_master_total_bound_update_time =
        restricted_result.master_total_bound_update_time;
    result.restricted_candidate_master_total_cut_insertion_time =
        restricted_result.master_total_cut_insertion_time;
    result.restricted_candidate_persistent_master_note =
        restricted_result.persistent_master_note;
    result.restricted_candidate_tail_score_diagnostics_enabled =
        restricted_result.tail_score_diagnostics_enabled;
    result.restricted_candidate_tail_score_diagnostics =
        restricted_result.tail_score_diagnostics;
    for (const auto& round : restricted_result.round_log) {
        result.restricted_candidate_round_log.push_back(to_io_restricted_round(round));
    }
}

solver::WarmStart build_reachability_greedy_warm_start(
    const opt::OptimizationInstance& opt,
    const MethodDispatchRequest& request,
    heuristics::ReachabilityGreedyWarmStartResult& greedy_result) {
    heuristics::ReachabilityGreedyWarmStart heuristic(
        opt,
        greedy_warm_start_options_from_request(request));
    greedy_result = heuristic.run();

    std::vector<int> original_nodes;
    original_nodes.reserve(greedy_result.selected_firebreak_compact_nodes.size());
    for (const int compact_node : greedy_result.selected_firebreak_compact_nodes) {
        original_nodes.push_back(opt.node_mapper.to_node(compact_node));
    }

    auto warm_start = solver::prepare_warm_start_from_original_nodes(
        original_nodes,
        opt,
        opt.budget,
        "reachability-greedy-warm-start");
    warm_start.notes.push_back("Warm start generated by reachability-greedy FPP recourse heuristic.");
    warm_start.notes.push_back(
        "Reachability-greedy empty objective: " +
        format_double_for_note(greedy_result.empty_objective) + ".");
    warm_start.notes.push_back(
        "Reachability-greedy final objective: " +
        format_double_for_note(greedy_result.objective) + ".");
    warm_start.notes.push_back(
        "Reachability-greedy exact marginal evaluations: " +
        std::to_string(greedy_result.exact_evaluations) + ".");
    return warm_start;
}

cuts::DominatorCutOptions dominator_options_from_request(const MethodDispatchRequest& request) {
    cuts::DominatorCutOptions options;
    options.enabled = request.enable_dominator_cuts;
    options.max_aggregate_dominator_cuts_per_scenario =
        request.max_aggregate_dominator_cuts_per_scenario;
    options.max_individual_dominator_cuts_per_scenario =
        request.max_individual_dominator_cuts_per_scenario;
    options.verbose = request.verbose;
    return options;
}

cuts::SeparatorCutOptions separator_options_from_request(const MethodDispatchRequest& request) {
    cuts::SeparatorCutOptions options;
    options.enabled = request.enable_separator_cuts;
    options.sep_at_root = request.sep_at_root;
    options.sep_frequency_nodes = request.sep_frequency_nodes;
    options.sep_max_scenarios_per_call = request.sep_max_scenarios_per_call;
    options.sep_max_nodes_per_scenario = request.sep_max_nodes_per_scenario;
    options.sep_max_cuts_per_call = request.sep_max_cuts_per_call;
    options.sep_min_violation = request.sep_min_violation;
    options.sep_max_cut_cardinality = request.sep_max_cut_cardinality;
    options.verbose = request.verbose;
    return options;
}

void save_solution_record(
    const MethodDispatchRequest& request,
    const opt::OptimizationInstance& opt,
    const std::vector<int>& selected_indices,
    const std::vector<int>& selected_original_nodes,
    const std::string& objective_metric,
    const std::vector<double>& selected_scores) {
    io::FirebreakSolutionRecord record;
    record.method = request.method;
    record.landscape = request.landscape;
    record.alpha = request.alpha;
    record.budget = opt.budget;
    record.selected_firebreak_indices = selected_indices;
    record.selected_firebreak_original_nodes = selected_original_nodes;
    record.objective_metric = objective_metric;
    record.selected_firebreak_scores = selected_scores;

    io::save_firebreak_solution_json(solution_json_path(request.output_dir, request.run_id), record);
    io::save_firebreak_solution_csv(solution_csv_path(request.output_dir, request.run_id), selected_original_nodes);
}

}  // namespace

io::StandardExperimentResult MethodDispatcher::run_method(const MethodDispatchRequest& request) const {
    const std::string method = normalize_batch_method_name(request.method);
    const std::string warm_start_policy = normalize_warm_start_policy(request.warm_start_policy);

    if (request.train_ids.empty() || request.test_ids.empty()) {
        throw std::runtime_error("Method dispatch requires nonempty train and test scenario IDs.");
    }
    if (!is_fpp_solver_method(method) &&
        request.risk_measure_specified &&
        request.risk_config.type != risk::RiskMeasureType::Expected) {
        if (method == "DPV-SAA" ||
            method == "DPV-Benders" ||
            is_dpv_branch_benders_method(method)) {
            throw std::runtime_error("DPV-CVaR optimization is out of scope and not implemented.");
        }
        throw std::runtime_error("risk_measure=cvar is only supported for FPP CVaR method labels.");
    }
    const auto fpp_variant = fpp_method_variant_settings(method);
    if (fpp_variant.is_fpp_saa) {
        validate_fpp_plumbing_options(request);
    }
    if (request.enable_separator_cuts && !fpp_variant.is_fpp_saa) {
        throw std::runtime_error("Separator cuts are currently supported only for FPP-SAA.");
    }
    if (is_solver_method(method) && !solver::cplex_support_enabled()) {
        throw std::runtime_error(solver::cplex_unavailable_message());
    }

    std::vector<std::string> train_warnings;
    std::vector<std::string> test_warnings;
    io::Cell2FireReader reader;
    auto train_instance = reader.load_instance(
        request.landscape,
        request.forest_path,
        request.results_path,
        request.train_ids,
        train_warnings);
    const auto test_load_start = std::chrono::steady_clock::now();
    auto test_instance = reader.load_instance(
        request.landscape,
        request.forest_path,
        request.results_path,
        request.test_ids,
        test_warnings);
    const auto test_load_end = std::chrono::steady_clock::now();
    const double test_loading_seconds = std::chrono::duration<double>(test_load_end - test_load_start).count();

    opt::OptimizationInstanceBuilder builder;
    const bool build_dpv_indices = method_needs_dpv_indices(method, warm_start_policy);
    auto opt_instance = builder.build(train_instance, request.alpha, build_dpv_indices);
    benders::FppStrengtheningOptions fpp_strengthening_options;
    if (fpp_variant.is_fpp_solver) {
        fpp_strengthening_options.use_coverage_llbi =
            request.use_coverage_llbi || fpp_variant.use_coverage_llbi;
        fpp_strengthening_options.use_path_llbi =
            request.use_path_llbi || fpp_variant.use_path_llbi;
        fpp_strengthening_options.path_llbi_max_paths_per_node =
            request.path_llbi_max_paths_per_node;
        fpp_strengthening_options.use_projected_coverage_llbi_exp =
            request.projected_llbi_options.use_projected_coverage_llbi_exp ||
            fpp_variant.projected_llbi_options.use_projected_coverage_llbi_exp;
        fpp_strengthening_options.use_projected_path_llbi_exp =
            request.projected_llbi_options.use_projected_path_llbi_exp ||
            fpp_variant.projected_llbi_options.use_projected_path_llbi_exp;
        fpp_strengthening_options.use_projected_coverage_llbi_poly =
            request.projected_llbi_options.use_projected_coverage_llbi_poly ||
            fpp_variant.projected_llbi_options.use_projected_coverage_llbi_poly;
        fpp_strengthening_options.use_projected_path_llbi_poly =
            request.projected_llbi_options.use_projected_path_llbi_poly ||
            fpp_variant.projected_llbi_options.use_projected_path_llbi_poly;
        fpp_strengthening_options.projected_llbi_root_rounds =
            request.projected_llbi_options.root_rounds;
        fpp_strengthening_options.projected_llbi_max_cuts_per_round =
            request.projected_llbi_options.max_cuts_per_round;
        fpp_strengthening_options.projected_llbi_violation_tolerance =
            request.projected_llbi_options.violation_tolerance;
        fpp_strengthening_options.projected_llbi_cut_density_limit =
            request.projected_llbi_options.cut_density_limit;
        fpp_strengthening_options.projected_poly_max_cuts =
            request.projected_llbi_options.poly_max_cuts;
        fpp_strengthening_options.projected_llbi_export_cuts_path =
            request.projected_llbi_options.export_cuts_path;
        fpp_strengthening_options.use_global_dominance_preprocessing =
            request.use_global_dominance_preprocessing ||
            fpp_variant.use_global_dominance_preprocessing;
        fpp_strengthening_options.use_conditional_zero_benefit_fixing =
            request.use_conditional_zero_benefit_fixing ||
            fpp_variant.use_conditional_zero_benefit_fixing;
    }
    const auto dominance_preprocess = benders::apply_fpp_global_dominance_preprocessing(
        opt_instance,
        fpp_strengthening_options.use_global_dominance_preprocessing);
    if (fpp_strengthening_options.use_global_dominance_preprocessing) {
        opt_instance = dominance_preprocess.reduced_instance;
    }

    solver::ModelResult solver_result;
    benders::FppRestrictedCandidateBranchBendersResult restricted_diagnostics;
    bool has_restricted_diagnostics = false;
    std::string objective_metric;
    std::vector<double> selected_scores;
    std::vector<std::string> notes;

    if (fpp_variant.is_fpp_saa) {
        const auto risk_config = effective_fpp_risk_config_for_method(request, method);
        const std::string fpp_formulation = normalize_fpp_formulation(request.fpp_formulation);
        if (request.enable_greedy_warm_start && warm_start_policy != "none") {
            throw std::runtime_error(
                "enable_greedy_warm_start cannot be combined with warm_start_policy=" +
                warm_start_policy + "; choose one warm-start source.");
        }

        heuristics::ReachabilityGreedyWarmStartResult greedy_start_result;
        solver::WarmStart warm_start =
            request.enable_greedy_warm_start
                ? build_reachability_greedy_warm_start(opt_instance, request, greedy_start_result)
                : build_policy_warm_start(method, warm_start_policy, opt_instance);
        const solver::WarmStart* warm_start_ptr = warm_start.enabled ? &warm_start : nullptr;
        if (request.enable_greedy_warm_start) {
            std::cout << "  reachability-greedy warm start:"
                      << " empty=" << greedy_start_result.empty_objective
                      << " final=" << greedy_start_result.objective
                      << " selected=" << greedy_start_result.selected_firebreak_compact_nodes.size()
                      << " exact_evaluations=" << greedy_start_result.exact_evaluations
                      << " runtime=" << greedy_start_result.runtime_sec << "\n" << std::flush;
        }
        const auto dominator_options = dominator_options_from_request(request);
        const cuts::DominatorCutOptions* dominator_options_ptr =
            dominator_options.enabled ? &dominator_options : nullptr;
        const auto separator_options = separator_options_from_request(request);
        const cuts::SeparatorCutOptions* separator_options_ptr =
            separator_options.enabled ? &separator_options : nullptr;

        if (fpp_formulation == "base") {
            solver::FppSaaCplexModel model;
            solver_result = model.solve(
                opt_instance,
                request.time_limit_seconds,
                request.mip_gap,
                request.threads,
                request.verbose,
                warm_start_ptr,
                dominator_options_ptr,
                separator_options_ptr,
                risk_config,
                &fpp_strengthening_options);
        } else if (fpp_formulation == "cut") {
            solver::FppCutReachabilityCplexModel model;
            solver_result = model.solve(
                opt_instance,
                request.time_limit_seconds,
                request.mip_gap,
                request.threads,
                request.verbose,
                warm_start_ptr,
                dominator_options_ptr,
                separator_options_ptr,
                risk_config);
        } else {
            throw std::runtime_error("Unsupported FPP formulation after normalization: " + fpp_formulation);
        }
        solver_result.method = method;
        solver_result.fpp_mode = request.fpp_mode.empty()
            ? fpp_mode_name_from_settings(
                fpp_formulation,
                request.enable_greedy_warm_start,
                request.enable_dominator_cuts,
                request.enable_separator_cuts,
                request.enable_local_search)
            : normalize_fpp_mode(request.fpp_mode);
        solver_result.formulation = fpp_formulation;
        solver_result.dominator_cuts_enabled = request.enable_dominator_cuts;
        solver_result.separator_cuts_enabled = request.enable_separator_cuts;
        solver_result.greedy_warm_start_enabled = request.enable_greedy_warm_start;
        solver_result.local_search_enabled = request.enable_local_search;
        solver_result.compact_node_count = opt_instance.node_mapper.size();
        solver_result.eligible_node_count = static_cast<int>(opt_instance.eligible_indices.size());
        solver_result.total_observed_scenario_nodes = total_observed_scenario_nodes(opt_instance);
        solver_result.total_scenario_arcs = static_cast<int>(opt_instance.total_arcs);
        if (request.enable_greedy_warm_start) {
            solver_result.heuristic_time_sec = greedy_start_result.runtime_sec;
            solver_result.heuristic_objective = greedy_start_result.objective;
            solver_result.heuristic_exact_evaluations = greedy_start_result.exact_evaluations;
            solver_result.heuristic_selected_count =
                static_cast<int>(greedy_start_result.selected_firebreak_compact_nodes.size());
            const std::string mip_start_description =
                fpp_formulation == "cut"
                    ? "a full y/x/q MIP start"
                    : "a y-only MIP start";
            solver_result.notes.push_back(
                "Reachability-greedy warm start was used as " +
                mip_start_description + " for the " + fpp_formulation + " FPP formulation.");
            solver_result.notes.insert(
                solver_result.notes.end(),
                greedy_start_result.notes.begin(),
                greedy_start_result.notes.end());
        }
        attach_fpp_recourse_validation(opt_instance, solver_result);
        objective_metric = objective_metric_for_fpp_risk(risk_config, "expected_burned_area");
    } else if (fpp_variant.is_fpp_benders) {
        const auto risk_config = effective_fpp_risk_config_for_method(request, method);
        benders::FppBendersOptions options;
        options.max_iterations = 20;
        options.tolerance = 1.0e-6;
        options.time_limit_seconds = request.time_limit_seconds;
        options.mip_gap = request.mip_gap;
        options.threads = request.threads;
        options.verbose = request.verbose;
        options.risk_config = risk_config;

        benders::FppBendersSolver solver;
        solver_result = solver.solve(opt_instance, options);
        solver_result.method = method;
        solver_result.formulation = "benders";
        solver_result.fpp_mode = "fpp_benders_explicit_loop";
        attach_fpp_recourse_validation(opt_instance, solver_result);
        objective_metric = objective_metric_for_fpp_risk(
            risk_config,
            "expected_burned_area_benders_lp_subproblems");
    } else if (fpp_variant.is_fpp_branch_benders) {
        const auto risk_config = effective_fpp_risk_config_for_method(request, method);
        benders::FppBranchBendersOptions options;
        options.tolerance = 1.0e-6;
        options.time_limit_seconds = request.time_limit_seconds;
        options.mip_gap = request.mip_gap;
        options.threads = request.threads;
        options.verbose = request.verbose;
        options.risk_config = risk_config;
        options.use_lifted_lower_bounds = fpp_variant.use_lifted_lower_bounds;
        options.use_root_user_cuts = fpp_variant.use_root_user_cuts;
        options.root_user_cut_max_rounds = request.root_user_cut_max_rounds;
        options.root_user_cut_tolerance = request.root_user_cut_tolerance;
        options.combinatorial_options = fpp_variant.use_combinatorial_benders
            ? fpp_variant.combinatorial_options
            : request.combinatorial_options;
        if (request.combinatorial_options.enabled) {
            options.combinatorial_options = request.combinatorial_options;
        }
        options.strengthening_options = fpp_strengthening_options;

        benders::FppBranchBendersSolver solver;
        solver_result = solver.solve(opt_instance, options);
        solver_result.method = method;
        solver_result.formulation = "branch_benders";
        solver_result.fpp_mode = options.combinatorial_options.enabled
            ? "fpp_branch_benders_combinatorial"
            : "fpp_branch_benders";
        solver_result.notes.push_back(
            "Batch method label " + method +
            " maps to callback FPP Branch-and-Benders with risk_measure=" +
            risk::to_string(risk_config.type) +
            ", LLBI=" +
            std::string(options.use_lifted_lower_bounds ? "true" : "false") +
            ", root_user_cuts=" +
            std::string(options.use_root_user_cuts ? "true" : "false") +
            ", combinatorial=" +
            std::string(options.combinatorial_options.enabled ? "true" : "false") + ".");
        attach_fpp_recourse_validation(opt_instance, solver_result);
        objective_metric = objective_metric_for_fpp_risk(
            risk_config,
            options.combinatorial_options.enabled
                ? "expected_burned_area_branch_benders_combinatorial"
                : "expected_burned_area_branch_benders_lazy_subproblems");
    } else if (fpp_variant.is_fpp_restricted_branch_benders) {
        const auto risk_config = effective_fpp_risk_config_for_method(request, method);
        benders::FppRestrictedCandidateBranchBendersOptions options;
        options.tolerance = 1.0e-6;
        options.time_limit_seconds = request.time_limit_seconds;
        options.mip_gap = request.mip_gap;
        options.threads = request.threads;
        options.verbose = request.verbose;
        options.risk_config = risk_config;
        options.use_lifted_lower_bounds = fpp_variant.use_lifted_lower_bounds;
        options.use_root_user_cuts = fpp_variant.use_root_user_cuts;
        options.root_user_cut_max_rounds = request.root_user_cut_max_rounds;
        options.root_user_cut_tolerance = request.root_user_cut_tolerance;
        options.combinatorial_options = fpp_variant.use_combinatorial_benders
            ? fpp_variant.combinatorial_options
            : request.combinatorial_options;
        if (request.combinatorial_options.enabled) {
            options.combinatorial_options = request.combinatorial_options;
        }
        options.strengthening_options = fpp_strengthening_options;
        options.initial_candidate_policy = "burn-frequency";
        options.initial_candidate_size = std::max(
            opt_instance.budget,
            std::min(
                static_cast<int>(opt_instance.eligible_indices.size()),
                std::max(5 * opt_instance.budget, 50)));
        options.activation_policy = "none";
        options.eventually_activate_all = true;

        benders::FppRestrictedCandidateBranchBendersSolver solver;
        const auto restricted_result = solver.solve(opt_instance, options);
        restricted_diagnostics = restricted_result;
        has_restricted_diagnostics = true;
        solver_result = restricted_result.final_stage_status.empty()
            ? (restricted_result.last_restricted_stage_result.status.empty()
                ? restricted_result.restricted_stage_result
                : restricted_result.last_restricted_stage_result)
            : restricted_result.final_stage_result;
        solver_result.method = method;
        solver_result.status = restricted_result.status;
        solver_result.objective_value = std::isfinite(restricted_result.final_full_objective)
            ? restricted_result.final_full_objective
            : restricted_result.restricted_objective;
        solver_result.runtime_seconds = restricted_result.elapsed_time_total_seconds;
        solver_result.formulation = "restricted_branch_benders";
        solver_result.fpp_mode = options.combinatorial_options.enabled
            ? "fpp_restricted_branch_benders_combinatorial"
            : "fpp_restricted_branch_benders";
        solver_result.notes.push_back(
            "Batch method label " + method +
            " maps to exact-mode restricted-candidate FPP Branch-and-Benders with burn-frequency initialization, eventual full activation, and risk_measure=" +
            risk::to_string(risk_config.type) +
            ", LLBI=" +
            std::string(options.use_lifted_lower_bounds ? "true" : "false") +
            ", root_user_cuts=" +
            std::string(options.use_root_user_cuts ? "true" : "false") +
            ", combinatorial=" +
            std::string(options.combinatorial_options.enabled ? "true" : "false") + ".");
        attach_fpp_recourse_validation(opt_instance, solver_result);
        objective_metric = objective_metric_for_fpp_risk(
            risk_config,
            options.combinatorial_options.enabled
                ? "expected_burned_area_restricted_branch_benders_combinatorial"
                : "expected_burned_area_restricted_branch_benders");
    } else if (method == "DPV-SAA") {
        solver::WarmStart warm_start = build_policy_warm_start(method, warm_start_policy, opt_instance);
        const solver::WarmStart* warm_start_ptr = warm_start.enabled ? &warm_start : nullptr;
        solver::DpvSaaCplexModel model;
        solver_result = model.solve(
            opt_instance,
            request.time_limit_seconds,
            request.mip_gap,
            request.threads,
            request.verbose,
            warm_start_ptr);
        solver_result.method = "DPV-SAA";
        solver_result.formulation = "base";
        objective_metric = "solution_dependent_DPV_unit_weights";
    } else if (method == "DPV-Benders") {
        solver::WarmStart warm_start = build_policy_warm_start("DPV-SAA", warm_start_policy, opt_instance);
        const solver::WarmStart* warm_start_ptr = warm_start.enabled ? &warm_start : nullptr;
        benders::DpvBendersOptions options;
        options.max_iterations = 20;
        options.tolerance = 1.0e-6;
        options.time_limit_seconds = request.time_limit_seconds;
        options.mip_gap = request.mip_gap;
        options.threads = request.threads;
        options.verbose = request.verbose;
        options.use_lifted_lower_bounds = false;
        options.warm_start = warm_start_ptr;

        benders::DpvBendersSolver solver;
        solver_result = solver.solve(opt_instance, options);
        solver_result.method = "DPV-Benders";
        solver_result.formulation = "benders";
        solver_result.notes.push_back(
            "Batch method label DPV-Benders maps to explicit-loop DPV-SAA Benders with LLBI disabled.");
        objective_metric = "solution_dependent_DPV_unit_weights_benders";
    } else if (is_dpv_branch_benders_method(method)) {
        const auto variant = dpv_branch_benders_variant_settings(method);
        solver::WarmStart warm_start = build_policy_warm_start("DPV-SAA", warm_start_policy, opt_instance);
        const solver::WarmStart* warm_start_ptr = warm_start.enabled ? &warm_start : nullptr;
        benders::DpvBranchBendersOptions options;
        options.tolerance = 1.0e-6;
        options.time_limit_seconds = request.time_limit_seconds;
        options.mip_gap = request.mip_gap;
        options.threads = request.threads;
        options.verbose = request.verbose;
        options.use_lifted_lower_bounds = variant.use_lifted_lower_bounds;
        options.use_root_user_cuts = variant.use_root_user_cuts;
        options.root_user_cut_max_rounds = request.root_user_cut_max_rounds;
        options.root_user_cut_tolerance = request.root_user_cut_tolerance;
        options.warm_start = warm_start_ptr;

        benders::DpvBranchBendersSolver solver;
        solver_result = solver.solve(opt_instance, options);
        solver_result.method = method;
        solver_result.formulation = "base";
        solver_result.notes.push_back(
            "Batch method label " + method +
            " maps to callback DPV Branch-and-Benders with LLBI=" +
            std::string(options.use_lifted_lower_bounds ? "true" : "false") +
            " and root_user_cuts=" +
            std::string(options.use_root_user_cuts ? "true" : "false") + ".");
        objective_metric = "solution_dependent_DPV_unit_weights_branch_benders";
    } else if (method == "Static-DPV") {
        benchmarks::StaticDpvBenchmark benchmark;
        const auto static_result = benchmark.run(opt_instance, opt_instance.budget);
        solver_result.method = "Static-DPV";
        solver_result.formulation = "base";
        solver_result.status = "NotApplicable";
        solver_result.objective_value = static_result.total_static_dpv_score;
        solver_result.best_bound = std::numeric_limits<double>::quiet_NaN();
        solver_result.mip_gap = std::numeric_limits<double>::quiet_NaN();
        solver_result.runtime_seconds = static_result.runtime_seconds;
        solver_result.selected_firebreak_indices = static_result.selected_firebreak_indices;
        solver_result.selected_firebreak_original_nodes = static_result.selected_firebreak_original_nodes;
        solver_result.num_variables = 0;
        solver_result.num_constraints = 0;
        objective_metric = "precomputed_static_DPV_unit_weights";
        selected_scores = static_result.selected_scores;
    } else if (method == "Static-DPV-MIP") {
        benchmarks::StaticDpvMipBenchmark benchmark;
        const auto static_result = benchmark.run(opt_instance, opt_instance.budget);
        solver_result.method = "Static-DPV-MIP";
        solver_result.formulation = "static_mip_cardinality";
        solver_result.status = static_result.solver_status;
        solver_result.objective_value = static_result.total_static_dpv_score;
        solver_result.best_bound = static_result.total_static_dpv_score;
        solver_result.mip_gap = 0.0;
        solver_result.runtime_seconds = static_result.runtime_seconds;
        solver_result.selected_firebreak_indices = static_result.selected_firebreak_indices;
        solver_result.selected_firebreak_original_nodes = static_result.selected_firebreak_original_nodes;
        solver_result.num_variables = static_result.num_variables;
        solver_result.num_constraints = static_result.num_constraints;
        solver_result.notes.push_back(
            "Static-DPV-MIP uses unit downstream values and closed downstream reachability.");
        solver_result.notes.push_back(
            "Static-DPV-MIP scores are computed from training scenarios only.");
        solver_result.notes.push_back(
            "Static-DPV-MIP does not multiply by out-degree, update scores after selection, or discount overlap.");
        solver_result.notes.push_back(
            "Default treatment_loss is zero; EMPC and treatment-loss constraints are not enabled.");
        solver_result.notes.push_back(
            "Pure-cardinality Static-DPV-MIP is solved exactly by deterministic top-budget sorting.");
        objective_metric = "static_DPV_MIP_unit_downstream_value";
        selected_scores = static_result.selected_scores;
    } else if (is_greedy_method(method)) {
        const std::string metric_name = method.substr(std::string("Greedy-").size());
        const auto metric = heuristics::parseGreedyMetricType(metric_name);
        heuristics::GreedyHeuristic heuristic;
        const auto greedy_result = heuristic.runGreedy(opt_instance, metric, true, request.verbose);
        solver_result.method = greedy_result.method_name;
        solver_result.formulation = "base";
        solver_result.status = "NotApplicable";
        solver_result.objective_value = greedy_result.total_score;
        solver_result.best_bound = std::numeric_limits<double>::quiet_NaN();
        solver_result.mip_gap = std::numeric_limits<double>::quiet_NaN();
        solver_result.runtime_seconds = greedy_result.runtime_seconds;
        solver_result.selected_firebreak_indices = greedy_result.selected_firebreak_indices;
        solver_result.selected_firebreak_original_nodes = greedy_result.selected_firebreak_original_nodes;
        solver_result.num_variables = 0;
        solver_result.num_constraints = 0;
        objective_metric = greedy_result.objective_metric;
        selected_scores = greedy_result.selected_scores;
        notes.insert(notes.end(), greedy_result.metric_notes.begin(), greedy_result.metric_notes.end());
    } else {
        throw std::runtime_error("Unsupported method in dispatcher: " + method);
    }

    if (fpp_strengthening_options.use_global_dominance_preprocessing) {
        solver_result.global_dominance_enabled = true;
        solver_result.global_dominance_structural_weight_safe =
            dominance_preprocess.structural_weight_safe;
        solver_result.global_dominance_original_candidate_count =
            dominance_preprocess.original_candidate_count;
        solver_result.global_dominance_candidates_removed =
            dominance_preprocess.candidates_removed;
        solver_result.global_dominance_equivalence_classes =
            dominance_preprocess.equivalence_classes;
        solver_result.global_dominance_post_candidate_count =
            dominance_preprocess.post_candidate_count;
        solver_result.global_dominance_warm_start_replacements =
            dominance_preprocess.warm_start_replacements;
        solver_result.global_dominance_precompute_time_sec =
            dominance_preprocess.precompute_time_sec;
        solver_result.notes.insert(
            solver_result.notes.end(),
            dominance_preprocess.notes.begin(),
            dominance_preprocess.notes.end());
    }

    save_solution_record(
        request,
        opt_instance,
        solver_result.selected_firebreak_indices,
        solver_result.selected_firebreak_original_nodes,
        objective_metric,
        selected_scores);

    const core::FirebreakSolution firebreaks(solver_result.selected_firebreak_original_nodes);
    const auto train_eval = eval::evaluate_instance_burned_area(train_instance, firebreaks);
    const auto test_eval = eval::evaluate_instance_burned_area(test_instance, firebreaks);

    io::StandardExperimentResult result;
    result.experiment_id = request.experiment_id;
    result.case_id = request.case_id;
    result.run_id = request.run_id;
    result.timestamp = io::current_timestamp_utc();
    result.landscape = request.landscape;
    result.method = method;
    result.objective_metric = objective_metric;
    result.alpha = request.alpha;
    result.budget = opt_instance.budget;
    result.train_scenario_count = static_cast<int>(request.train_ids.size());
    result.test_scenario_count = static_cast<int>(request.test_ids.size());
    result.train_ids = request.train_ids;
    result.test_ids = request.test_ids;
    result.solver_status = solver_result.status;
    result.objective_in_sample = solver_result.objective_value;
    result.best_bound = solver_result.best_bound;
    result.mip_gap = solver_result.mip_gap;
    result.runtime_seconds = solver_result.runtime_seconds;
    result.solver_status_code = solver_result.solver_status_code;
    result.explored_nodes = solver_result.explored_nodes;
    result.num_variables = solver_result.num_variables;
    result.num_constraints = solver_result.num_constraints;
    result.solver_iterations = solver_result.iterations;
    result.cuts_added = solver_result.cuts_added;
    result.max_cut_violation = solver_result.max_cut_violation;
    result.benders_status = solver_result.benders_status;
    result.benders_iterations = solver_result.benders_iterations;
    result.benders_cuts_added = solver_result.benders_cuts_added;
    result.benders_final_max_cut_violation = solver_result.benders_final_max_cut_violation;
    result.benders_largest_intermediate_cut_violation =
        solver_result.benders_largest_intermediate_cut_violation;
    result.benders_termination_reason = solver_result.benders_termination_reason;
    result.benders_master_solve_time_sec = solver_result.benders_master_solve_time_sec;
    result.benders_subproblem_time_sec = solver_result.benders_subproblem_time_sec;
    result.benders_subproblems_solved = solver_result.benders_subproblems_solved;
    result.benders_average_subproblem_time_sec =
        solver_result.benders_average_subproblem_time_sec;
    result.benders_max_subproblem_time_sec = solver_result.benders_max_subproblem_time_sec;
    result.benders_use_lifted_lower_bounds = solver_result.benders_use_lifted_lower_bounds;
    result.benders_lifted_lower_bound_count = solver_result.benders_lifted_lower_bound_count;
    result.benders_lifted_lower_bound_precompute_time_sec =
        solver_result.benders_lifted_lower_bound_precompute_time_sec;
    result.benders_lifted_lower_bound_nonzero_coefficients =
        solver_result.benders_lifted_lower_bound_nonzero_coefficients;
    result.benders_lifted_lower_bound_min_rhs =
        solver_result.benders_lifted_lower_bound_min_rhs;
    result.benders_lifted_lower_bound_max_rhs =
        solver_result.benders_lifted_lower_bound_max_rhs;
    result.benders_lifted_lower_bound_notes =
        solver_result.benders_lifted_lower_bound_notes;
    result.benders_iteration_log = solver_result.benders_iteration_log;
    result.branch_benders_enabled = solver_result.branch_benders_enabled;
    result.branch_benders_callback_calls = solver_result.branch_benders_callback_calls;
    result.branch_benders_candidate_callback_calls =
        solver_result.branch_benders_candidate_callback_calls;
    result.branch_benders_incumbent_callback_calls =
        solver_result.branch_benders_incumbent_callback_calls;
    result.branch_benders_candidate_incumbents_checked =
        solver_result.branch_benders_candidate_incumbents_checked;
    result.branch_benders_subproblems_attempted =
        solver_result.branch_benders_subproblems_attempted;
    result.branch_benders_subproblems_solved = solver_result.branch_benders_subproblems_solved;
    result.branch_benders_lazy_cuts_added = solver_result.branch_benders_lazy_cuts_added;
    result.branch_benders_max_cut_violation = solver_result.branch_benders_max_cut_violation;
    result.branch_benders_largest_incumbent_cut_violation =
        solver_result.branch_benders_largest_incumbent_cut_violation;
    result.branch_benders_callback_time_sec = solver_result.branch_benders_callback_time_sec;
    result.branch_benders_subproblem_time_sec = solver_result.branch_benders_subproblem_time_sec;
    result.branch_benders_average_subproblem_time_sec =
        solver_result.branch_benders_average_subproblem_time_sec;
    result.branch_benders_max_subproblem_time_sec =
        solver_result.branch_benders_max_subproblem_time_sec;
    result.branch_benders_cut_construction_time_sec =
        solver_result.branch_benders_cut_construction_time_sec;
    result.branch_benders_lazy_cut_insertion_time_sec =
        solver_result.branch_benders_lazy_cut_insertion_time_sec;
    result.branch_benders_violated_cuts = solver_result.branch_benders_violated_cuts;
    result.branch_benders_nonviolated_cuts = solver_result.branch_benders_nonviolated_cuts;
    result.branch_benders_skipped_cuts = solver_result.branch_benders_skipped_cuts;
    result.branch_benders_duplicate_cuts = solver_result.branch_benders_duplicate_cuts;
    result.branch_benders_incumbent_log = solver_result.branch_benders_incumbent_log;
    result.combinatorial_benders_enabled = solver_result.combinatorial_benders_enabled;
    result.combinatorial_benders_lift_mode =
        solver_result.combinatorial_benders_lift_mode;
    result.combinatorial_benders_scenario_order =
        solver_result.combinatorial_benders_scenario_order;
    result.combinatorial_benders_cut_sampling_ratio =
        solver_result.combinatorial_benders_cut_sampling_ratio;
    result.combinatorial_benders_fractional_separation_enabled =
        solver_result.combinatorial_benders_fractional_separation_enabled;
    result.combinatorial_benders_initial_cuts_enabled =
        solver_result.combinatorial_benders_initial_cuts_enabled;
    result.combinatorial_benders_integer_cuts_added =
        solver_result.combinatorial_benders_integer_cuts_added;
    result.combinatorial_benders_fractional_cuts_added =
        solver_result.combinatorial_benders_fractional_cuts_added;
    result.combinatorial_benders_initial_cuts_added =
        solver_result.combinatorial_benders_initial_cuts_added;
    result.combinatorial_benders_scenarios_checked =
        solver_result.combinatorial_benders_scenarios_checked;
    result.combinatorial_benders_separation_time_sec =
        solver_result.combinatorial_benders_separation_time_sec;
    result.combinatorial_benders_avg_paths_per_cut =
        solver_result.combinatorial_benders_avg_paths_per_cut;
    result.combinatorial_benders_avg_cut_nonzeros =
        solver_result.combinatorial_benders_avg_cut_nonzeros;
    result.combinatorial_benders_num_violated_cuts =
        solver_result.combinatorial_benders_num_violated_cuts;
    result.coverage_llbi_enabled = solver_result.coverage_llbi_enabled;
    result.coverage_llbi_num_zeta_vars = solver_result.coverage_llbi_num_zeta_vars;
    result.coverage_llbi_num_constraints = solver_result.coverage_llbi_num_constraints;
    result.coverage_llbi_precompute_time_sec = solver_result.coverage_llbi_precompute_time_sec;
    result.path_llbi_enabled = solver_result.path_llbi_enabled;
    result.path_llbi_num_b_vars = solver_result.path_llbi_num_b_vars;
    result.path_llbi_num_path_constraints = solver_result.path_llbi_num_path_constraints;
    result.path_llbi_num_paths_used = solver_result.path_llbi_num_paths_used;
    result.path_llbi_precompute_time_sec = solver_result.path_llbi_precompute_time_sec;
    result.projected_coverage_llbi_enabled =
        solver_result.projected_coverage_llbi_enabled;
    result.projected_path_llbi_enabled =
        solver_result.projected_path_llbi_enabled;
    result.projected_llbi_family = solver_result.projected_llbi_family;
    result.projected_llbi_strategy = solver_result.projected_llbi_strategy;
    result.projected_llbi_mode = solver_result.projected_llbi_mode;
    result.projected_llbi_root_rounds = solver_result.projected_llbi_root_rounds;
    result.projected_llbi_cuts_added = solver_result.projected_llbi_cuts_added;
    result.projected_llbi_coverage_cuts_added =
        solver_result.projected_llbi_coverage_cuts_added;
    result.projected_llbi_path_cuts_added =
        solver_result.projected_llbi_path_cuts_added;
    result.projected_llbi_violated_cuts_found =
        solver_result.projected_llbi_violated_cuts_found;
    result.projected_llbi_separation_time_sec =
        solver_result.projected_llbi_separation_time_sec;
    result.projected_llbi_solve_time_sec =
        solver_result.projected_llbi_solve_time_sec;
    result.projected_llbi_total_time_sec =
        solver_result.projected_llbi_total_time_sec;
    result.projected_llbi_total_nonzeros =
        solver_result.projected_llbi_total_nonzeros;
    result.projected_llbi_avg_nonzeros_per_cut =
        solver_result.projected_llbi_avg_nonzeros_per_cut;
    result.projected_llbi_max_nonzeros_per_cut =
        solver_result.projected_llbi_max_nonzeros_per_cut;
    result.projected_llbi_min_violation =
        solver_result.projected_llbi_min_violation;
    result.projected_llbi_max_violation =
        solver_result.projected_llbi_max_violation;
    result.projected_llbi_avg_violation =
        solver_result.projected_llbi_avg_violation;
    result.projected_llbi_root_bound_initial =
        solver_result.projected_llbi_root_bound_initial;
    result.projected_llbi_root_bound_final =
        solver_result.projected_llbi_root_bound_final;
    result.projected_llbi_root_bound_improvement_abs =
        solver_result.projected_llbi_root_bound_improvement_abs;
    result.projected_llbi_root_bound_improvement_pct =
        solver_result.projected_llbi_root_bound_improvement_pct;
    result.projected_poly_candidate_cuts_generated =
        solver_result.projected_poly_candidate_cuts_generated;
    result.projected_poly_candidate_cuts_added =
        solver_result.projected_poly_candidate_cuts_added;
    result.projected_poly_enumeration_truncated =
        solver_result.projected_poly_enumeration_truncated;
    result.projected_poly_enumeration_limit =
        solver_result.projected_poly_enumeration_limit;
    result.projected_exp_separated_cuts_added =
        solver_result.projected_exp_separated_cuts_added;
    result.projected_exp_separation_rounds =
        solver_result.projected_exp_separation_rounds;
    result.projected_exp_candidate_cuts_generated =
        solver_result.projected_exp_candidate_cuts_generated;
    result.projected_exp_candidate_cuts_added =
        solver_result.projected_exp_candidate_cuts_added;
    result.projected_exp_enumeration_truncated =
        solver_result.projected_exp_enumeration_truncated;
    result.projected_exp_enumeration_limit =
        solver_result.projected_exp_enumeration_limit;
    result.global_dominance_enabled = solver_result.global_dominance_enabled;
    result.global_dominance_structural_weight_safe =
        solver_result.global_dominance_structural_weight_safe;
    result.global_dominance_original_candidate_count =
        solver_result.global_dominance_original_candidate_count;
    result.global_dominance_candidates_removed = solver_result.global_dominance_candidates_removed;
    result.global_dominance_equivalence_classes =
        solver_result.global_dominance_equivalence_classes;
    result.global_dominance_post_candidate_count =
        solver_result.global_dominance_post_candidate_count;
    result.global_dominance_warm_start_replacements =
        solver_result.global_dominance_warm_start_replacements;
    result.global_dominance_precompute_time_sec =
        solver_result.global_dominance_precompute_time_sec;
    result.conditional_zero_benefit_enabled =
        solver_result.conditional_zero_benefit_enabled;
    result.conditional_zero_benefit_structural_weight_safe =
        solver_result.conditional_zero_benefit_structural_weight_safe;
    result.conditional_zero_benefit_callback_calls =
        solver_result.conditional_zero_benefit_callback_calls;
    result.conditional_zero_benefit_nodes_checked =
        solver_result.conditional_zero_benefit_nodes_checked;
    result.conditional_zero_benefit_candidates_checked =
        solver_result.conditional_zero_benefit_candidates_checked;
    result.conditional_zero_benefit_fixings_attempted =
        solver_result.conditional_zero_benefit_fixings_attempted;
    result.conditional_zero_benefit_fixings_applied =
        solver_result.conditional_zero_benefit_fixings_applied;
    result.conditional_zero_benefit_variables_fixed_zero =
        solver_result.conditional_zero_benefit_variables_fixed_zero;
    result.conditional_zero_benefit_scenarios_reachability_computed =
        solver_result.conditional_zero_benefit_scenarios_reachability_computed;
    result.conditional_zero_benefit_time_sec =
        solver_result.conditional_zero_benefit_time_sec;
    result.branch_benders_use_root_user_cuts = solver_result.branch_benders_use_root_user_cuts;
    result.branch_benders_root_user_cut_max_rounds =
        solver_result.branch_benders_root_user_cut_max_rounds;
    result.branch_benders_root_user_cut_tolerance =
        solver_result.branch_benders_root_user_cut_tolerance;
    result.branch_benders_root_user_cut_rounds_executed =
        solver_result.branch_benders_root_user_cut_rounds_executed;
    result.branch_benders_root_user_cut_callback_calls =
        solver_result.branch_benders_root_user_cut_callback_calls;
    result.branch_benders_root_user_cuts_added =
        solver_result.branch_benders_root_user_cuts_added;
    result.branch_benders_root_user_cut_scenarios_solved =
        solver_result.branch_benders_root_user_cut_scenarios_solved;
    result.branch_benders_root_user_cut_max_violation =
        solver_result.branch_benders_root_user_cut_max_violation;
    result.branch_benders_root_user_cut_total_time_sec =
        solver_result.branch_benders_root_user_cut_total_time_sec;
    result.branch_benders_root_user_cut_subproblem_time_sec =
        solver_result.branch_benders_root_user_cut_subproblem_time_sec;
    result.branch_benders_root_user_cut_skipped_reason =
        solver_result.branch_benders_root_user_cut_skipped_reason;
    result.branch_benders_root_user_cut_only_at_root_confirmed =
        solver_result.branch_benders_root_user_cut_only_at_root_confirmed;
    result.branch_benders_root_user_cut_round_log =
        solver_result.branch_benders_root_user_cut_round_log;
    result.fpp_mode = solver_result.fpp_mode;
    result.formulation = solver_result.formulation;
    result.dominator_cuts_enabled = solver_result.dominator_cuts_enabled;
    result.separator_cuts_enabled = solver_result.separator_cuts_enabled;
    result.greedy_warm_start_enabled = solver_result.greedy_warm_start_enabled;
    result.local_search_enabled = solver_result.local_search_enabled;
    result.separator_cuts_added = solver_result.separator_cuts_added;
    result.separator_min_cut_calls = solver_result.separator_min_cut_calls;
    result.separator_callback_invocations = solver_result.separator_callback_invocations;
    result.separator_duplicate_cuts_skipped = solver_result.separator_duplicate_cuts_skipped;
    result.separator_large_cuts_skipped = solver_result.separator_large_cuts_skipped;
    result.separator_time_sec = solver_result.separator_time_sec;
    result.dominator_cuts_added = solver_result.dominator_cuts_added;
    result.dominator_aggregate_cuts_added = solver_result.dominator_aggregate_cuts_added;
    result.dominator_individual_cuts_added = solver_result.dominator_individual_cuts_added;
    result.dominator_dag_scenarios = solver_result.dominator_dag_scenarios;
    result.dominator_fallback_scenarios = solver_result.dominator_fallback_scenarios;
    result.dominator_preprocessing_time_sec = solver_result.dominator_preprocessing_time_sec;
    result.heuristic_time_sec = solver_result.heuristic_time_sec;
    result.heuristic_objective = solver_result.heuristic_objective;
    result.heuristic_exact_evaluations = solver_result.heuristic_exact_evaluations;
    result.heuristic_selected_count = solver_result.heuristic_selected_count;
    result.selected_firebreaks = solver_result.selected_firebreak_original_nodes;
    result.warm_start_used = solver_result.warm_start_used;
    result.mip_start_accepted = solver_result.mip_start_accepted;
    result.compact_node_count = solver_result.compact_node_count;
    result.eligible_node_count = solver_result.eligible_node_count;
    result.total_observed_scenario_nodes = solver_result.total_observed_scenario_nodes;
    result.total_scenario_arcs = solver_result.total_scenario_arcs;
    result.evaluator_objective = solver_result.evaluator_objective;
    result.evaluator_abs_diff = solver_result.evaluator_abs_diff;
    result.evaluator_rel_diff = solver_result.evaluator_rel_diff;
    result.risk_measure = solver_result.risk_measure;
    result.cvar_beta = solver_result.cvar_beta;
    result.cvar_lambda = solver_result.cvar_lambda;
    result.risk_threshold_value = solver_result.risk_threshold_value;
    result.expected_loss_component = solver_result.expected_loss_component;
    result.cvar_loss_component = solver_result.cvar_loss_component;
    result.validation_status = solver_result.validation_status;
    result.warm_start_source = solver_result.warm_start_source;
    result.warm_start_valid_nodes = solver_result.warm_start_valid_nodes;
    result.warm_start_ignored_nodes = solver_result.warm_start_ignored_nodes;
    result.warm_start_notes = solver_result.warm_start_notes;
    result.train_expected_burned_area = train_eval.expected_burned_area;
    result.train_worst_10pct_burned_area = train_eval.worst_10pct_burned_area;
    result.test_expected_burned_area = test_eval.expected_burned_area;
    result.test_worst_10pct_burned_area = test_eval.worst_10pct_burned_area;
    result.train_empirical_var_burned_area = train_eval.empirical_var_90pct_burned_area;
    result.train_empirical_cvar_burned_area = train_eval.empirical_cvar_90pct_burned_area;
    result.test_empirical_var_burned_area = test_eval.empirical_var_90pct_burned_area;
    result.test_empirical_cvar_burned_area = test_eval.empirical_cvar_90pct_burned_area;
    result.train_evaluation_runtime_seconds = train_eval.total_runtime_seconds;
    result.test_evaluation_runtime_seconds = test_eval.total_runtime_seconds;
    result.test_scenario_loading_runtime_seconds = test_loading_seconds;
    result.train_graph_classification_ratios =
        analysis::graph_classification_ratio_summary(train_instance.scenarios);
    result.test_graph_classification_ratios =
        analysis::graph_classification_ratio_summary(test_instance.scenarios);
    result.notes = solver_result.notes;
    if (has_restricted_diagnostics) {
        attach_restricted_candidate_diagnostics(restricted_diagnostics, result);
    }
    result.notes.insert(result.notes.end(), notes.begin(), notes.end());
    for (const auto& warning : train_warnings) {
        result.notes.push_back("Train reader warning: " + warning);
    }
    for (const auto& warning : test_warnings) {
        result.notes.push_back("Test reader warning: " + warning);
    }
    result.notes.push_back("Test scenarios were used only for out-of-sample evaluation.");

    io::write_experiment_result_json(result_json_path(request.output_dir, request.run_id), result);
    io::append_experiment_result_csv(request.output_csv, result);
    return result;
}

}  // namespace firebreak::experiments
