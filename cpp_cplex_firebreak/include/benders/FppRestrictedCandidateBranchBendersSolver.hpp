#pragma once

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/CvarTailScoreDiagnostics.hpp"
#include "benders/FppCombinatorialBenders.hpp"
#include "benders/FppStrengthening.hpp"
#include "benders/RestrictedCandidateCutPool.hpp"
#include "benders/RestrictedCandidateManager.hpp"
#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/ModelResult.hpp"

namespace firebreak::benders {

struct FppRestrictedCandidateBranchBendersOptions {
    double tolerance = 1.0e-6;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    bool verbose = false;
    risk::RiskMeasureConfig risk_config;
    bool use_lifted_lower_bounds = false;
    bool use_root_user_cuts = false;
    int root_user_cut_max_rounds = 1;
    double root_user_cut_tolerance = std::numeric_limits<double>::quiet_NaN();
    FppCombinatorialBendersOptions combinatorial_options;
    FppStrengtheningOptions strengthening_options;
    std::vector<int> initial_active_candidates;
    std::string initial_candidate_policy = "explicit-list";
    int initial_candidate_size = -1;
    std::string activation_policy = "none";
    int activation_batch_size = 0;
    int max_candidate_rounds = 0;
    std::string candidate_maintenance_policy = "none";
    int candidate_deactivation_batch_size = -1;
    int candidate_min_active_size = -1;
    int candidate_max_active_size = -1;
    int candidate_deactivation_min_age = 1;
    int candidate_reactivation_cooldown_rounds = 1;
    bool protect_selected_candidates = true;
    bool export_tail_score_diagnostics = false;
    std::string candidate_score_mode = "generic";
    double candidate_tail_score_gamma = 0.5;
    int candidate_tail_protection_size = -1;
    bool solve_restricted_stage = true;
    bool eventually_activate_all = true;
    bool restricted_heuristic_mode = false;
    int stop_after_candidate_rounds = -1;
};

struct FppRestrictedCandidateRoundLog {
    int round_index = 0;
    std::string stage_type;
    std::string risk_measure = "expected";
    int active_candidate_count = 0;
    double active_candidate_fraction = 0.0;
    std::vector<int> newly_activated_candidates;
    std::string activation_policy;
    std::vector<std::pair<int, double>> top_scores_considered;
    std::string solve_status;
    double objective = std::numeric_limits<double>::quiet_NaN();
    double best_bound = std::numeric_limits<double>::quiet_NaN();
    double mip_gap = std::numeric_limits<double>::quiet_NaN();
    double runtime_seconds = 0.0;
    double time_limit_seconds = 0.0;
    double remaining_global_time_before_stage = std::numeric_limits<double>::quiet_NaN();
    double remaining_global_time_after_stage = std::numeric_limits<double>::quiet_NaN();
    int cuts_added = 0;
    int candidate_incumbent_checks = 0;
    int subproblem_solves = 0;
    double callback_time_seconds = 0.0;
    double subproblem_time_seconds = 0.0;
    double max_cut_violation = 0.0;
    int activation_cuts_used = 0;
    int activation_nonzero_inactive_coefficients = 0;
    int cut_pool_size_before_stage = 0;
    int cuts_reused_at_stage = 0;
    int new_cuts_added_to_pool = 0;
    int duplicate_cuts_skipped = 0;
    std::vector<int> selected_firebreaks;
    int active_count_before_maintenance = 0;
    int active_count_after_activation = 0;
    int active_count_after_deactivation = 0;
    std::vector<int> deactivated_candidates;
    int protected_selected_count = 0;
    int protected_min_age_count = 0;
    int protected_cooldown_count = 0;
    int protected_newly_activated_count = 0;
    int protected_tail_count = 0;
    int deactivation_candidate_count = 0;
    int reactivation_blocked_by_cooldown_count = 0;
    int oscillation_event_count = 0;
    std::vector<int> selected_candidates_protected;
    std::vector<int> tail_protected_candidates;
    std::string candidate_score_mode = "generic";
    double candidate_tail_score_gamma = 0.5;
    int candidate_tail_protection_size = 0;
    std::vector<std::pair<int, double>> top_blend_candidates;
    std::vector<std::pair<int, double>> top_generic_candidates_for_score_mode;
    std::vector<std::pair<int, double>> top_tail_candidates;
    int top_blend_tail_overlap = 0;
    int top_blend_generic_overlap = 0;
    int activated_tail_top_k_overlap = 0;
    int deactivated_tail_top_k_warning_count = 0;
};

struct FppRestrictedCandidateBranchBendersResult {
    bool restricted_candidate_enabled = true;
    bool restricted_candidate_exact_mode = true;
    bool full_activation_performed = false;
    bool eventually_activated_all = false;
    bool restricted_lower_bound_is_global = false;
    bool final_lower_bound_is_global = false;
    bool full_space_cuts_generated = true;
    bool cuts_reused_across_stages = true;
    bool cut_reuse_postponed = false;
    bool cut_reuse_enabled = true;
    bool heuristic_mode_enabled = false;
    bool stopped_before_full_activation = false;
    bool global_optimality_certified = false;
    bool global_time_budget_enabled = false;
    bool time_budget_exhausted = false;
    std::string reason_for_heuristic_stop;
    double global_time_limit_seconds = 0.0;
    double elapsed_time_total_seconds = 0.0;
    double restricted_initial_stage_runtime = 0.0;
    double restricted_activation_stage_runtime_total = 0.0;
    double restricted_final_full_stage_runtime = 0.0;
    double restricted_final_stage_time_limit = 0.0;

    int initial_active_candidate_count = 0;
    int active_candidate_count_after_restricted_stage = 0;
    int active_candidate_count_final = 0;
    double active_candidate_fraction_final = 0.0;
    int candidate_rounds = 0;

    std::string status;
    std::string risk_measure = "expected";
    double cvar_beta = 0.9;
    double cvar_lambda = 1.0;
    std::string restricted_stage_status;
    std::string final_stage_status;
    double restricted_stage_objective = std::numeric_limits<double>::quiet_NaN();
    double final_full_objective = std::numeric_limits<double>::quiet_NaN();
    double restricted_objective = std::numeric_limits<double>::quiet_NaN();
    double restricted_best_bound = std::numeric_limits<double>::quiet_NaN();
    bool restricted_bound_is_global = false;
    double active_candidate_fraction_at_stop = 0.0;
    int restricted_stage_lazy_cuts = 0;
    int final_stage_lazy_cuts = 0;
    std::string initial_candidate_policy;
    std::string activation_policy;
    std::string candidate_maintenance_policy = "none";
    bool maintenance_weighted = false;
    std::string maintenance_map_hash;
    bool deactivation_enabled = false;
    int deactivation_rounds = 0;
    int active_candidate_target = 0;
    int candidates_considered_for_deactivation = 0;
    int candidates_deactivated = 0;
    int candidates_reactivated = 0;
    int candidates_protected_from_deactivation = 0;
    bool full_activation_overrode_maintenance = false;
    int candidate_min_active_size = 0;
    int candidate_max_active_size = 0;
    int candidate_deactivation_batch_size = 0;
    int candidate_deactivation_min_age = 1;
    int candidate_reactivation_cooldown_rounds = 1;
    bool protect_selected_candidates = true;
    std::string candidate_score_mode = "generic";
    double candidate_tail_score_gamma = 0.5;
    int candidate_tail_protection_size = 0;
    std::string candidate_scorer = "none";
    bool candidate_scorer_weighted = false;
    std::string candidate_score_map_hash;
    std::vector<int> initial_candidate_ids;
    std::vector<std::pair<int, double>> initial_candidate_scores;
    int score_recomputations = 0;
    std::vector<int> candidates_activated_by_score;
    std::vector<int> candidates_activated_by_full_fallback;
    std::vector<std::vector<int>> tail_protected_candidates_by_round;
    std::vector<int> tail_protected_count_by_round;
    int protected_tail_count = 0;
    int deactivation_blocked_by_tail_protection_count = 0;
    int activated_by_tail_blend_count = 0;
    int activated_tail_top_k_overlap = 0;
    int deactivated_tail_top_k_warning_count = 0;
    std::vector<int> activated_count_by_round;
    std::vector<int> deactivated_count_by_round;
    std::vector<std::vector<int>> activated_candidates_by_round;
    std::vector<std::vector<int>> deactivated_candidates_by_round;
    std::vector<std::vector<int>> selected_candidates_protected_by_round;
    int protected_selected_count = 0;
    int protected_min_age_count = 0;
    int protected_cooldown_count = 0;
    int protected_newly_activated_count = 0;
    int deactivation_candidate_count = 0;
    int reactivation_blocked_by_cooldown_count = 0;
    int oscillation_event_count = 0;
    int max_candidate_state_changes = 0;
    double average_candidate_state_changes = 0.0;
    bool burn_frequency_score_available = false;
    std::vector<int> initial_candidates_from_burn_frequency;
    int activation_batch_size = 0;
    std::vector<int> candidates_activated_by_burn_frequency;
    std::vector<std::pair<int, double>> top_burn_frequency_candidates;
    bool benders_coefficient_scores_available = false;
    std::vector<std::pair<int, double>> top_benders_coefficient_candidates;
    std::vector<int> candidates_activated_by_benders_coefficients;
    int number_of_cuts_used_for_activation = 0;
    int number_of_nonzero_inactive_coefficients = 0;
    double max_benders_coefficient_score = std::numeric_limits<double>::quiet_NaN();
    double avg_benders_coefficient_score = std::numeric_limits<double>::quiet_NaN();
    std::vector<BendersCut> accumulated_benders_cuts;
    int cut_pool_size = 0;
    int cut_pool_peak_size = 0;
    int cut_pool_evictions = 0;
    int cut_pool_reinstantiations = 0;
    int cuts_reused_in_full_stage = 0;
    int restricted_stage_cuts_reused = 0;
    int duplicate_cuts_skipped = 0;

    bool persistent_subproblems_enabled = false;
    int subproblem_model_build_count = 0;
    int subproblem_fixed_y_update_count = 0;
    int subproblem_solve_count = 0;
    int subproblem_model_rebuild_count = 0;
    double subproblem_total_build_time = 0.0;
    double subproblem_total_update_time = 0.0;
    double subproblem_total_solve_time = 0.0;
    double subproblem_average_update_time = 0.0;
    double subproblem_average_solve_time = 0.0;

    bool persistent_master_enabled = false;
    int master_model_build_count = 0;
    int master_model_rebuild_count = 0;
    int master_bound_update_count = 0;
    int master_cut_insertions = 0;
    int master_duplicate_cut_insertions_skipped = 0;
    double master_total_build_time = 0.0;
    double master_total_bound_update_time = 0.0;
    double master_total_cut_insertion_time = 0.0;
    std::string persistent_master_note;

    std::vector<std::pair<int, int>> cuts_by_round;
    std::vector<std::pair<int, int>> cuts_by_scenario;
    RestrictedCandidateCutPool cut_pool;
    bool tail_score_diagnostics_enabled = false;
    std::vector<CvarTailScoreRoundDiagnostics> tail_score_diagnostics;

    solver::ModelResult restricted_stage_result;
    solver::ModelResult last_restricted_stage_result;
    solver::ModelResult final_stage_result;
    std::vector<FppRestrictedCandidateRoundLog> round_log;
    std::vector<CandidateActivationRound> activation_history;
    std::vector<std::string> notes;
};

struct FppRestrictedCandidateBranchBendersMasterStructure {
    std::size_t y_variable_count = 0;
    std::size_t eta_variable_count = 0;
    std::size_t risk_threshold_variable_count = 0;
    std::size_t cvar_excess_variable_count = 0;
    std::size_t total_variable_count = 0;
    std::size_t budget_constraint_count = 0;
    std::size_t base_constraint_count = 0;
    std::size_t risk_constraint_count = 0;
    bool has_scenario_recourse_variables = false;
};

FppRestrictedCandidateBranchBendersMasterStructure
analyze_fpp_restricted_candidate_branch_benders_master_structure(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config = risk::RiskMeasureConfig());

class FppRestrictedCandidateBranchBendersSolver {
public:
    FppRestrictedCandidateBranchBendersResult solve(
        const opt::OptimizationInstance& opt,
        const FppRestrictedCandidateBranchBendersOptions& options) const;
};

}  // namespace firebreak::benders
