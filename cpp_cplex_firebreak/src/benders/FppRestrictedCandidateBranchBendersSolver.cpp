#include "benders/FppRestrictedCandidateBranchBendersSolver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "benders/BurnFrequencyCandidateScorer.hpp"
#include "benders/BendersCoefficientCandidateScorer.hpp"
#include "benders/CandidateBoundController.hpp"
#include "benders/CvarTailAwareBendersCandidateScorer.hpp"
#include "benders/FppLiftedLowerBound.hpp"
#include "benders/FppPersistentScenarioSubproblemManager.hpp"
#include "benders/RestrictedCandidateMaintenanceTracker.hpp"
#include "benders/RestrictedCandidateCutPool.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::benders {

namespace {

void validate_options(const FppRestrictedCandidateBranchBendersOptions& options) {
    if (options.tolerance < 0.0) {
        throw std::runtime_error("FPP restricted Branch-Benders tolerance must be nonnegative.");
    }
    if (options.time_limit_seconds < 0.0) {
        throw std::runtime_error("FPP restricted Branch-Benders time_limit_seconds must be nonnegative.");
    }
    if (options.mip_gap < -1.0) {
        throw std::runtime_error("FPP restricted Branch-Benders mip_gap must be nonnegative, or omitted.");
    }
    if (options.threads < 0) {
        throw std::runtime_error("FPP restricted Branch-Benders threads must be nonnegative.");
    }
    if (options.combinatorial_options.enabled) {
        validate_fpp_combinatorial_benders_options(options.combinatorial_options);
    }
    if (options.root_user_cut_max_rounds <= 0) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders root_user_cut_max_rounds must be positive.");
    }
    if (!std::isnan(options.root_user_cut_tolerance) &&
        options.root_user_cut_tolerance < 0.0) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders root_user_cut_tolerance must be nonnegative.");
    }
    risk::RiskMeasureConfig effective_risk_config = options.risk_config;
    if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
        effective_risk_config.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective_risk_config);
    if (options.initial_candidate_policy != "explicit-list" &&
        options.initial_candidate_policy != "burn-frequency") {
        throw std::runtime_error(
            "FPP restricted Branch-Benders initial_candidate_policy must be explicit-list or burn-frequency.");
    }
    if (options.initial_candidate_policy == "burn-frequency" &&
        options.initial_candidate_size < 0) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders burn-frequency initialization requires initial_candidate_size.");
    }
    if (options.activation_policy != "none" &&
        options.activation_policy != "burn-frequency" &&
        options.activation_policy != "benders-coefficients" &&
        options.activation_policy != "activate-all-final") {
        throw std::runtime_error(
            "FPP restricted Branch-Benders activation_policy must be none, burn-frequency, benders-coefficients, or activate-all-final.");
    }
    if (options.activation_batch_size < 0) {
        throw std::runtime_error("FPP restricted Branch-Benders activation_batch_size must be nonnegative.");
    }
    if (options.max_candidate_rounds < 0) {
        throw std::runtime_error("FPP restricted Branch-Benders max_candidate_rounds must be nonnegative.");
    }
    if (options.candidate_maintenance_policy != "none" &&
        options.candidate_maintenance_policy != "benders-coefficients") {
        throw std::runtime_error(
            "FPP restricted Branch-Benders candidate_maintenance_policy must be none or benders-coefficients.");
    }
    if (options.candidate_deactivation_batch_size < -1 ||
        options.candidate_min_active_size < -1 ||
        options.candidate_max_active_size < -1) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders maintenance sizes must be nonnegative, or omitted.");
    }
    if (options.candidate_deactivation_min_age < 0 ||
        options.candidate_reactivation_cooldown_rounds < 0) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders maintenance age and cooldown values must be nonnegative.");
    }
    if (options.stop_after_candidate_rounds < -1) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders stop_after_candidate_rounds must be nonnegative, or omitted.");
    }
    if (options.candidate_score_mode != "generic" &&
        options.candidate_score_mode != "cvar-tail-blend") {
        throw std::runtime_error(
            "FPP restricted Branch-Benders candidate_score_mode must be generic or cvar-tail-blend.");
    }
    if (!std::isfinite(options.candidate_tail_score_gamma) ||
        options.candidate_tail_score_gamma < 0.0 ||
        options.candidate_tail_score_gamma > 1.0) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders candidate_tail_score_gamma must satisfy 0.0 <= gamma <= 1.0.");
    }
    if (options.candidate_tail_protection_size < -1) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders candidate_tail_protection_size must be nonnegative, or omitted.");
    }
    if (options.restricted_heuristic_mode && options.eventually_activate_all) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders heuristic mode cannot be combined with eventual full activation.");
    }
    if (options.activation_policy == "burn-frequency" &&
        options.max_candidate_rounds > 0 &&
        options.activation_batch_size <= 0) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders burn-frequency activation requires activation_batch_size > 0.");
    }
    if (options.activation_policy == "benders-coefficients" &&
        options.max_candidate_rounds > 0 &&
        options.activation_batch_size <= 0) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders Benders-coefficient activation requires activation_batch_size > 0.");
    }
    if (options.candidate_maintenance_policy != "none") {
        if (!options.restricted_heuristic_mode || options.eventually_activate_all) {
            throw std::runtime_error(
                "FPP restricted Branch-Benders active-set maintenance is heuristic-mode only; use --restricted-heuristic-mode.");
        }
        if (options.activation_policy != "benders-coefficients") {
            throw std::runtime_error(
                "FPP restricted Branch-Benders Benders-coefficient maintenance requires candidate_activation_policy=benders-coefficients.");
        }
    }
    if (options.candidate_score_mode == "cvar-tail-blend") {
        if (!options.solve_restricted_stage) {
            throw std::runtime_error(
                "CVaR tail-aware candidate scoring requires an initial restricted stage.");
        }
        if (!options.restricted_heuristic_mode || options.eventually_activate_all) {
            throw std::runtime_error(
                "CVaR tail-aware candidate scoring is supported only in restricted heuristic mode.");
        }
        if (effective_risk_config.type != risk::RiskMeasureType::CVaR) {
            throw std::runtime_error(
                "CVaR tail-aware candidate scoring requires risk_measure=cvar.");
        }
        if (options.activation_policy != "benders-coefficients" ||
            options.candidate_maintenance_policy != "benders-coefficients") {
            throw std::runtime_error(
                "CVaR tail-aware candidate scoring requires Benders-coefficient activation and maintenance policies.");
        }
    }
}

void validate_instance(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("FPP restricted Branch-Benders requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("FPP restricted Branch-Benders requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("FPP restricted Branch-Benders requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0 || opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders budget must be between zero and the eligible-node count.");
    }
}

bool final_result_is_globally_optimal(
    const solver::ModelResult& result,
    double tolerance) {
    return result.status == "Optimal" && result.max_cut_violation <= tolerance;
}

bool status_is_time_limit(const std::string& status) {
    return status.find("Time") != std::string::npos ||
           status.find("time") != std::string::npos;
}

double elapsed_seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

double remaining_global_time(
    const FppRestrictedCandidateBranchBendersOptions& options,
    std::chrono::steady_clock::time_point global_start) {
    if (options.time_limit_seconds <= 0.0) {
        return 0.0;
    }
    return std::max(0.0, options.time_limit_seconds - elapsed_seconds_since(global_start));
}

bool global_time_budget_exhausted(
    const FppRestrictedCandidateBranchBendersOptions& options,
    std::chrono::steady_clock::time_point global_start) {
    return options.time_limit_seconds > 0.0 &&
           remaining_global_time(options, global_start) <= 1.0e-6;
}

FppRestrictedCandidateBranchBendersOptions options_for_stage_time_limit(
    const FppRestrictedCandidateBranchBendersOptions& options,
    double remaining_time_seconds) {
    FppRestrictedCandidateBranchBendersOptions stage_options = options;
    if (options.time_limit_seconds > 0.0) {
        stage_options.time_limit_seconds = std::max(0.0, remaining_time_seconds);
    }
    return stage_options;
}

void normalize_stage_time_limit_status(
    solver::ModelResult& stage_result,
    double stage_time_limit_seconds) {
    if (stage_time_limit_seconds <= 0.0 ||
        stage_result.status == "Optimal" ||
        status_is_time_limit(stage_result.status)) {
        return;
    }
    if (stage_result.runtime_seconds >= 0.99 * stage_time_limit_seconds) {
        stage_result.status = "TimeLimit";
        stage_result.notes.push_back(
            "Stage reached the assigned restricted-candidate time budget.");
    }
}

bool uses_cvar_risk(const risk::RiskMeasureConfig& config) {
    return config.type == risk::RiskMeasureType::CVaR ||
           config.type == risk::RiskMeasureType::MeanCVaR;
}

risk::RiskMeasureConfig effective_risk_config_from(const risk::RiskMeasureConfig& config) {
    risk::RiskMeasureConfig effective = config;
    if (effective.type == risk::RiskMeasureType::CVaR) {
        effective.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective);
    return effective;
}

int effective_candidate_round_limit(
    const FppRestrictedCandidateBranchBendersOptions& options) {
    if (!options.restricted_heuristic_mode ||
        options.stop_after_candidate_rounds < 0) {
        return options.max_candidate_rounds;
    }
    if (options.max_candidate_rounds == 0) {
        return options.stop_after_candidate_rounds;
    }
    return std::min(options.max_candidate_rounds, options.stop_after_candidate_rounds);
}

RestrictedCandidateMaintenanceOptions effective_maintenance_options(
    const FppRestrictedCandidateBranchBendersOptions& options,
    const RestrictedCandidateManager& manager) {
    RestrictedCandidateMaintenanceOptions maintenance;
    maintenance.min_active_size = options.candidate_min_active_size >= 0
        ? options.candidate_min_active_size
        : manager.activeCount();
    maintenance.min_active_size = std::max(maintenance.min_active_size, manager.budget());
    maintenance.deactivation_batch_size = options.candidate_deactivation_batch_size >= 0
        ? options.candidate_deactivation_batch_size
        : options.activation_batch_size;
    maintenance.max_active_size = options.candidate_max_active_size >= 0
        ? options.candidate_max_active_size
        : maintenance.min_active_size + std::max(0, options.activation_batch_size);
    maintenance.deactivation_min_age = options.candidate_deactivation_min_age;
    maintenance.reactivation_cooldown_rounds = options.candidate_reactivation_cooldown_rounds;
    maintenance.protect_selected_candidates = options.protect_selected_candidates;

    if (maintenance.min_active_size < manager.budget()) {
        throw std::runtime_error("candidate_min_active_size must be at least the firebreak budget.");
    }
    if (maintenance.min_active_size > manager.candidateCount()) {
        throw std::runtime_error("candidate_min_active_size must not exceed the candidate count.");
    }
    if (maintenance.max_active_size > manager.candidateCount()) {
        maintenance.max_active_size = manager.candidateCount();
    }
    if (maintenance.max_active_size < maintenance.min_active_size) {
        throw std::runtime_error("candidate_max_active_size must be at least candidate_min_active_size.");
    }
    if (maintenance.deactivation_batch_size < 0) {
        throw std::runtime_error("candidate_deactivation_batch_size must be nonnegative.");
    }
    return maintenance;
}

bool uses_tail_aware_candidate_scoring(
    const FppRestrictedCandidateBranchBendersOptions& options) {
    return options.candidate_score_mode == "cvar-tail-blend";
}

int effective_tail_protection_size(
    const FppRestrictedCandidateBranchBendersOptions& options) {
    return options.candidate_tail_protection_size >= 0
        ? options.candidate_tail_protection_size
        : options.activation_batch_size;
}

std::vector<int> candidate_ids_from_scores(
    const std::vector<std::pair<int, double>>& scores) {
    std::vector<int> ids;
    ids.reserve(scores.size());
    for (const auto& [candidate, score] : scores) {
        (void)score;
        ids.push_back(candidate);
    }
    return ids;
}

int overlap_count(
    const std::vector<int>& candidates,
    const std::vector<int>& ranked_candidates) {
    std::set<int> candidate_set(candidates.begin(), candidates.end());
    int overlap = 0;
    for (const int candidate : ranked_candidates) {
        if (candidate_set.count(candidate) > 0) {
            ++overlap;
        }
    }
    return overlap;
}

std::vector<TailScoreCandidateRank> tail_rank_array_from_scores(
    const std::vector<std::pair<int, double>>& scores,
    int limit) {
    const auto top_scores = topCvarTailAwareCandidates(scores, limit);
    std::vector<TailScoreCandidateRank> ranks;
    ranks.reserve(top_scores.size());
    for (std::size_t i = 0; i < top_scores.size(); ++i) {
        ranks.push_back({
            top_scores[i].first,
            top_scores[i].second,
            static_cast<int>(i + 1),
        });
    }
    return ranks;
}

std::map<int, CvarTailAwareBendersCandidateScore> tail_aware_score_map(
    const CvarTailAwareBendersScoringSummary& summary) {
    std::map<int, CvarTailAwareBendersCandidateScore> out;
    for (const auto& score : summary.detailed_scores) {
        out[score.candidate] = score;
    }
    return out;
}

struct RestrictedStageSolveResult {
    solver::ModelResult model_result;
    std::vector<BendersCut> generated_cuts;
    std::vector<std::pair<int, double>> scenario_losses_by_id;
    std::vector<std::pair<int, double>> cvar_excess_by_id;
    int preloaded_cuts_added = 0;
    double master_model_build_time = 0.0;
    double master_bound_update_time = 0.0;
    double master_cut_insertion_time = 0.0;
};

FppRestrictedCandidateRoundLog make_round_log(
    int round_index,
    const std::string& stage_type,
    const std::string& risk_measure,
    int active_candidate_count,
    int candidate_count,
    const std::vector<int>& newly_activated,
    const std::string& activation_policy,
    const std::vector<std::pair<int, double>>& top_scores_considered,
    const solver::ModelResult& stage_result,
    double time_limit_seconds,
    double remaining_global_time_before_stage,
    double remaining_global_time_after_stage,
    int cut_pool_size_before_stage,
    int new_cuts_added_to_pool,
    int duplicate_cuts_skipped,
    int activation_cuts_used = 0,
    int activation_nonzero_inactive_coefficients = 0) {
    FppRestrictedCandidateRoundLog log;
    log.round_index = round_index;
    log.stage_type = stage_type;
    log.risk_measure = risk_measure;
    log.active_candidate_count = active_candidate_count;
    log.active_candidate_fraction =
        static_cast<double>(active_candidate_count) / static_cast<double>(candidate_count);
    log.newly_activated_candidates = newly_activated;
    log.activation_policy = activation_policy;
    log.top_scores_considered = top_scores_considered;
    log.solve_status = stage_result.status;
    log.objective = stage_result.objective_value;
    log.best_bound = stage_result.best_bound;
    log.mip_gap = stage_result.mip_gap;
    log.runtime_seconds = stage_result.runtime_seconds;
    log.time_limit_seconds = time_limit_seconds;
    log.remaining_global_time_before_stage = remaining_global_time_before_stage;
    log.remaining_global_time_after_stage = remaining_global_time_after_stage;
    log.cuts_added = stage_result.branch_benders_lazy_cuts_added;
    log.candidate_incumbent_checks = stage_result.branch_benders_candidate_incumbents_checked;
    log.subproblem_solves = stage_result.branch_benders_subproblems_solved;
    log.callback_time_seconds = stage_result.branch_benders_callback_time_sec;
    log.subproblem_time_seconds = stage_result.branch_benders_subproblem_time_sec;
    log.max_cut_violation = stage_result.branch_benders_max_cut_violation;
    log.activation_cuts_used = activation_cuts_used;
    log.activation_nonzero_inactive_coefficients = activation_nonzero_inactive_coefficients;
    log.cut_pool_size_before_stage = cut_pool_size_before_stage;
    log.cuts_reused_at_stage = stage_result.benders_cuts_added;
    log.new_cuts_added_to_pool = new_cuts_added_to_pool;
    log.duplicate_cuts_skipped = duplicate_cuts_skipped;
    log.selected_firebreaks = stage_result.selected_firebreak_indices;
    return log;
}

void attach_maintenance_decision(
    FppRestrictedCandidateRoundLog& log,
    const RestrictedCandidateMaintenanceDecision& decision) {
    log.active_count_before_maintenance = decision.active_count_before_maintenance;
    log.active_count_after_activation = decision.active_count_after_activation;
    log.active_count_after_deactivation = decision.active_count_after_deactivation;
    log.deactivated_candidates = decision.deactivated;
    log.protected_selected_count = decision.protected_selected_count;
    log.protected_min_age_count = decision.protected_min_age_count;
    log.protected_cooldown_count = decision.protected_cooldown_count;
    log.protected_newly_activated_count = decision.protected_newly_activated_count;
    log.protected_tail_count = decision.protected_tail_count;
    log.deactivation_candidate_count = decision.deactivation_candidate_count;
    log.reactivation_blocked_by_cooldown_count =
        decision.reactivation_blocked_by_cooldown_count;
    log.oscillation_event_count = decision.oscillation_event_count;
    log.selected_candidates_protected = decision.selected_candidates_protected;
    log.tail_protected_candidates = decision.tail_protected_candidates;
}

std::vector<int> initial_candidates_from_options(
    const opt::OptimizationInstance& opt,
    const FppRestrictedCandidateBranchBendersOptions& options,
    std::vector<std::pair<int, double>>& burn_frequency_scores,
    bool& burn_frequency_score_available) {
    if (options.initial_candidate_policy == "explicit-list") {
        return options.initial_active_candidates;
    }

    BurnFrequencyCandidateScorer scorer;
    burn_frequency_scores = scorer.scoreCandidates(opt);
    burn_frequency_score_available = true;
    return makeInitialActiveSetFromScores(
        static_cast<int>(opt.eligible_indices.size()),
        opt.budget,
        burn_frequency_scores,
        options.initial_candidate_size);
}

std::vector<std::pair<int, double>> top_inactive_scores(
    const RestrictedCandidateManager& manager,
    const std::vector<std::pair<int, double>>& scores,
    int limit) {
    std::vector<std::pair<int, double>> inactive_scores;
    inactive_scores.reserve(scores.size());
    for (const auto& [candidate, score] : scores) {
        if (candidate >= 0 &&
            candidate < manager.candidateCount() &&
            manager.isInactive(candidate)) {
            inactive_scores.push_back({candidate, score});
        }
    }
    return topBurnFrequencyCandidates(inactive_scores, limit);
}

std::map<int, double> scenario_probability_by_id(const opt::OptimizationInstance& opt) {
    std::map<int, double> probability_by_id;
    for (const auto& scenario : opt.scenarios) {
        probability_by_id[scenario.scenario_id] = scenario.probability;
    }
    return probability_by_id;
}

std::vector<std::pair<int, int>> vector_from_count_map(const std::map<int, int>& counts) {
    std::vector<std::pair<int, int>> values;
    values.reserve(counts.size());
    for (const auto& item : counts) {
        values.push_back(item);
    }
    return values;
}

void update_cut_pool_diagnostics(
    FppRestrictedCandidateBranchBendersResult& result,
    const RestrictedCandidateCutPool& cut_pool) {
    result.cut_pool = cut_pool;
    result.cut_pool_size = cut_pool.size();
    result.duplicate_cuts_skipped = cut_pool.duplicateCutsSkipped();
    result.cuts_by_round = vector_from_count_map(cut_pool.cutsByRound());
    result.cuts_by_scenario = vector_from_count_map(cut_pool.cutsByScenario());
    result.accumulated_benders_cuts = cut_pool.cuts();
}

void update_subproblem_diagnostics(
    FppRestrictedCandidateBranchBendersResult& result,
    const FppPersistentScenarioSubproblemManager& subproblem_manager) {
    const auto diagnostics = subproblem_manager.diagnostics();
    result.persistent_subproblems_enabled = diagnostics.persistent_subproblems_enabled;
    result.subproblem_model_build_count = diagnostics.subproblem_model_build_count;
    result.subproblem_fixed_y_update_count =
        diagnostics.subproblem_fixed_y_update_count;
    result.subproblem_solve_count = diagnostics.subproblem_solve_count;
    result.subproblem_model_rebuild_count = diagnostics.subproblem_model_rebuild_count;
    result.subproblem_total_build_time = diagnostics.subproblem_total_build_time;
    result.subproblem_total_update_time = diagnostics.subproblem_total_update_time;
    result.subproblem_total_solve_time = diagnostics.subproblem_total_solve_time;
    result.subproblem_average_update_time =
        diagnostics.subproblem_average_update_time;
    result.subproblem_average_solve_time =
        diagnostics.subproblem_average_solve_time;
}

void record_maintenance_diagnostics(
    FppRestrictedCandidateBranchBendersResult& result,
    const RestrictedCandidateMaintenanceDecision& decision,
    const RestrictedCandidateMaintenanceTracker& tracker) {
    ++result.deactivation_rounds;
    result.activated_count_by_round.push_back(static_cast<int>(decision.activated.size()));
    result.deactivated_count_by_round.push_back(static_cast<int>(decision.deactivated.size()));
    result.activated_candidates_by_round.push_back(decision.activated);
    result.deactivated_candidates_by_round.push_back(decision.deactivated);
    result.selected_candidates_protected_by_round.push_back(
        decision.selected_candidates_protected);
    result.protected_selected_count += decision.protected_selected_count;
    result.protected_min_age_count += decision.protected_min_age_count;
    result.protected_cooldown_count += decision.protected_cooldown_count;
    result.protected_newly_activated_count +=
        decision.protected_newly_activated_count;
    result.protected_tail_count += decision.protected_tail_count;
    result.deactivation_blocked_by_tail_protection_count +=
        decision.protected_tail_count;
    result.tail_protected_candidates_by_round.push_back(
        decision.tail_protected_candidates);
    result.tail_protected_count_by_round.push_back(
        decision.protected_tail_count);
    result.deactivation_candidate_count += decision.deactivation_candidate_count;
    result.reactivation_blocked_by_cooldown_count +=
        decision.reactivation_blocked_by_cooldown_count;
    result.oscillation_event_count = tracker.totalOscillationEvents();
    result.max_candidate_state_changes = tracker.maxStateChangeCount();
    result.average_candidate_state_changes = tracker.averageStateChangeCount();
}

std::vector<int> candidate_ids_from_selected_compact_indices(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& selected_compact_indices) {
    std::map<int, int> candidate_by_compact;
    for (std::size_t candidate = 0; candidate < opt.eligible_indices.size(); ++candidate) {
        candidate_by_compact[opt.eligible_indices[candidate]] = static_cast<int>(candidate);
    }
    std::vector<int> selected_candidates;
    selected_candidates.reserve(selected_compact_indices.size());
    for (const int compact_index : selected_compact_indices) {
        const auto it = candidate_by_compact.find(compact_index);
        if (it != candidate_by_compact.end()) {
            selected_candidates.push_back(it->second);
        }
    }
    std::sort(selected_candidates.begin(), selected_candidates.end());
    selected_candidates.erase(
        std::unique(selected_candidates.begin(), selected_candidates.end()),
        selected_candidates.end());
    return selected_candidates;
}

void append_tail_score_diagnostics_if_enabled(
    FppRestrictedCandidateBranchBendersResult& result,
    const opt::OptimizationInstance& opt,
    const FppRestrictedCandidateBranchBendersOptions& options,
    const RestrictedCandidateCutPool& cut_pool,
    const RestrictedStageSolveResult& stage,
    const FppRestrictedCandidateRoundLog& log,
    const std::vector<int>& active_candidates_before_round,
    const std::vector<int>& active_candidates_after_round) {
    if (!options.export_tail_score_diagnostics &&
        !uses_tail_aware_candidate_scoring(options)) {
        return;
    }

    CvarTailScoreDiagnosticsInput input;
    input.round_index = log.round_index;
    input.risk_measure = log.risk_measure;
    input.cvar_beta = result.cvar_beta;
    input.risk_threshold = stage.model_result.risk_threshold_value;
    input.candidate_count = static_cast<int>(opt.eligible_indices.size());
    input.eligible_compact_indices = opt.eligible_indices;
    input.scenario_probability_by_id = scenario_probability_by_id(opt);
    input.accumulated_cuts = cut_pool.cuts();
    input.recent_cuts = stage.generated_cuts;
    input.scenario_losses_by_id = stage.scenario_losses_by_id;
    input.cvar_excess_by_id = stage.cvar_excess_by_id;
    input.active_candidates_before_round = active_candidates_before_round;
    input.active_candidates_after_round = active_candidates_after_round;
    input.activated_candidates = log.newly_activated_candidates;
    input.deactivated_candidates = log.deactivated_candidates;
    input.selected_candidates = candidate_ids_from_selected_compact_indices(
        opt,
        stage.model_result.selected_firebreak_indices);
    input.protected_selected_candidates = log.selected_candidates_protected;
    input.top_k = std::max(10, options.activation_batch_size);
    input.tolerance = options.tolerance;

    auto diagnostics = computeCvarTailScoreRoundDiagnostics(input);
    diagnostics.deactivation_blocked_by_tail_protection_count =
        log.protected_tail_count;
    if (uses_tail_aware_candidate_scoring(options)) {
        CvarTailAwareBendersCandidateScorer scorer;
        std::vector<int> all_candidates(static_cast<std::size_t>(input.candidate_count));
        for (int candidate = 0; candidate < input.candidate_count; ++candidate) {
            all_candidates[static_cast<std::size_t>(candidate)] = candidate;
        }
        const auto score_summary = scorer.scoreCandidates(
            input.candidate_count,
            input.eligible_compact_indices,
            all_candidates,
            input.accumulated_cuts,
            input.scenario_losses_by_id,
            input.cvar_beta,
            options.candidate_tail_score_gamma,
            input.scenario_probability_by_id);
        const int top_k = std::max(1, std::min(input.top_k, input.candidate_count));
        diagnostics.top_tail_blend_candidates =
            tail_rank_array_from_scores(score_summary.blend_scores, top_k);
        diagnostics.top_k_overlap_blend_tail = tailScoreTopKOverlap(
            diagnostics.top_tail_blend_candidates,
            diagnostics.top_tail_empirical_candidates);
        diagnostics.top_k_overlap_blend_generic = tailScoreTopKOverlap(
            diagnostics.top_tail_blend_candidates,
            diagnostics.top_generic_candidates);
        const auto score_by_candidate = tail_aware_score_map(score_summary);
        const std::set<int> tail_protected(
            log.tail_protected_candidates.begin(),
            log.tail_protected_candidates.end());
        for (auto& event : diagnostics.candidate_events) {
            const auto it = score_by_candidate.find(event.candidate_id);
            if (it == score_by_candidate.end()) {
                continue;
            }
            event.tail_blend_score = it->second.blend_score;
            event.rank_tail_blend = it->second.blend_rank;
            event.was_tail_protected =
                tail_protected.count(event.candidate_id) > 0;
        }
        diagnostics.notes.push_back(
            "CVaR-tail-aware blended score diagnostics were computed for Phase 1S; generic solver mathematics and Benders cuts are unchanged.");
    }
    if (result.risk_measure == "expected") {
        diagnostics.notes.push_back(
            "Risk measure is expected value; CVaR-tail diagnostics are informational only.");
    }
    result.tail_score_diagnostics.push_back(std::move(diagnostics));
}

#ifdef FIREBREAK_WITH_CPLEX

double effective_root_user_cut_tolerance(
    const FppRestrictedCandidateBranchBendersOptions& options) {
    if (std::isnan(options.root_user_cut_tolerance)) {
        return options.tolerance;
    }
    return options.root_user_cut_tolerance;
}

struct RiskObjectiveEvaluation {
    double objective = 0.0;
    double expected = 0.0;
    double cvar = std::numeric_limits<double>::quiet_NaN();
    double risk_threshold = std::numeric_limits<double>::quiet_NaN();
};

RiskObjectiveEvaluation evaluate_risk_objective(
    const opt::OptimizationInstance& opt,
    const std::vector<double>& scenario_losses,
    const risk::RiskMeasureConfig& config) {
    if (scenario_losses.size() != opt.scenarios.size()) {
        throw std::runtime_error(
            "FPP restricted Branch-Benders risk evaluation received the wrong loss vector size.");
    }

    RiskObjectiveEvaluation evaluation;
    std::vector<risk::WeightedLoss> weighted_losses;
    weighted_losses.reserve(scenario_losses.size());
    for (std::size_t s = 0; s < scenario_losses.size(); ++s) {
        const double probability = opt.scenarios[s].probability;
        evaluation.expected += probability * scenario_losses[s];
        weighted_losses.push_back({
            opt.scenarios[s].scenario_id,
            scenario_losses[s],
            probability,
        });
    }

    if (config.type == risk::RiskMeasureType::Expected) {
        evaluation.objective = evaluation.expected;
        return evaluation;
    }

    const auto metrics = risk::compute_weighted_risk_metrics(weighted_losses, config.cvarBeta);
    evaluation.expected = metrics.expected;
    evaluation.cvar = metrics.cvar;
    evaluation.risk_threshold = metrics.var;
    if (config.type == risk::RiskMeasureType::CVaR) {
        evaluation.objective = metrics.cvar;
    } else {
        evaluation.objective =
            (1.0 - config.cvarLambda) * metrics.expected +
            config.cvarLambda * metrics.cvar;
    }
    return evaluation;
}

std::vector<double> expand_y_to_compact_values(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& y_values_by_eligible_position) {
    std::vector<double> compact_values(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < y_values_by_eligible_position.size(); ++pos) {
        compact_values[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            static_cast<double>(y_values_by_eligible_position[pos]);
    }
    return compact_values;
}

std::vector<double> expand_y_to_compact_values(
    const opt::OptimizationInstance& opt,
    const std::vector<double>& y_values_by_eligible_position) {
    std::vector<double> compact_values(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < y_values_by_eligible_position.size(); ++pos) {
        compact_values[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            y_values_by_eligible_position[pos];
    }
    return compact_values;
}

std::vector<int> selected_compact_indices(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& y_values_by_eligible_position) {
    std::vector<int> selected;
    for (std::size_t pos = 0; pos < y_values_by_eligible_position.size(); ++pos) {
        if (y_values_by_eligible_position[pos] == 1) {
            selected.push_back(opt.eligible_indices[pos]);
        }
    }
    return selected;
}

std::vector<int> selected_original_nodes(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& y_values_by_eligible_position) {
    std::vector<int> selected;
    for (const int compact_index : selected_compact_indices(opt, y_values_by_eligible_position)) {
        selected.push_back(opt.node_mapper.to_node(compact_index));
    }
    return selected;
}

std::map<int, int> scenario_position_by_id(const opt::OptimizationInstance& opt) {
    std::map<int, int> position_by_id;
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        position_by_id[opt.scenarios[s].scenario_id] = static_cast<int>(s);
    }
    return position_by_id;
}

std::string cut_signature(const BendersCut& cut) {
    auto coefficients = cut.coefficients_by_compact_index;
    std::sort(
        coefficients.begin(),
        coefficients.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

    std::ostringstream out;
    out << cut.scenario_id << "|" << std::setprecision(17) << cut.rhs_constant;
    for (const auto& [compact_index, coefficient] : coefficients) {
        if (std::fabs(coefficient) <= 1.0e-12) {
            continue;
        }
        out << "|" << compact_index << ":" << std::setprecision(17) << coefficient;
    }
    return out.str();
}

struct BranchBendersVariableAccess {
    IloBoolVarArray y;
    IloNumVarArray eta;
    std::vector<int> y_position_by_node;
};

struct BranchBendersCallbackStats {
    int callback_calls = 0;
    int candidate_callback_calls = 0;
    int incumbent_callback_calls = 0;
    int candidate_incumbents_checked = 0;
    int subproblems_attempted = 0;
    int subproblems_solved = 0;
    int lazy_cuts_added = 0;
    int violated_cuts = 0;
    int nonviolated_cuts = 0;
    int skipped_cuts = 0;
    int duplicate_cuts = 0;
    double largest_incumbent_cut_violation = 0.0;
    double callback_time_sec = 0.0;
    double subproblem_time_sec = 0.0;
    double max_subproblem_time_sec = 0.0;
    double cut_construction_time_sec = 0.0;
    double lazy_cut_insertion_time_sec = 0.0;
    std::set<std::string> cut_signatures;
    std::vector<BendersCut> generated_cuts;
    std::vector<solver::BranchBendersIncumbentLog> incumbent_log;
    FppCombinatorialBendersStats combinatorial_stats;
    mutable std::mutex mutex;
};

struct BranchBendersRootUserCutStats {
    bool enabled = false;
    int max_rounds = 1;
    double tolerance = 1.0e-6;
    int callback_calls = 0;
    int rounds_executed = 0;
    int cuts_added = 0;
    int scenarios_solved = 0;
    double max_violation = 0.0;
    double total_time_sec = 0.0;
    double subproblem_time_sec = 0.0;
    double max_subproblem_time_sec = 0.0;
    bool only_at_root_confirmed = true;
    std::string skipped_reason;
    std::vector<solver::BranchBendersRootUserCutRoundLog> round_log;
    mutable std::mutex mutex;
};

void add_coverage_llbi_constraints(
    IloEnv& env,
    IloModel& model,
    const FppCoverageLlbiData& data,
    const IloBoolVarArray& y,
    const IloNumVarArray& eta,
    const std::vector<int>& y_position_by_node) {
    if (!data.enabled) {
        return;
    }
    for (const auto& scenario_record : data.scenarios) {
        IloExpr lower_bound_rhs(env);
        lower_bound_rhs += scenario_record.empty_burned_area;
        for (const auto& node_record : scenario_record.nodes) {
            IloNumVar zeta(env, 0.0, 1.0, ILOFLOAT);
            std::ostringstream zeta_name;
            zeta_name << "coverage_zeta_s" << scenario_record.scenario_id
                      << "_" << node_record.compact_node;
            zeta.setName(zeta_name.str().c_str());

            IloExpr cover(env);
            for (const int candidate : node_record.covering_candidate_compact_nodes) {
                if (candidate < 0 ||
                    candidate >= static_cast<int>(y_position_by_node.size()) ||
                    y_position_by_node[static_cast<std::size_t>(candidate)] < 0) {
                    continue;
                }
                cover += y[static_cast<IloInt>(
                    y_position_by_node[static_cast<std::size_t>(candidate)])];
            }
            cover -= zeta;
            model.add(cover >= 0.0);
            cover.end();
            lower_bound_rhs -= zeta;
        }
        if (!scenario_record.nodes.empty()) {
            IloExpr lhs(env);
            lhs += eta[static_cast<IloInt>(scenario_record.scenario_index)];
            lhs -= lower_bound_rhs;
            model.add(lhs >= 0.0);
            lhs.end();
        }
        lower_bound_rhs.end();
    }
}

void add_path_llbi_constraints(
    IloEnv& env,
    IloModel& model,
    const FppPathLlbiData& data,
    const IloBoolVarArray& y,
    const IloNumVarArray& eta,
    const std::vector<int>& y_position_by_node) {
    if (!data.enabled) {
        return;
    }
    for (const auto& scenario_record : data.scenarios) {
        IloExpr eta_lower_bound(env);
        for (const auto& node_record : scenario_record.nodes) {
            IloNumVar burn_lb(env, 0.0, 1.0, ILOFLOAT);
            std::ostringstream b_name;
            b_name << "path_b_s" << scenario_record.scenario_id
                   << "_" << node_record.compact_node;
            burn_lb.setName(b_name.str().c_str());
            eta_lower_bound += burn_lb;
            for (const auto& path : node_record.paths) {
                IloExpr expr(env);
                expr += burn_lb;
                for (const int candidate : path.blocking_candidate_compact_nodes) {
                    if (candidate < 0 ||
                        candidate >= static_cast<int>(y_position_by_node.size()) ||
                        y_position_by_node[static_cast<std::size_t>(candidate)] < 0) {
                        continue;
                    }
                    expr += y[static_cast<IloInt>(
                        y_position_by_node[static_cast<std::size_t>(candidate)])];
                }
                model.add(expr >= 1.0);
                expr.end();
            }
        }
        if (!scenario_record.nodes.empty()) {
            IloExpr lhs(env);
            lhs += eta[static_cast<IloInt>(scenario_record.scenario_index)];
            lhs -= eta_lower_bound;
            model.add(lhs >= 0.0);
            lhs.end();
        }
        eta_lower_bound.end();
    }
}

void add_benders_cut_to_model(
    IloEnv& env,
    IloModel& model,
    const BendersCut& benders_cut,
    const BranchBendersVariableAccess& access,
    int scenario_position,
    const std::string& context) {
    IloExpr expr(env);
    expr += access.eta[static_cast<IloInt>(scenario_position)];
    for (const auto& [compact_index, coefficient] :
         benders_cut.coefficients_by_compact_index) {
        if (std::fabs(coefficient) <= 1.0e-12) {
            continue;
        }
        if (compact_index < 0 ||
            compact_index >= static_cast<int>(access.y_position_by_node.size()) ||
            access.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
            expr.end();
            throw std::runtime_error(context + " references a compact node without a master y variable.");
        }
        const int y_pos =
            access.y_position_by_node[static_cast<std::size_t>(compact_index)];
        expr -= coefficient * access.y[static_cast<IloInt>(y_pos)];
    }
    model.add(expr >= benders_cut.rhs_constant);
    expr.end();
}

IloRange make_benders_cut_range(
    IloEnv& env,
    const BendersCut& benders_cut,
    const BranchBendersVariableAccess& access,
    int scenario_position,
    const std::string& context) {
    IloExpr expr(env);
    expr += access.eta[static_cast<IloInt>(scenario_position)];
    for (const auto& [compact_index, coefficient] :
         benders_cut.coefficients_by_compact_index) {
        if (std::fabs(coefficient) <= 1.0e-12) {
            continue;
        }
        if (compact_index < 0 ||
            compact_index >= static_cast<int>(access.y_position_by_node.size()) ||
            access.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
            expr.end();
            throw std::runtime_error(context + " references a compact node without a master y variable.");
        }
        const int y_pos =
            access.y_position_by_node[static_cast<std::size_t>(compact_index)];
        expr -= coefficient * access.y[static_cast<IloInt>(y_pos)];
    }
    IloRange range(expr >= benders_cut.rhs_constant);
    expr.end();
    return range;
}

void accumulate_combinatorial_summary(
    FppCombinatorialBendersStats& stats,
    const FppCombinatorialSeparationSummary& summary,
    bool fractional,
    int cuts_added) {
    stats.scenarios_checked += summary.scenarios_checked;
    stats.separation_time_sec += summary.separation_time_sec;
    stats.num_violated_cuts += summary.violated_cuts;
    stats.lift_fallback_count += summary.lift_fallback_count;
    if (summary.lift_fallback_count > 0) {
        stats.fractional_lift_disabled_due_to_validity = true;
    }
    if (fractional) {
        stats.fractional_cuts_added += cuts_added;
    } else {
        stats.integer_cuts_added += cuts_added;
    }
    for (const auto& cut : summary.cuts) {
        stats.total_paths_per_cut += static_cast<double>(cut.activation_paths);
        stats.total_nonzeros_per_cut += static_cast<double>(cut.nonzeros);
        ++stats.cuts_for_averages;
    }
}

class FppRestrictedCandidateCallback : public IloCplex::Callback::Function {
public:
    FppRestrictedCandidateCallback(
        const opt::OptimizationInstance& opt,
        FppPersistentScenarioSubproblemManager& subproblem_manager,
        BranchBendersVariableAccess access,
        double tolerance,
        bool verbose,
        BranchBendersCallbackStats& stats)
        : opt_(opt),
          subproblem_manager_(subproblem_manager),
          access_(std::move(access)),
          tolerance_(tolerance),
          verbose_(verbose),
          stats_(stats) {}

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE {
        const auto callback_start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.callback_calls;
            ++stats_.candidate_callback_calls;
        }

        if (!context.inCandidate() || !context.isCandidatePoint()) {
            record_callback_time(callback_start);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.incumbent_callback_calls;
        }

        std::vector<int> ybar(static_cast<std::size_t>(access_.y.getSize()), 0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            ybar[static_cast<std::size_t>(pos)] =
                context.getCandidatePoint(access_.y[pos]) > 0.5 ? 1 : 0;
        }

        std::vector<double> eta_values(static_cast<std::size_t>(access_.eta.getSize()), 0.0);
        for (IloInt s = 0; s < access_.eta.getSize(); ++s) {
            eta_values[static_cast<std::size_t>(s)] = context.getCandidatePoint(access_.eta[s]);
        }

        const auto compact_y = expand_y_to_compact_values(opt_, ybar);
        IloEnv env = context.getEnv();
        IloRangeArray lazy_cuts(env);

        int cuts_added = 0;
        int violated_cuts = 0;
        int nonviolated_cuts = 0;
        int skipped_cuts = 0;
        int duplicate_cuts = 0;
        int subproblems_attempted = 0;
        int subproblems_solved = 0;
        double max_violation = 0.0;
        double subproblem_time = 0.0;
        double max_subproblem_time = 0.0;
        double cut_construction_time = 0.0;
        double lazy_cut_insertion_time = 0.0;
        std::vector<BendersCut> generated_cuts;

        for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
            ++subproblems_attempted;
            const auto sub_result =
                subproblem_manager_.solveScenario(static_cast<int>(s), ybar);
            ++subproblems_solved;
            subproblem_time += sub_result.runtime_seconds;
            max_subproblem_time = std::max(max_subproblem_time, sub_result.runtime_seconds);

            const double eta_value = eta_values[s];
            const double direct_violation = sub_result.objective_value - eta_value;
            const double cut_violation =
                sub_result.benders_cut.violationAt(eta_value, compact_y);
            const double violation = std::max(direct_violation, cut_violation);
            max_violation = std::max(max_violation, violation);

            if (violation > tolerance_) {
                ++violated_cuts;
                const auto signature = cut_signature(sub_result.benders_cut);
                {
                    std::lock_guard<std::mutex> lock(stats_.mutex);
                    const auto [_, inserted] = stats_.cut_signatures.insert(signature);
                    if (!inserted) {
                        ++duplicate_cuts;
                    }
                }
                const auto cut_construction_start = std::chrono::steady_clock::now();
                IloExpr expr(env);
                expr += access_.eta[static_cast<IloInt>(s)];
                for (const auto& [compact_index, coefficient] :
                     sub_result.benders_cut.coefficients_by_compact_index) {
                    if (std::fabs(coefficient) <= 1.0e-12) {
                        continue;
                    }
                    if (compact_index < 0 ||
                        compact_index >= static_cast<int>(access_.y_position_by_node.size()) ||
                        access_.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
                        expr.end();
                        lazy_cuts.end();
                        throw std::runtime_error(
                            "FPP restricted Branch-Benders cut references a compact node without a master y variable.");
                    }
                    const int y_pos =
                        access_.y_position_by_node[static_cast<std::size_t>(compact_index)];
                    expr -= coefficient * access_.y[static_cast<IloInt>(y_pos)];
                }
                IloRange cut(expr >= sub_result.benders_cut.rhs_constant);
                cut_construction_time += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - cut_construction_start).count();
                const auto lazy_insert_start = std::chrono::steady_clock::now();
                lazy_cuts.add(cut);
                lazy_cut_insertion_time += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - lazy_insert_start).count();
                expr.end();
                ++cuts_added;
                generated_cuts.push_back(sub_result.benders_cut);
            } else {
                ++nonviolated_cuts;
            }
        }

        if (lazy_cuts.getSize() > 0) {
            const auto lazy_insert_start = std::chrono::steady_clock::now();
            context.rejectCandidate(lazy_cuts);
            lazy_cut_insertion_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - lazy_insert_start).count();
        }
        lazy_cuts.end();

        solver::BranchBendersIncumbentLog log;
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.candidate_incumbents_checked;
            stats_.subproblems_attempted += subproblems_attempted;
            stats_.subproblems_solved += subproblems_solved;
            stats_.lazy_cuts_added += cuts_added;
            stats_.violated_cuts += violated_cuts;
            stats_.nonviolated_cuts += nonviolated_cuts;
            stats_.skipped_cuts += skipped_cuts;
            stats_.duplicate_cuts += duplicate_cuts;
            stats_.subproblem_time_sec += subproblem_time;
            stats_.max_subproblem_time_sec =
                std::max(stats_.max_subproblem_time_sec, max_subproblem_time);
            stats_.cut_construction_time_sec += cut_construction_time;
            stats_.lazy_cut_insertion_time_sec += lazy_cut_insertion_time;
            stats_.largest_incumbent_cut_violation =
                std::max(stats_.largest_incumbent_cut_violation, max_violation);
            stats_.generated_cuts.insert(
                stats_.generated_cuts.end(),
                generated_cuts.begin(),
                generated_cuts.end());

            log.incumbent_index = stats_.candidate_incumbents_checked;
            log.incumbent_objective = context.getCandidateObjective();
            log.selected_firebreak_count =
                static_cast<int>(selected_original_nodes(opt_, ybar).size());
            log.cuts_added = cuts_added;
            log.max_cut_violation = max_violation;
            log.subproblems_attempted = subproblems_attempted;
            log.subproblems_solved = subproblems_solved;
            log.subproblem_time_sec = subproblem_time;
            log.average_subproblem_time_sec =
                subproblems_solved > 0
                    ? subproblem_time / static_cast<double>(subproblems_solved)
                    : 0.0;
            log.max_subproblem_time_sec = max_subproblem_time;
            log.cut_construction_time_sec = cut_construction_time;
            log.lazy_cut_insertion_time_sec = lazy_cut_insertion_time;
            log.violated_cuts = violated_cuts;
            log.nonviolated_cuts = nonviolated_cuts;
            log.skipped_cuts = skipped_cuts;
            log.duplicate_cuts = duplicate_cuts;
            stats_.incumbent_log.push_back(log);
        }

        record_callback_time(callback_start);
    }

private:
    void record_callback_time(std::chrono::steady_clock::time_point start) {
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::lock_guard<std::mutex> lock(stats_.mutex);
        stats_.callback_time_sec += elapsed;
    }

    const opt::OptimizationInstance& opt_;
    FppPersistentScenarioSubproblemManager& subproblem_manager_;
    BranchBendersVariableAccess access_;
    double tolerance_ = 1.0e-6;
    bool verbose_ = false;
    BranchBendersCallbackStats& stats_;
};

class FppRestrictedCandidateRootUserCutCallback : public IloCplex::Callback::Function {
public:
    FppRestrictedCandidateRootUserCutCallback(
        const opt::OptimizationInstance& opt,
        FppPersistentScenarioSubproblemManager& subproblem_manager,
        BranchBendersVariableAccess access,
        int max_rounds,
        double tolerance,
        bool verbose,
        BranchBendersRootUserCutStats& stats)
        : opt_(opt),
          subproblem_manager_(subproblem_manager),
          access_(std::move(access)),
          max_rounds_(max_rounds),
          tolerance_(tolerance),
          verbose_(verbose),
          stats_(stats) {}

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE {
        const auto callback_start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.callback_calls;
        }

        if (!context.inRelaxation()) {
            record_total_time(callback_start);
            return;
        }

        CPXLONG depth = -1;
        try {
            depth = context.getLongInfo(IloCplex::Callback::Context::Info::NodeDepth);
        } catch (...) {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            stats_.only_at_root_confirmed = false;
            if (stats_.skipped_reason.empty()) {
                stats_.skipped_reason = "NodeDepth unavailable; root user cuts skipped.";
            }
            record_total_time_unlocked(callback_start);
            return;
        }

        if (depth != 0) {
            record_total_time(callback_start);
            return;
        }

        int round_index = 0;
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            if (stats_.rounds_executed >= max_rounds_) {
                if (stats_.skipped_reason.empty()) {
                    stats_.skipped_reason = "Root user cut max rounds reached.";
                }
                record_total_time_unlocked(callback_start);
                return;
            }
            ++stats_.rounds_executed;
            round_index = stats_.rounds_executed;
        }

        std::vector<double> ybar(static_cast<std::size_t>(access_.y.getSize()), 0.0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            const double value = context.getRelaxationPoint(access_.y[pos]);
            ybar[static_cast<std::size_t>(pos)] = std::max(0.0, std::min(1.0, value));
        }

        std::vector<double> eta_values(static_cast<std::size_t>(access_.eta.getSize()), 0.0);
        for (IloInt s = 0; s < access_.eta.getSize(); ++s) {
            eta_values[static_cast<std::size_t>(s)] = context.getRelaxationPoint(access_.eta[s]);
        }

        const auto compact_y = expand_y_to_compact_values(opt_, ybar);

        int cuts_added = 0;
        int scenarios_solved = 0;
        double max_violation = 0.0;
        double sum_violation = 0.0;
        double subproblem_time = 0.0;
        double max_subproblem_time = 0.0;

        for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
            const auto sub_result =
                subproblem_manager_.solveScenarioFractional(static_cast<int>(s), ybar);
            ++scenarios_solved;
            subproblem_time += sub_result.runtime_seconds;
            max_subproblem_time = std::max(max_subproblem_time, sub_result.runtime_seconds);

            const double eta_value = eta_values[s];
            const double violation =
                sub_result.benders_cut.violationAt(eta_value, compact_y);
            max_violation = std::max(max_violation, violation);

            if (violation > tolerance_) {
                sum_violation += violation;
                IloEnv env = context.getEnv();
                IloExpr expr(env);
                expr += access_.eta[static_cast<IloInt>(s)];
                for (const auto& [compact_index, coefficient] :
                     sub_result.benders_cut.coefficients_by_compact_index) {
                    if (std::fabs(coefficient) <= 1.0e-12) {
                        continue;
                    }
                    if (compact_index < 0 ||
                        compact_index >= static_cast<int>(access_.y_position_by_node.size()) ||
                        access_.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
                        expr.end();
                        throw std::runtime_error(
                            "FPP restricted Branch-Benders root user cut references a compact node without a master y variable.");
                    }
                    const int y_pos =
                        access_.y_position_by_node[static_cast<std::size_t>(compact_index)];
                    expr -= coefficient * access_.y[static_cast<IloInt>(y_pos)];
                }
                IloRange cut(expr >= sub_result.benders_cut.rhs_constant);
                context.addUserCut(cut, IloCplex::UseCutPurge, IloFalse);
                cut.end();
                expr.end();
                ++cuts_added;
            }
        }

        solver::BranchBendersRootUserCutRoundLog log;
        log.round_index = round_index;
        log.scenarios_solved = scenarios_solved;
        log.cuts_added = cuts_added;
        log.max_violation = max_violation;
        log.avg_violation =
            cuts_added > 0 ? sum_violation / static_cast<double>(cuts_added) : 0.0;
        log.time_sec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - callback_start).count();

        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            stats_.cuts_added += cuts_added;
            stats_.scenarios_solved += scenarios_solved;
            stats_.max_violation = std::max(stats_.max_violation, max_violation);
            stats_.subproblem_time_sec += subproblem_time;
            stats_.max_subproblem_time_sec =
                std::max(stats_.max_subproblem_time_sec, max_subproblem_time);
            stats_.round_log.push_back(log);
            record_total_time_unlocked(callback_start);
        }
    }

private:
    void record_total_time(std::chrono::steady_clock::time_point start) {
        std::lock_guard<std::mutex> lock(stats_.mutex);
        record_total_time_unlocked(start);
    }

    void record_total_time_unlocked(std::chrono::steady_clock::time_point start) {
        stats_.total_time_sec += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
    }

    const opt::OptimizationInstance& opt_;
    FppPersistentScenarioSubproblemManager& subproblem_manager_;
    BranchBendersVariableAccess access_;
    int max_rounds_ = 1;
    double tolerance_ = 1.0e-6;
    bool verbose_ = false;
    BranchBendersRootUserCutStats& stats_;
};

class FppRestrictedCandidateCombinedCallback : public IloCplex::Callback::Function {
public:
    FppRestrictedCandidateCombinedCallback(
        const opt::OptimizationInstance& opt,
        FppPersistentScenarioSubproblemManager& subproblem_manager,
        BranchBendersVariableAccess access,
        double candidate_tolerance,
        int root_max_rounds,
        double root_tolerance,
        bool verbose,
        BranchBendersCallbackStats& candidate_stats,
        BranchBendersRootUserCutStats& root_stats)
        : candidate_callback_(
              opt,
              subproblem_manager,
              access,
              candidate_tolerance,
              verbose,
              candidate_stats),
          root_callback_(
              opt,
              subproblem_manager,
              std::move(access),
              root_max_rounds,
              root_tolerance,
              verbose,
              root_stats) {}

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE {
        if (context.inCandidate()) {
            candidate_callback_.invoke(context);
            return;
        }
        if (context.inRelaxation()) {
            root_callback_.invoke(context);
        }
    }

private:
    FppRestrictedCandidateCallback candidate_callback_;
    FppRestrictedCandidateRootUserCutCallback root_callback_;
};

class FppRestrictedCombinatorialCallback : public IloCplex::Callback::Function {
public:
    FppRestrictedCombinatorialCallback(
        const opt::OptimizationInstance& opt,
        const FppCombinatorialBendersSeparator& separator,
        FppCombinatorialBendersOptions combinatorial_options,
        BranchBendersVariableAccess access,
        double tolerance,
        BranchBendersCallbackStats& stats)
        : opt_(opt),
          separator_(separator),
          combinatorial_options_(combinatorial_options),
          access_(std::move(access)),
          tolerance_(tolerance),
          stats_(stats) {}

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE {
        const auto callback_start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.callback_calls;
        }
        if (context.inCandidate()) {
            separate_candidate(context, callback_start);
            return;
        }
        if (context.inRelaxation()) {
            separate_relaxation(context, callback_start);
            return;
        }
        record_callback_time(callback_start);
    }

private:
    int scenario_position_for_cut(const BendersCut& cut) const {
        for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
            if (opt_.scenarios[s].scenario_id == cut.scenario_id) {
                return static_cast<int>(s);
            }
        }
        throw std::runtime_error(
            "FPP restricted combinatorial Benders cut references an unknown scenario id.");
    }

    void separate_candidate(
        const IloCplex::Callback::Context& context,
        std::chrono::steady_clock::time_point callback_start) {
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.candidate_callback_calls;
        }
        if (!context.isCandidatePoint()) {
            record_callback_time(callback_start);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.incumbent_callback_calls;
        }
        std::vector<int> ybar_int(static_cast<std::size_t>(access_.y.getSize()), 0);
        std::vector<double> ybar(static_cast<std::size_t>(access_.y.getSize()), 0.0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            const int value = context.getCandidatePoint(access_.y[pos]) > 0.5 ? 1 : 0;
            ybar_int[static_cast<std::size_t>(pos)] = value;
            ybar[static_cast<std::size_t>(pos)] = static_cast<double>(value);
        }
        std::vector<double> eta_values(static_cast<std::size_t>(access_.eta.getSize()), 0.0);
        for (IloInt s = 0; s < access_.eta.getSize(); ++s) {
            eta_values[static_cast<std::size_t>(s)] = context.getCandidatePoint(access_.eta[s]);
        }
        const auto summary = separator_.separateViolatedCuts(
            ybar,
            eta_values,
            false,
            combinatorial_options_.lift_mode,
            combinatorial_options_.cut_sampling_ratio,
            tolerance_);

        IloEnv env = context.getEnv();
        IloRangeArray lazy_cuts(env);
        int cuts_added = 0;
        int duplicate_cuts = 0;
        double cut_construction_time = 0.0;
        double lazy_cut_insertion_time = 0.0;
        std::vector<BendersCut> generated_cuts;
        for (const auto& separated : summary.cuts) {
            const auto signature = cut_signature(separated.cut);
            {
                std::lock_guard<std::mutex> lock(stats_.mutex);
                const auto [_, inserted] = stats_.cut_signatures.insert(signature);
                if (!inserted) {
                    ++duplicate_cuts;
                }
            }
            const auto cut_start = std::chrono::steady_clock::now();
            IloRange cut = make_benders_cut_range(
                env,
                separated.cut,
                access_,
                scenario_position_for_cut(separated.cut),
                "FPP restricted combinatorial Branch-Benders lazy cut");
            cut_construction_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cut_start).count();
            const auto insert_start = std::chrono::steady_clock::now();
            lazy_cuts.add(cut);
            lazy_cut_insertion_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - insert_start).count();
            generated_cuts.push_back(separated.cut);
            ++cuts_added;
        }
        if (lazy_cuts.getSize() > 0) {
            const auto reject_start = std::chrono::steady_clock::now();
            context.rejectCandidate(lazy_cuts);
            lazy_cut_insertion_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - reject_start).count();
        }
        lazy_cuts.end();

        solver::BranchBendersIncumbentLog log;
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.candidate_incumbents_checked;
            stats_.lazy_cuts_added += cuts_added;
            stats_.violated_cuts += summary.violated_cuts;
            stats_.nonviolated_cuts += summary.nonviolated_cuts;
            stats_.skipped_cuts += summary.scenarios_skipped;
            stats_.duplicate_cuts += duplicate_cuts;
            stats_.cut_construction_time_sec += cut_construction_time;
            stats_.lazy_cut_insertion_time_sec += lazy_cut_insertion_time;
            stats_.largest_incumbent_cut_violation =
                std::max(stats_.largest_incumbent_cut_violation, summary.max_violation);
            stats_.generated_cuts.insert(
                stats_.generated_cuts.end(),
                generated_cuts.begin(),
                generated_cuts.end());
            accumulate_combinatorial_summary(
                stats_.combinatorial_stats,
                summary,
                false,
                cuts_added);

            log.incumbent_index = stats_.candidate_incumbents_checked;
            log.incumbent_objective = context.getCandidateObjective();
            log.selected_firebreak_count =
                static_cast<int>(selected_original_nodes(opt_, ybar_int).size());
            log.cuts_added = cuts_added;
            log.max_cut_violation = summary.max_violation;
            log.cut_construction_time_sec = cut_construction_time;
            log.lazy_cut_insertion_time_sec = lazy_cut_insertion_time;
            log.violated_cuts = summary.violated_cuts;
            log.nonviolated_cuts = summary.nonviolated_cuts;
            log.skipped_cuts = summary.scenarios_skipped;
            log.duplicate_cuts = duplicate_cuts;
            stats_.incumbent_log.push_back(log);
        }
        record_callback_time(callback_start);
    }

    void separate_relaxation(
        const IloCplex::Callback::Context& context,
        std::chrono::steady_clock::time_point callback_start) {
        if (!combinatorial_options_.separate_fractional) {
            record_callback_time(callback_start);
            return;
        }
        std::vector<double> ybar(static_cast<std::size_t>(access_.y.getSize()), 0.0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            const double value = context.getRelaxationPoint(access_.y[pos]);
            ybar[static_cast<std::size_t>(pos)] = std::max(0.0, std::min(1.0, value));
        }
        std::vector<double> eta_values(static_cast<std::size_t>(access_.eta.getSize()), 0.0);
        for (IloInt s = 0; s < access_.eta.getSize(); ++s) {
            eta_values[static_cast<std::size_t>(s)] = context.getRelaxationPoint(access_.eta[s]);
        }
        const auto summary = separator_.separateViolatedCuts(
            ybar,
            eta_values,
            true,
            combinatorial_options_.lift_mode,
            combinatorial_options_.cut_sampling_ratio,
            tolerance_);
        int cuts_added = 0;
        double cut_construction_time = 0.0;
        for (const auto& separated : summary.cuts) {
            const auto cut_start = std::chrono::steady_clock::now();
            IloEnv env = context.getEnv();
            IloRange cut = make_benders_cut_range(
                env,
                separated.cut,
                access_,
                scenario_position_for_cut(separated.cut),
                "FPP restricted combinatorial Branch-Benders user cut");
            cut_construction_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cut_start).count();
            context.addUserCut(cut, IloCplex::UseCutPurge, IloFalse);
            cut.end();
            ++cuts_added;
        }
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            stats_.cut_construction_time_sec += cut_construction_time;
            stats_.violated_cuts += summary.violated_cuts;
            stats_.nonviolated_cuts += summary.nonviolated_cuts;
            stats_.skipped_cuts += summary.scenarios_skipped;
            accumulate_combinatorial_summary(
                stats_.combinatorial_stats,
                summary,
                true,
                cuts_added);
        }
        record_callback_time(callback_start);
    }

    void record_callback_time(std::chrono::steady_clock::time_point start) {
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::lock_guard<std::mutex> lock(stats_.mutex);
        stats_.callback_time_sec += elapsed;
    }

    const opt::OptimizationInstance& opt_;
    const FppCombinatorialBendersSeparator& separator_;
    FppCombinatorialBendersOptions combinatorial_options_;
    BranchBendersVariableAccess access_;
    double tolerance_ = 1.0e-6;
    BranchBendersCallbackStats& stats_;
};

void apply_cplex_parameters(
    IloCplex& cplex,
    IloEnv env,
    const FppRestrictedCandidateBranchBendersOptions& options) {
    if (!options.verbose) {
        cplex.setOut(env.getNullStream());
        cplex.setWarning(env.getNullStream());
    }
    if (options.time_limit_seconds > 0.0) {
        cplex.setParam(IloCplex::Param::TimeLimit, options.time_limit_seconds);
    }
    if (options.mip_gap >= 0.0) {
        cplex.setParam(IloCplex::Param::MIP::Tolerances::MIPGap, options.mip_gap);
    }
    if (options.threads > 0) {
        cplex.setParam(IloCplex::Param::Threads, options.threads);
    }
}

int add_pooled_cuts_to_model(
    const RestrictedCandidateCutPool& cut_pool,
    const opt::OptimizationInstance& opt,
    const BranchBendersVariableAccess& access,
    IloEnv env,
    IloModel& model) {
    const auto scenario_position_by_scenario_id = scenario_position_by_id(opt);
    int added = 0;
    for (const auto& record : cut_pool.records()) {
        const auto scenario_it = scenario_position_by_scenario_id.find(record.scenario_id);
        if (scenario_it == scenario_position_by_scenario_id.end()) {
            throw std::runtime_error(
                "Restricted candidate cut pool contains a cut for an unknown scenario id.");
        }

        IloExpr expr(env);
        expr += access.eta[static_cast<IloInt>(scenario_it->second)];
        for (const auto& [compact_index, coefficient] :
             record.cut.coefficients_by_compact_index) {
            if (std::fabs(coefficient) <= 1.0e-12) {
                continue;
            }
            if (compact_index < 0 ||
                compact_index >= static_cast<int>(access.y_position_by_node.size()) ||
                access.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
                expr.end();
                throw std::runtime_error(
                    "Restricted candidate cut pool references a compact node without a master y variable.");
            }
            const int y_pos =
                access.y_position_by_node[static_cast<std::size_t>(compact_index)];
            expr -= coefficient * access.y[static_cast<IloInt>(y_pos)];
        }
        model.add(expr >= record.cut.rhs_constant);
        expr.end();
        ++added;
    }
    return added;
}

RestrictedStageSolveResult solve_stage_impl(
    const opt::OptimizationInstance& opt,
    const FppRestrictedCandidateBranchBendersOptions& options,
    const std::vector<double>& upper_bounds,
    const std::string& stage_name,
    const RestrictedCandidateCutPool& cut_pool,
    FppPersistentScenarioSubproblemManager& subproblem_manager) {
    if (upper_bounds.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("Restricted FPP stage upper-bound vector size must match eligible-node count.");
    }
    for (const double value : upper_bounds) {
        if (value != 0.0 && value != 1.0) {
            throw std::runtime_error("Restricted FPP stage upper bounds must be exactly 0.0 or 1.0.");
        }
    }
    const auto risk_config = effective_risk_config_from(options.risk_config);
    const bool risk_enabled = uses_cvar_risk(risk_config);

    RestrictedStageSolveResult stage;
    stage.model_result.method = "FPP-Restricted-Branch-Benders-" + stage_name;
    stage.model_result.risk_measure = risk::to_string(risk_config.type);
    stage.model_result.cvar_beta = risk_config.cvarBeta;
    stage.model_result.cvar_lambda = risk_config.cvarLambda;
    stage.model_result.branch_benders_enabled = true;
    stage.model_result.combinatorial_benders_enabled =
        options.combinatorial_options.enabled;
    stage.model_result.combinatorial_benders_lift_mode =
        to_string(options.combinatorial_options.lift_mode);
    stage.model_result.combinatorial_benders_cut_sampling_ratio =
        options.combinatorial_options.cut_sampling_ratio;
    stage.model_result.combinatorial_benders_fractional_separation_enabled =
        options.combinatorial_options.separate_fractional;
    stage.model_result.combinatorial_benders_initial_cuts_enabled =
        options.combinatorial_options.initial_cuts;
    const double root_user_cut_tolerance = effective_root_user_cut_tolerance(options);
    stage.model_result.branch_benders_use_root_user_cuts = options.use_root_user_cuts;
    stage.model_result.branch_benders_root_user_cut_max_rounds =
        options.root_user_cut_max_rounds;
    stage.model_result.branch_benders_root_user_cut_tolerance = root_user_cut_tolerance;
    if (!options.use_root_user_cuts) {
        stage.model_result.branch_benders_root_user_cut_skipped_reason =
            "Root user cuts disabled.";
    }
    stage.model_result.branch_benders_root_user_cut_only_at_root_confirmed = true;

    const auto solve_start = std::chrono::steady_clock::now();
    IloEnv env;
    try {
        const auto master_build_start = std::chrono::steady_clock::now();
        IloModel model(env);
        std::vector<int> y_position_by_node(static_cast<std::size_t>(opt.node_mapper.size()), -1);

        IloBoolVarArray y(env, static_cast<IloInt>(opt.eligible_indices.size()));
        const auto bound_update_start = std::chrono::steady_clock::now();
        for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
            const int compact_index = opt.eligible_indices[pos];
            y_position_by_node[static_cast<std::size_t>(compact_index)] = static_cast<int>(pos);
            y[static_cast<IloInt>(pos)].setUB(upper_bounds[pos]);
            std::ostringstream name;
            name << "y_" << compact_index;
            y[static_cast<IloInt>(pos)].setName(name.str().c_str());
        }
        stage.master_bound_update_time =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - bound_update_start).count();

        IloNumVarArray eta(
            env,
            static_cast<IloInt>(opt.scenarios.size()),
            0.0,
            IloInfinity,
            ILOFLOAT);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            std::ostringstream name;
            name << "eta_s" << opt.scenarios[s].scenario_id;
            eta[static_cast<IloInt>(s)].setName(name.str().c_str());
        }

        IloNumVar risk_threshold;
        IloNumVarArray cvar_excess(env);
        int risk_constraint_count = 0;
        if (risk_enabled) {
            risk_threshold = IloNumVar(env, -IloInfinity, IloInfinity, ILOFLOAT);
            risk_threshold.setName("risk_threshold");
            cvar_excess = IloNumVarArray(
                env,
                static_cast<IloInt>(opt.scenarios.size()),
                0.0,
                IloInfinity,
                ILOFLOAT);
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                std::ostringstream name;
                name << "cvar_excess_s" << opt.scenarios[s].scenario_id;
                cvar_excess[static_cast<IloInt>(s)].setName(name.str().c_str());

                IloExpr excess_lhs(env);
                excess_lhs += cvar_excess[static_cast<IloInt>(s)];
                excess_lhs -= eta[static_cast<IloInt>(s)];
                excess_lhs += risk_threshold;
                IloRange excess_range = (excess_lhs >= 0.0);
                std::ostringstream constraint_name;
                constraint_name << "cvar_excess_link_s" << opt.scenarios[s].scenario_id;
                excess_range.setName(constraint_name.str().c_str());
                model.add(excess_range);
                excess_lhs.end();
                ++risk_constraint_count;
            }
        }

        IloExpr objective(env);
        const double cvar_tail_scale = 1.0 / (1.0 - risk_config.cvarBeta);
        if (risk_config.type == risk::RiskMeasureType::Expected) {
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                objective += opt.scenarios[s].probability * eta[static_cast<IloInt>(s)];
            }
        } else {
            const bool include_expected_term =
                risk_config.type == risk::RiskMeasureType::MeanCVaR;
            const double expected_weight = include_expected_term
                ? (1.0 - risk_config.cvarLambda)
                : 0.0;
            if (include_expected_term && expected_weight != 0.0) {
                for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                    objective +=
                        expected_weight *
                        opt.scenarios[s].probability *
                        eta[static_cast<IloInt>(s)];
                }
            }

            IloExpr cvar_tail(env);
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                cvar_tail +=
                    opt.scenarios[s].probability *
                    cvar_excess[static_cast<IloInt>(s)];
            }
            objective +=
                risk_config.cvarLambda * risk_threshold +
                risk_config.cvarLambda * cvar_tail_scale * cvar_tail;
            cvar_tail.end();
        }
        model.add(IloMinimize(env, objective));
        objective.end();

        IloExpr budget(env);
        for (IloInt pos = 0; pos < y.getSize(); ++pos) {
            budget += y[pos];
        }
        model.add(budget <= opt.budget);
        budget.end();

        BranchBendersVariableAccess access;
        access.y = y;
        access.eta = eta;
        access.y_position_by_node = y_position_by_node;

        int lifted_lower_bound_count = 0;
        if (options.use_lifted_lower_bounds) {
            const auto llb_result = build_fpp_lifted_lower_bounds(opt);
            for (std::size_t s = 0; s < llb_result.inequalities.size(); ++s) {
                const auto& inequality = llb_result.inequalities[s];
                IloExpr expr(env);
                expr += eta[static_cast<IloInt>(s)];
                for (const auto& [compact_index, coefficient] :
                     inequality.coefficients_by_compact_index) {
                    if (std::fabs(coefficient) <= 1.0e-12) {
                        continue;
                    }
                    if (compact_index < 0 ||
                        compact_index >= static_cast<int>(y_position_by_node.size()) ||
                        y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
                        expr.end();
                        throw std::runtime_error(
                            "FPP restricted Branch-Benders LLBI references a compact node without a master y variable.");
                    }
                    const int y_pos =
                        y_position_by_node[static_cast<std::size_t>(compact_index)];
                    expr -= coefficient * y[static_cast<IloInt>(y_pos)];
                }
                model.add(expr >= inequality.rhs_constant);
                expr.end();
                ++lifted_lower_bound_count;

                solver::BendersLiftedLowerBoundRecord record;
                record.scenario_id = inequality.scenario_id;
                record.f_empty = inequality.f_empty;
                record.rhs_constant = inequality.rhs_constant;
                record.num_nonzero_coefficients = inequality.nonzero_coefficients;
                record.coefficients_by_compact_index =
                    inequality.coefficients_by_compact_index;
                stage.model_result.benders_lifted_lower_bounds.push_back(std::move(record));
            }
            stage.model_result.benders_use_lifted_lower_bounds = true;
            stage.model_result.benders_lifted_lower_bound_count = lifted_lower_bound_count;
            stage.model_result.benders_lifted_lower_bound_precompute_time_sec =
                llb_result.precompute_time_sec;
            stage.model_result.benders_lifted_lower_bound_nonzero_coefficients =
                llb_result.total_nonzero_coefficients;
            stage.model_result.benders_lifted_lower_bound_min_rhs = llb_result.min_rhs;
            stage.model_result.benders_lifted_lower_bound_max_rhs = llb_result.max_rhs;
            stage.model_result.benders_lifted_lower_bound_notes = llb_result.notes;
        }

        const auto coverage_llbi = build_fpp_coverage_llbi_data(
            opt,
            options.strengthening_options.use_coverage_llbi);
        const auto path_llbi = build_fpp_path_llbi_data(
            opt,
            options.strengthening_options.use_path_llbi,
            options.strengthening_options.path_llbi_max_paths_per_node);
        add_coverage_llbi_constraints(
            env,
            model,
            coverage_llbi,
            y,
            eta,
            y_position_by_node);
        add_path_llbi_constraints(
            env,
            model,
            path_llbi,
            y,
            eta,
            y_position_by_node);
        stage.model_result.coverage_llbi_enabled = coverage_llbi.enabled;
        stage.model_result.coverage_llbi_num_zeta_vars = coverage_llbi.num_zeta_vars;
        stage.model_result.coverage_llbi_num_constraints = coverage_llbi.num_constraints;
        stage.model_result.coverage_llbi_precompute_time_sec = coverage_llbi.precompute_time_sec;
        stage.model_result.path_llbi_enabled = path_llbi.enabled;
        stage.model_result.path_llbi_num_b_vars = path_llbi.num_b_vars;
        stage.model_result.path_llbi_num_path_constraints = path_llbi.num_path_constraints;
        stage.model_result.path_llbi_num_paths_used = path_llbi.num_paths_used;
        stage.model_result.path_llbi_precompute_time_sec = path_llbi.precompute_time_sec;
        stage.model_result.conditional_zero_benefit_enabled =
            options.strengthening_options.use_conditional_zero_benefit_fixing;
        if (options.strengthening_options.use_conditional_zero_benefit_fixing) {
            stage.model_result.notes.push_back(
                "Conditional zero-benefit local fixing requested, but CPLEX generic callbacks in this restricted solver do not safely expose node-local y upper-bound tightening; diagnostics are reported with zero applied local fixings.");
        }

        std::unique_ptr<FppCombinatorialBendersSeparator> combinatorial_separator;
        int combinatorial_initial_cuts_added = 0;
        if (options.combinatorial_options.enabled) {
            combinatorial_separator =
                std::make_unique<FppCombinatorialBendersSeparator>(opt);
            if (options.combinatorial_options.initial_cuts) {
                const auto initial_y = combinatorial_separator->greedyInitialSolution();
                const auto initial_cuts =
                    combinatorial_separator->initialCutsFromSolution(
                        initial_y,
                        options.combinatorial_options.lift_mode);
                for (std::size_t s = 0; s < initial_cuts.size(); ++s) {
                    add_benders_cut_to_model(
                        env,
                        model,
                        initial_cuts[s].cut,
                        access,
                        static_cast<int>(s),
                        "FPP restricted combinatorial initial Benders cut");
                    ++combinatorial_initial_cuts_added;
                }
            }
        }

        const auto cut_insert_start = std::chrono::steady_clock::now();
        stage.preloaded_cuts_added = add_pooled_cuts_to_model(
            cut_pool,
            opt,
            access,
            env,
            model);
        stage.master_cut_insertion_time =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cut_insert_start).count();
        stage.model_result.benders_cuts_added = stage.preloaded_cuts_added;
        stage.master_model_build_time =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - master_build_start).count() -
            stage.master_cut_insertion_time;
        if (stage.master_model_build_time < 0.0) {
            stage.master_model_build_time = 0.0;
        }

        IloCplex cplex(model);
        apply_cplex_parameters(cplex, env, options);

        BranchBendersCallbackStats callback_stats;
        callback_stats.combinatorial_stats.enabled =
            options.combinatorial_options.enabled;
        callback_stats.combinatorial_stats.lift_mode =
            to_string(options.combinatorial_options.lift_mode);
        callback_stats.combinatorial_stats.cut_sampling_ratio =
            options.combinatorial_options.cut_sampling_ratio;
        callback_stats.combinatorial_stats.fractional_separation_enabled =
            options.combinatorial_options.separate_fractional;
        callback_stats.combinatorial_stats.initial_cuts_enabled =
            options.combinatorial_options.initial_cuts;
        callback_stats.combinatorial_stats.initial_cuts_added =
            combinatorial_initial_cuts_added;
        BranchBendersRootUserCutStats root_user_stats;
        root_user_stats.enabled = options.use_root_user_cuts;
        root_user_stats.max_rounds = options.root_user_cut_max_rounds;
        root_user_stats.tolerance = root_user_cut_tolerance;

        std::unique_ptr<FppRestrictedCandidateCallback> candidate_callback;
        std::unique_ptr<FppRestrictedCandidateCombinedCallback> combined_callback;
        std::unique_ptr<FppRestrictedCombinatorialCallback> combinatorial_callback;
        if (options.combinatorial_options.enabled) {
            if (!combinatorial_separator) {
                throw std::runtime_error(
                    "FPP restricted combinatorial separator was not initialized.");
            }
            combinatorial_callback = std::make_unique<FppRestrictedCombinatorialCallback>(
                opt,
                *combinatorial_separator,
                options.combinatorial_options,
                access,
                options.tolerance,
                callback_stats);
            CPXLONG context_mask =
                IloCplex::Callback::Context::Id::Candidate;
            if (options.combinatorial_options.separate_fractional) {
                context_mask = context_mask | IloCplex::Callback::Context::Id::Relaxation;
            }
            cplex.use(combinatorial_callback.get(), context_mask);
            if (options.use_root_user_cuts) {
                root_user_stats.skipped_reason =
                    "LP root user cuts disabled because restricted combinatorial fractional separation is controlled by combinatorial_benders_fractional_separation_enabled.";
            }
        } else if (options.use_root_user_cuts) {
            combined_callback = std::make_unique<FppRestrictedCandidateCombinedCallback>(
                opt,
                subproblem_manager,
                access,
                options.tolerance,
                options.root_user_cut_max_rounds,
                root_user_cut_tolerance,
                options.verbose,
                callback_stats,
                root_user_stats);
            cplex.use(
                combined_callback.get(),
                IloCplex::Callback::Context::Id::Candidate |
                    IloCplex::Callback::Context::Id::Relaxation);
        } else {
            candidate_callback = std::make_unique<FppRestrictedCandidateCallback>(
                opt,
                subproblem_manager,
                access,
                options.tolerance,
                options.verbose,
                callback_stats);
            cplex.use(candidate_callback.get(), IloCplex::Callback::Context::Id::Candidate);
        }

        const bool solved = cplex.solve();
        const auto solve_end = std::chrono::steady_clock::now();
        stage.model_result.runtime_seconds = std::chrono::duration<double>(solve_end - solve_start).count();

        std::ostringstream status;
        status << cplex.getStatus();
        stage.model_result.status = solved ? status.str() : "No feasible restricted Branch-Benders solution";
        stage.model_result.solver_status_code = static_cast<int>(cplex.getCplexStatus());
        if (!solved) {
            env.end();
            return stage;
        }

        stage.model_result.best_bound = cplex.getBestObjValue();
        stage.model_result.mip_gap = cplex.getMIPRelativeGap();
        stage.model_result.explored_nodes = static_cast<long long>(cplex.getNnodes());

        std::vector<int> y_values;
        y_values.reserve(static_cast<std::size_t>(y.getSize()));
        for (IloInt pos = 0; pos < y.getSize(); ++pos) {
            y_values.push_back(cplex.getValue(y[pos]) > 0.5 ? 1 : 0);
        }
        std::vector<double> eta_values;
        eta_values.reserve(static_cast<std::size_t>(eta.getSize()));
        for (IloInt s = 0; s < eta.getSize(); ++s) {
            eta_values.push_back(cplex.getValue(eta[s]));
        }

        const auto compact_y = expand_y_to_compact_values(opt, y_values);
        double final_max_violation = 0.0;
        double verification_subproblem_time = 0.0;
        double verification_max_subproblem_time = 0.0;
        std::vector<double> scenario_recourse_values;
        scenario_recourse_values.reserve(opt.scenarios.size());
        stage.scenario_losses_by_id.reserve(opt.scenarios.size());
        if (options.combinatorial_options.enabled) {
            if (!combinatorial_separator) {
                throw std::runtime_error(
                    "FPP restricted combinatorial separator was not initialized.");
            }
            scenario_recourse_values =
                combinatorial_separator->evaluateScenarioLosses(y_values);
            std::vector<double> y_values_double;
            y_values_double.reserve(y_values.size());
            for (const int value : y_values) {
                y_values_double.push_back(static_cast<double>(value));
            }
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                stage.scenario_losses_by_id.push_back({
                    opt.scenarios[s].scenario_id,
                    scenario_recourse_values[s],
                });
                const double eta_value = eta_values[s];
                const double direct_violation = scenario_recourse_values[s] - eta_value;
                const auto separated = combinatorial_separator->separateScenario(
                    static_cast<int>(s),
                    y_values_double,
                    eta_value,
                    false,
                    options.combinatorial_options.lift_mode,
                    options.tolerance);
                final_max_violation = std::max(
                    final_max_violation,
                    std::max(direct_violation, separated.violation));
            }
        } else {
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                const auto sub_result =
                    subproblem_manager.solveScenario(static_cast<int>(s), y_values);
                scenario_recourse_values.push_back(sub_result.objective_value);
                stage.scenario_losses_by_id.push_back({
                    opt.scenarios[s].scenario_id,
                    sub_result.objective_value,
                });
                verification_subproblem_time += sub_result.runtime_seconds;
                verification_max_subproblem_time =
                    std::max(verification_max_subproblem_time, sub_result.runtime_seconds);
                const double eta_value = eta_values[s];
                const double direct_violation = sub_result.objective_value - eta_value;
                const double cut_violation =
                    sub_result.benders_cut.violationAt(eta_value, compact_y);
                final_max_violation =
                    std::max(final_max_violation, std::max(direct_violation, cut_violation));
            }
        }
        if (final_max_violation < options.tolerance) {
            final_max_violation = std::max(0.0, final_max_violation);
        }
        const auto risk_evaluation = evaluate_risk_objective(
            opt,
            scenario_recourse_values,
            risk_config);

        stage.model_result.objective_value = risk_evaluation.objective;
        stage.model_result.expected_loss_component = risk_evaluation.expected;
        if (risk_enabled) {
            stage.model_result.cvar_loss_component = risk_evaluation.cvar;
            stage.model_result.risk_threshold_value = cplex.getValue(risk_threshold);
            stage.cvar_excess_by_id.reserve(opt.scenarios.size());
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                stage.cvar_excess_by_id.push_back({
                    opt.scenarios[s].scenario_id,
                    cplex.getValue(cvar_excess[static_cast<IloInt>(s)]),
                });
            }
        }
        stage.model_result.selected_firebreak_indices = selected_compact_indices(opt, y_values);
        stage.model_result.selected_firebreak_original_nodes = selected_original_nodes(opt, y_values);
        stage.model_result.iterations = static_cast<int>(cplex.getNiterations());
        stage.model_result.cuts_added =
            callback_stats.lazy_cuts_added +
            callback_stats.combinatorial_stats.fractional_cuts_added +
            callback_stats.combinatorial_stats.initial_cuts_added;
        stage.model_result.max_cut_violation = final_max_violation;
        stage.model_result.branch_benders_callback_calls = callback_stats.callback_calls;
        stage.model_result.branch_benders_candidate_callback_calls =
            callback_stats.candidate_callback_calls;
        stage.model_result.branch_benders_incumbent_callback_calls =
            callback_stats.incumbent_callback_calls;
        stage.model_result.branch_benders_candidate_incumbents_checked =
            callback_stats.candidate_incumbents_checked;
        stage.model_result.branch_benders_subproblems_attempted =
            callback_stats.subproblems_attempted +
            (options.combinatorial_options.enabled ? 0 : static_cast<int>(opt.scenarios.size())) +
            root_user_stats.scenarios_solved;
        stage.model_result.branch_benders_subproblems_solved =
            callback_stats.subproblems_solved +
            (options.combinatorial_options.enabled ? 0 : static_cast<int>(opt.scenarios.size())) +
            root_user_stats.scenarios_solved;
        stage.model_result.branch_benders_lazy_cuts_added = callback_stats.lazy_cuts_added;
        stage.model_result.branch_benders_max_cut_violation = final_max_violation;
        stage.model_result.branch_benders_largest_incumbent_cut_violation =
            callback_stats.largest_incumbent_cut_violation;
        stage.model_result.branch_benders_callback_time_sec = callback_stats.callback_time_sec;
        stage.model_result.branch_benders_subproblem_time_sec =
            callback_stats.subproblem_time_sec +
            verification_subproblem_time +
            root_user_stats.subproblem_time_sec;
        stage.model_result.branch_benders_average_subproblem_time_sec =
            stage.model_result.branch_benders_subproblems_solved > 0
                ? stage.model_result.branch_benders_subproblem_time_sec /
                    static_cast<double>(stage.model_result.branch_benders_subproblems_solved)
                : 0.0;
        stage.model_result.branch_benders_max_subproblem_time_sec = std::max(
            std::max(callback_stats.max_subproblem_time_sec, verification_max_subproblem_time),
            root_user_stats.max_subproblem_time_sec);
        stage.model_result.branch_benders_cut_construction_time_sec =
            callback_stats.cut_construction_time_sec;
        stage.model_result.branch_benders_lazy_cut_insertion_time_sec =
            callback_stats.lazy_cut_insertion_time_sec;
        stage.model_result.branch_benders_violated_cuts = callback_stats.violated_cuts;
        stage.model_result.branch_benders_nonviolated_cuts = callback_stats.nonviolated_cuts;
        stage.model_result.branch_benders_skipped_cuts = callback_stats.skipped_cuts;
        stage.model_result.branch_benders_duplicate_cuts = callback_stats.duplicate_cuts;
        stage.model_result.branch_benders_incumbent_log = callback_stats.incumbent_log;
        stage.model_result.combinatorial_benders_enabled =
            callback_stats.combinatorial_stats.enabled;
        stage.model_result.combinatorial_benders_lift_mode =
            callback_stats.combinatorial_stats.lift_mode;
        stage.model_result.combinatorial_benders_cut_sampling_ratio =
            callback_stats.combinatorial_stats.cut_sampling_ratio;
        stage.model_result.combinatorial_benders_fractional_separation_enabled =
            callback_stats.combinatorial_stats.fractional_separation_enabled;
        stage.model_result.combinatorial_benders_initial_cuts_enabled =
            callback_stats.combinatorial_stats.initial_cuts_enabled;
        stage.model_result.combinatorial_benders_integer_cuts_added =
            callback_stats.combinatorial_stats.integer_cuts_added;
        stage.model_result.combinatorial_benders_fractional_cuts_added =
            callback_stats.combinatorial_stats.fractional_cuts_added;
        stage.model_result.combinatorial_benders_initial_cuts_added =
            callback_stats.combinatorial_stats.initial_cuts_added;
        stage.model_result.combinatorial_benders_scenarios_checked =
            callback_stats.combinatorial_stats.scenarios_checked;
        stage.model_result.combinatorial_benders_separation_time_sec =
            callback_stats.combinatorial_stats.separation_time_sec;
        stage.model_result.combinatorial_benders_avg_paths_per_cut =
            callback_stats.combinatorial_stats.average_paths_per_cut();
        stage.model_result.combinatorial_benders_avg_cut_nonzeros =
            callback_stats.combinatorial_stats.average_cut_nonzeros();
        stage.model_result.combinatorial_benders_num_violated_cuts =
            callback_stats.combinatorial_stats.num_violated_cuts;
        stage.model_result.branch_benders_use_root_user_cuts = options.use_root_user_cuts;
        stage.model_result.branch_benders_root_user_cut_max_rounds =
            options.root_user_cut_max_rounds;
        stage.model_result.branch_benders_root_user_cut_tolerance =
            root_user_cut_tolerance;
        stage.model_result.branch_benders_root_user_cut_rounds_executed =
            root_user_stats.rounds_executed;
        stage.model_result.branch_benders_root_user_cut_callback_calls =
            root_user_stats.callback_calls;
        stage.model_result.branch_benders_root_user_cuts_added =
            root_user_stats.cuts_added;
        stage.model_result.branch_benders_root_user_cut_scenarios_solved =
            root_user_stats.scenarios_solved;
        stage.model_result.branch_benders_root_user_cut_max_violation =
            root_user_stats.max_violation;
        stage.model_result.branch_benders_root_user_cut_total_time_sec =
            root_user_stats.total_time_sec;
        stage.model_result.branch_benders_root_user_cut_subproblem_time_sec =
            root_user_stats.subproblem_time_sec;
        stage.model_result.branch_benders_root_user_cut_skipped_reason =
            options.use_root_user_cuts
                ? root_user_stats.skipped_reason
                : "Root user cuts disabled.";
        stage.model_result.branch_benders_root_user_cut_only_at_root_confirmed =
            root_user_stats.only_at_root_confirmed;
        stage.model_result.branch_benders_root_user_cut_round_log =
            root_user_stats.round_log;
        const auto master_structure =
            analyze_fpp_restricted_candidate_branch_benders_master_structure(opt, risk_config);
        stage.model_result.num_variables =
            master_structure.total_variable_count +
            static_cast<std::size_t>(coverage_llbi.num_zeta_vars) +
            static_cast<std::size_t>(path_llbi.num_b_vars);
        stage.model_result.num_constraints =
            master_structure.base_constraint_count +
            static_cast<std::size_t>(risk_constraint_count) +
            static_cast<std::size_t>(
                stage.preloaded_cuts_added +
                callback_stats.lazy_cuts_added +
                callback_stats.combinatorial_stats.initial_cuts_added +
                callback_stats.combinatorial_stats.fractional_cuts_added +
                root_user_stats.cuts_added +
                lifted_lower_bound_count) +
            static_cast<std::size_t>(coverage_llbi.num_constraints) +
            static_cast<std::size_t>(path_llbi.num_path_constraints) +
            (path_llbi.enabled ? path_llbi.scenarios.size() : 0);
        stage.model_result.compact_node_count = opt.node_mapper.size();
        stage.model_result.eligible_node_count = static_cast<int>(opt.eligible_indices.size());
        stage.model_result.total_scenario_arcs = static_cast<int>(opt.total_arcs);
        stage.model_result.notes.push_back("Restricted-candidate FPP Branch-and-Benders stage: " + stage_name + ".");
        stage.model_result.notes.push_back("Master keeps the full eligible firebreak vector and applies candidate upper bounds.");
        stage.model_result.notes.push_back("Lazy Benders cuts are generated over the full eligible y-vector.");
        stage.model_result.notes.push_back("Generated full-space cuts are stored for activation and cut-pool reuse.");
        if (options.combinatorial_options.enabled) {
            stage.model_result.notes.push_back(
                "Restricted combinatorial FPP Branch-and-Benders separation is enabled; scenario cuts are generated by graph search instead of LP subproblem solves.");
        }
        if (options.combinatorial_options.enabled &&
            options.combinatorial_options.separate_fractional) {
            stage.model_result.notes.push_back(
                "Optional restricted FPP fractional combinatorial Benders user cuts were separated in CPLEX relaxation callbacks.");
        } else if (options.use_root_user_cuts) {
            stage.model_result.notes.push_back(
                "Optional FPP fractional Benders user cuts were separated at the root node only.");
        } else {
            stage.model_result.notes.push_back("FPP fractional root user cuts were disabled.");
        }
        if (options.use_lifted_lower_bounds) {
            stage.model_result.notes.push_back(
                "Optional FPP lifted lower-bound inequalities were added to the restricted callback master.");
        } else {
            stage.model_result.notes.push_back("FPP lifted lower-bound inequalities were disabled.");
        }
        if (risk_config.type == risk::RiskMeasureType::Expected) {
            stage.model_result.notes.push_back(
                "Restricted FPP Branch-Benders master objective is expected burned area.");
        } else if (risk_config.type == risk::RiskMeasureType::CVaR) {
            stage.model_result.notes.push_back(
                "Restricted FPP Branch-Benders master objective is pure CVaR of scenario eta variables.");
        } else {
            stage.model_result.notes.push_back(
                "Restricted FPP Branch-Benders master objective is a mean-CVaR blend of scenario eta variables.");
        }
        if (stage.preloaded_cuts_added > 0) {
            stage.model_result.notes.push_back(
                "Preloaded " + std::to_string(stage.preloaded_cuts_added) +
                " full-space cuts from the restricted-candidate cut pool.");
        }
        stage.generated_cuts = callback_stats.generated_cuts;

        env.end();
        return stage;
    } catch (const IloException& exc) {
        std::string message = "CPLEX exception in FPP restricted Branch-Benders solver: ";
        message += exc.getMessage();
        env.end();
        throw std::runtime_error(message);
    } catch (...) {
        env.end();
        throw;
    }
}

#endif

RestrictedStageSolveResult solve_stage(
    const opt::OptimizationInstance& opt,
    const FppRestrictedCandidateBranchBendersOptions& options,
    const std::vector<double>& upper_bounds,
    const std::string& stage_name,
    const RestrictedCandidateCutPool& cut_pool,
    FppPersistentScenarioSubproblemManager& subproblem_manager) {
#ifndef FIREBREAK_WITH_CPLEX
    (void)opt;
    (void)options;
    (void)upper_bounds;
    (void)stage_name;
    (void)cut_pool;
    (void)subproblem_manager;
    throw std::runtime_error(solver::cplex_unavailable_message());
#else
    return solve_stage_impl(
        opt,
        options,
        upper_bounds,
        stage_name,
        cut_pool,
        subproblem_manager);
#endif
}

}  // namespace

FppRestrictedCandidateBranchBendersMasterStructure
analyze_fpp_restricted_candidate_branch_benders_master_structure(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config) {
    validate_instance(opt);
    const auto effective_risk_config = effective_risk_config_from(risk_config);
    const bool risk_enabled = uses_cvar_risk(effective_risk_config);

    FppRestrictedCandidateBranchBendersMasterStructure structure;
    structure.y_variable_count = opt.eligible_indices.size();
    structure.eta_variable_count = opt.scenarios.size();
    if (risk_enabled) {
        structure.risk_threshold_variable_count = 1;
        structure.cvar_excess_variable_count = opt.scenarios.size();
        structure.risk_constraint_count = opt.scenarios.size();
    }
    structure.total_variable_count =
        structure.y_variable_count +
        structure.eta_variable_count +
        structure.risk_threshold_variable_count +
        structure.cvar_excess_variable_count;
    structure.budget_constraint_count = 1;
    structure.base_constraint_count = 1;
    structure.has_scenario_recourse_variables = false;
    return structure;
}

FppRestrictedCandidateBranchBendersResult FppRestrictedCandidateBranchBendersSolver::solve(
    const opt::OptimizationInstance& opt,
    const FppRestrictedCandidateBranchBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);
    const auto risk_config = effective_risk_config_from(options.risk_config);
    const auto global_start = std::chrono::steady_clock::now();

    std::vector<std::pair<int, double>> burn_frequency_scores;
    bool burn_frequency_score_available = false;
    const std::vector<int> initial_active_candidates = initial_candidates_from_options(
        opt,
        options,
        burn_frequency_scores,
        burn_frequency_score_available);

    RestrictedCandidateManager manager(
        static_cast<int>(opt.eligible_indices.size()),
        opt.budget,
        initial_active_candidates);
    CandidateBoundController bounds(static_cast<int>(opt.eligible_indices.size()));
    RestrictedCandidateCutPool cut_pool;
    FppPersistentScenarioSubproblemManager subproblem_manager(opt, options.verbose);
    RestrictedCandidateMaintenanceTracker maintenance_tracker(
        manager.candidateCount(),
        manager.activeCandidates());
    const RestrictedCandidateMaintenanceOptions maintenance_options =
        effective_maintenance_options(options, manager);

    FppRestrictedCandidateBranchBendersResult result;
    result.risk_measure = risk::to_string(risk_config.type);
    result.cvar_beta = risk_config.cvarBeta;
    result.cvar_lambda = risk_config.cvarLambda;
    result.restricted_candidate_exact_mode = options.eventually_activate_all;
    result.heuristic_mode_enabled = options.restricted_heuristic_mode;
    result.initial_candidate_policy = options.initial_candidate_policy;
    result.activation_policy = options.activation_policy;
    result.candidate_maintenance_policy = options.candidate_maintenance_policy;
    result.deactivation_enabled = options.candidate_maintenance_policy != "none";
    result.candidate_score_mode = options.candidate_score_mode;
    result.candidate_tail_score_gamma = options.candidate_tail_score_gamma;
    result.candidate_tail_protection_size =
        effective_tail_protection_size(options);
    result.candidate_min_active_size = maintenance_options.min_active_size;
    result.candidate_max_active_size = maintenance_options.max_active_size;
    result.candidate_deactivation_batch_size =
        maintenance_options.deactivation_batch_size;
    result.candidate_deactivation_min_age =
        maintenance_options.deactivation_min_age;
    result.candidate_reactivation_cooldown_rounds =
        maintenance_options.reactivation_cooldown_rounds;
    result.protect_selected_candidates =
        maintenance_options.protect_selected_candidates;
    result.tail_score_diagnostics_enabled =
        options.export_tail_score_diagnostics ||
        uses_tail_aware_candidate_scoring(options);
    result.activation_batch_size = options.activation_batch_size;
    result.global_time_budget_enabled = options.time_limit_seconds > 0.0;
    result.global_time_limit_seconds = options.time_limit_seconds;
    result.persistent_master_enabled = false;
    result.persistent_master_note =
        "Phase 1O keeps the master-rebuild fallback across activation stages; pooled full-space cuts are reinserted before each rebuilt callback Branch-and-Benders stage.";
    result.burn_frequency_score_available = burn_frequency_score_available;
    result.top_burn_frequency_candidates = burn_frequency_score_available
        ? topBurnFrequencyCandidates(
              burn_frequency_scores,
              static_cast<int>(std::min<std::size_t>(10, burn_frequency_scores.size())))
        : std::vector<std::pair<int, double>>();
    if (options.initial_candidate_policy == "burn-frequency") {
        result.initial_candidates_from_burn_frequency = initial_active_candidates;
    }
    result.initial_active_candidate_count = manager.activeCount();
    result.active_candidate_count_after_restricted_stage = manager.activeCount();
    result.active_candidate_count_final = manager.activeCount();
    result.active_candidate_fraction_final = manager.activeFraction();
    result.eventually_activated_all = manager.allActive();
    result.notes.push_back("FPP restricted Branch-and-Benders solver with Phase 1O persistence diagnostics.");
    if (risk_config.type == risk::RiskMeasureType::Expected) {
        result.notes.push_back("Restricted FPP risk measure: expected burned area.");
    } else if (risk_config.type == risk::RiskMeasureType::CVaR) {
        result.notes.push_back("Restricted FPP risk measure: pure CVaR.");
    } else {
        result.notes.push_back("Restricted FPP risk measure: mean-CVaR blend.");
    }
    if (uses_tail_aware_candidate_scoring(options)) {
        result.notes.push_back(
            "DPV and reduced-cost activation remain disabled; LLBI and root cuts follow their restricted Branch-and-Benders options.");
    } else {
        result.notes.push_back(
            "DPV, reduced-cost activation, and CVaR-aware activation weighting are disabled; LLBI and root cuts follow their restricted Branch-and-Benders options.");
    }
    result.notes.push_back("Exactness is claimed only after eventual full activation and an optimal final solve.");
    result.notes.push_back("Restricted-stage cuts are generated over the full y-vector and persisted in a reusable cut pool.");
    if (options.combinatorial_options.enabled) {
        result.notes.push_back(
            "Restricted combinatorial Branch-and-Benders is enabled; stage separation uses graph-search cuts instead of LP scenario subproblem solves.");
    } else {
        result.notes.push_back("Phase 1O reuses one persistent FPP scenario subproblem per scenario and updates fixed y-copy equality bounds before each solve.");
    }
    result.notes.push_back("Phase 1O does not preserve the CPLEX master branch-and-bound tree across activation stages; the master is rebuilt and full-space cuts are reinserted.");
    update_subproblem_diagnostics(result, subproblem_manager);
    if (options.restricted_heuristic_mode) {
        result.notes.push_back("Restricted heuristic mode is enabled; global optimality will not be claimed before full activation.");
    }
    if (options.candidate_maintenance_policy == "benders-coefficients") {
        result.notes.push_back("Phase 1P Benders-coefficient active-set maintenance is enabled for heuristic mode.");
        result.notes.push_back("Selected firebreaks are protected from deactivation by default; deactivation uses upper-bound changes only.");
    }
    if (uses_tail_aware_candidate_scoring(options)) {
        result.notes.push_back(
            "Phase 1S CVaR-tail-aware blended Benders scoring is enabled for restricted FPP-CVaR heuristic maintenance only.");
        result.notes.push_back(
            "Tail-aware scoring changes candidate activation/deactivation choices but does not change the FPP-CVaR objective, cuts, or feasibility logic.");
        result.notes.push_back(
            "Tail-score diagnostics are auto-enabled for cvar-tail-blend mode.");
    }
    if (burn_frequency_score_available) {
        result.notes.push_back("Burn-frequency candidate scores were computed from no-firebreak scenario reachability.");
    }
    if (result.global_time_budget_enabled) {
        result.notes.push_back(
            "Restricted-candidate staged solves share one global wall-clock time budget.");
    }
    if (options.export_tail_score_diagnostics) {
        result.notes.push_back(
            "Phase 1Q CVaR-tail score diagnostics are enabled; they do not change activation or deactivation decisions.");
    }

    auto refresh_elapsed_time = [&]() {
        result.elapsed_time_total_seconds = elapsed_seconds_since(global_start);
        if (result.global_time_budget_enabled &&
            result.elapsed_time_total_seconds >= result.global_time_limit_seconds) {
            result.time_budget_exhausted = true;
        }
    };

    auto stop_for_global_time_limit = [&](const std::string& reason) {
        refresh_elapsed_time();
        result.time_budget_exhausted = true;
        result.status = "RestrictedTimeLimit";
        result.reason_for_heuristic_stop = reason;
        result.global_optimality_certified = false;
        result.final_lower_bound_is_global = false;
        result.restricted_bound_is_global = false;
        result.stopped_before_full_activation = !manager.allActive();
        result.active_candidate_count_final = manager.activeCount();
        result.active_candidate_fraction_final = manager.activeFraction();
        result.active_candidate_fraction_at_stop = manager.activeFraction();
        result.eventually_activated_all = manager.allActive();
        result.candidate_rounds = static_cast<int>(result.round_log.size());
        result.activation_history = manager.activationHistory();
        result.notes.push_back(reason);
        update_cut_pool_diagnostics(result, cut_pool);
        update_subproblem_diagnostics(result, subproblem_manager);
    };

    struct BudgetedStageSolve {
        RestrictedStageSolveResult stage;
        double stage_time_limit_seconds = 0.0;
        double remaining_before = std::numeric_limits<double>::quiet_NaN();
        double remaining_after = std::numeric_limits<double>::quiet_NaN();
        int cut_pool_size_before = 0;
        int new_cuts_added_to_pool = 0;
        int duplicate_cuts_skipped = 0;
        bool skipped_for_time_limit = false;
    };

    auto solve_and_pool_stage = [&](
        const std::vector<double>& upper_bounds,
        const std::string& stage_name,
        int round_index,
        int active_candidate_count) {
        BudgetedStageSolve timed;
        timed.cut_pool_size_before = cut_pool.size();
        const int duplicate_cuts_before = cut_pool.duplicateCutsSkipped();
        if (result.global_time_budget_enabled) {
            timed.remaining_before = remaining_global_time(options, global_start);
            if (global_time_budget_exhausted(options, global_start)) {
                timed.skipped_for_time_limit = true;
                stop_for_global_time_limit(
                    "Restricted-candidate global time budget exhausted before stage " + stage_name + ".");
                return timed;
            }
        }

        FppRestrictedCandidateBranchBendersOptions stage_options =
            result.global_time_budget_enabled
                ? options_for_stage_time_limit(options, timed.remaining_before)
                : options;
        timed.stage_time_limit_seconds = stage_options.time_limit_seconds;

        timed.stage = solve_stage(
            opt,
            stage_options,
            upper_bounds,
            stage_name,
            cut_pool,
            subproblem_manager);
        normalize_stage_time_limit_status(
            timed.stage.model_result,
            timed.stage_time_limit_seconds);

        ++result.master_model_build_count;
        result.master_model_rebuild_count =
            std::max(0, result.master_model_build_count - 1);
        result.master_total_build_time += timed.stage.master_model_build_time;
        result.master_total_bound_update_time +=
            timed.stage.master_bound_update_time;
        result.master_total_cut_insertion_time +=
            timed.stage.master_cut_insertion_time;
        result.master_cut_insertions += timed.stage.preloaded_cuts_added;

        timed.new_cuts_added_to_pool = cut_pool.addCuts(
            timed.stage.generated_cuts,
            round_index,
            stage_name,
            active_candidate_count);
        timed.duplicate_cuts_skipped =
            cut_pool.duplicateCutsSkipped() - duplicate_cuts_before;
        result.master_duplicate_cut_insertions_skipped =
            cut_pool.duplicateCutsSkipped();
        update_cut_pool_diagnostics(result, cut_pool);
        update_subproblem_diagnostics(result, subproblem_manager);

        refresh_elapsed_time();
        if (result.global_time_budget_enabled) {
            timed.remaining_after = remaining_global_time(options, global_start);
            if (timed.remaining_after <= 1.0e-6) {
                result.time_budget_exhausted = true;
            }
        }
        return timed;
    };

    std::vector<std::pair<int, double>> latest_scenario_losses_by_id;

    if (options.solve_restricted_stage) {
        const auto active_before_round = manager.activeCandidates();
        bounds.apply(manager);
        const auto restricted_stage = solve_and_pool_stage(
            bounds.upperBounds(),
            "restricted",
            0,
            manager.activeCount());
        if (restricted_stage.skipped_for_time_limit) {
            return result;
        }
        result.restricted_stage_result = restricted_stage.stage.model_result;
        result.last_restricted_stage_result = result.restricted_stage_result;
        result.restricted_stage_status = result.restricted_stage_result.status;
        result.restricted_stage_objective = result.restricted_stage_result.objective_value;
        result.restricted_stage_lazy_cuts =
            result.restricted_stage_result.branch_benders_lazy_cuts_added;
        result.restricted_initial_stage_runtime =
            result.restricted_stage_result.runtime_seconds;
        latest_scenario_losses_by_id = restricted_stage.stage.scenario_losses_by_id;
        result.active_candidate_count_after_restricted_stage = manager.activeCount();
        auto log = make_round_log(
            0,
            "initial-restricted",
            result.risk_measure,
            manager.activeCount(),
            manager.candidateCount(),
            {},
            "initial",
            {},
            result.restricted_stage_result,
            restricted_stage.stage_time_limit_seconds,
            restricted_stage.remaining_before,
            restricted_stage.remaining_after,
            restricted_stage.cut_pool_size_before,
            restricted_stage.new_cuts_added_to_pool,
            restricted_stage.duplicate_cuts_skipped);
        append_tail_score_diagnostics_if_enabled(
            result,
            opt,
            options,
            cut_pool,
            restricted_stage.stage,
            log,
            active_before_round,
            manager.activeCandidates());
        result.round_log.push_back(std::move(log));
        if (options.candidate_maintenance_policy != "none") {
            maintenance_tracker.completeRound(manager);
        }
        if (result.time_budget_exhausted && !manager.allActive()) {
            stop_for_global_time_limit(
                "Restricted-candidate global time budget exhausted after initial restricted stage.");
            return result;
        }
    }

    result.restricted_lower_bound_is_global = false;

    if (options.solve_restricted_stage &&
        options.activation_policy == "burn-frequency" &&
        effective_candidate_round_limit(options) > 0 &&
        options.activation_batch_size > 0) {
        if (!burn_frequency_score_available) {
            BurnFrequencyCandidateScorer scorer;
            burn_frequency_scores = scorer.scoreCandidates(opt);
            burn_frequency_score_available = true;
            result.burn_frequency_score_available = true;
            result.top_burn_frequency_candidates = topBurnFrequencyCandidates(
                burn_frequency_scores,
                static_cast<int>(std::min<std::size_t>(10, burn_frequency_scores.size())));
        }

        for (int activation_round = 0;
             activation_round < effective_candidate_round_limit(options) && !manager.allActive();
             ++activation_round) {
            const auto active_before_round = manager.activeCandidates();
            const auto top_scores = top_inactive_scores(
                manager,
                burn_frequency_scores,
                options.activation_batch_size);
            const auto activated = manager.activateTopK(
                burn_frequency_scores,
                options.activation_batch_size);
            if (activated.empty()) {
                break;
            }
            result.candidates_activated_by_burn_frequency.insert(
                result.candidates_activated_by_burn_frequency.end(),
                activated.begin(),
                activated.end());

            bounds.apply(manager);
            const std::string stage_name =
                "burn_frequency_round_" + std::to_string(activation_round + 1);
            const auto stage = solve_and_pool_stage(
                bounds.upperBounds(),
                stage_name,
                static_cast<int>(result.round_log.size()),
                manager.activeCount());
            if (stage.skipped_for_time_limit) {
                return result;
            }
            result.last_restricted_stage_result = stage.stage.model_result;
            latest_scenario_losses_by_id = stage.stage.scenario_losses_by_id;
            result.restricted_stage_cuts_reused += stage.stage.preloaded_cuts_added;
            result.restricted_activation_stage_runtime_total +=
                stage.stage.model_result.runtime_seconds;
            auto log = make_round_log(
                static_cast<int>(result.round_log.size()),
                "activation",
                result.risk_measure,
                manager.activeCount(),
                manager.candidateCount(),
                activated,
                "burn-frequency",
                top_scores,
                stage.stage.model_result,
                stage.stage_time_limit_seconds,
                stage.remaining_before,
                stage.remaining_after,
                stage.cut_pool_size_before,
                stage.new_cuts_added_to_pool,
                stage.duplicate_cuts_skipped);
            append_tail_score_diagnostics_if_enabled(
                result,
                opt,
                options,
                cut_pool,
                stage.stage,
                log,
                active_before_round,
                manager.activeCandidates());
            result.round_log.push_back(std::move(log));
            if (result.time_budget_exhausted && !manager.allActive()) {
                stop_for_global_time_limit(
                    "Restricted-candidate global time budget exhausted after burn-frequency activation stage.");
                return result;
            }
        }
        result.active_candidate_count_after_restricted_stage = manager.activeCount();
    }

    if (options.solve_restricted_stage &&
        options.candidate_maintenance_policy == "benders-coefficients" &&
        effective_candidate_round_limit(options) > 0 &&
        options.activation_batch_size > 0) {
        BendersCoefficientCandidateScorer scorer;
        CvarTailAwareBendersCandidateScorer tail_scorer;
        const bool tail_aware_scoring = uses_tail_aware_candidate_scoring(options);
        const int tail_protection_size = effective_tail_protection_size(options);
        const auto probability_by_id = scenario_probability_by_id(opt);
        for (int activation_round = 0;
             activation_round < effective_candidate_round_limit(options) && !manager.allActive();
             ++activation_round) {
            const auto active_before_round = manager.activeCandidates();
            RestrictedCandidateMaintenanceDecision decision;
            decision.maintenance_round = maintenance_tracker.currentRound();
            decision.active_count_before_maintenance = manager.activeCount();

            BendersCoefficientScoringSummary inactive_summary;
            CvarTailAwareBendersScoringSummary inactive_tail_summary;
            std::vector<std::pair<int, double>> raw_activation_scores;
            int activation_cuts_used = 0;
            int activation_nonzero_coefficients = 0;
            if (tail_aware_scoring) {
                inactive_tail_summary = tail_scorer.scoreCandidates(
                    manager.candidateCount(),
                    opt.eligible_indices,
                    manager.inactiveCandidates(),
                    cut_pool.cuts(),
                    latest_scenario_losses_by_id,
                    risk_config.cvarBeta,
                    options.candidate_tail_score_gamma,
                    probability_by_id);
                raw_activation_scores = inactive_tail_summary.blend_scores;
                activation_cuts_used = inactive_tail_summary.cuts_used;
                activation_nonzero_coefficients =
                    inactive_tail_summary.nonzero_generic_coefficients;
            } else {
                inactive_summary = scorer.scoreInactiveCandidates(
                    manager.candidateCount(),
                    opt.eligible_indices,
                    manager.inactiveCandidates(),
                    cut_pool.cuts(),
                    probability_by_id);
                raw_activation_scores = inactive_summary.scores;
                activation_cuts_used = inactive_summary.cuts_used;
                activation_nonzero_coefficients =
                    inactive_summary.nonzero_inactive_coefficients;
            }
            auto activation_scores = maintenance_tracker.filterActivationScores(
                raw_activation_scores,
                maintenance_options.reactivation_cooldown_rounds,
                decision);

            result.benders_coefficient_scores_available = true;
            result.number_of_cuts_used_for_activation = activation_cuts_used;
            result.number_of_nonzero_inactive_coefficients =
                activation_nonzero_coefficients;
            result.top_benders_coefficient_candidates = tail_aware_scoring
                ? topCvarTailAwareCandidates(
                      inactive_tail_summary.generic_scores,
                      static_cast<int>(std::min<std::size_t>(
                          10,
                          inactive_tail_summary.generic_scores.size())))
                : topBendersCoefficientCandidates(
                      inactive_summary.scores,
                      static_cast<int>(std::min<std::size_t>(
                          10,
                          inactive_summary.scores.size())));
            result.max_benders_coefficient_score = 0.0;
            result.avg_benders_coefficient_score = 0.0;
            if (!raw_activation_scores.empty()) {
                double total_score = 0.0;
                result.max_benders_coefficient_score =
                    -std::numeric_limits<double>::infinity();
                for (const auto& [candidate, score] : raw_activation_scores) {
                    (void)candidate;
                    result.max_benders_coefficient_score =
                        std::max(result.max_benders_coefficient_score, score);
                    total_score += score;
                }
                result.avg_benders_coefficient_score =
                    total_score / static_cast<double>(raw_activation_scores.size());
            }

            const auto top_scores = tail_aware_scoring
                ? topCvarTailAwareCandidates(
                      activation_scores,
                      options.activation_batch_size)
                : topBendersCoefficientCandidates(
                      activation_scores,
                      options.activation_batch_size);
            for (const auto& [candidate, score] : top_scores) {
                (void)score;
                decision.top_activation_candidates.push_back(candidate);
            }
            decision.top_activation_scores = top_scores;

            const auto activated = manager.activateTopK(
                activation_scores,
                options.activation_batch_size);
            decision.activated = activated;
            maintenance_tracker.recordActivated(activated);
            decision.active_count_after_activation = manager.activeCount();
            result.candidates_activated_by_benders_coefficients.insert(
                result.candidates_activated_by_benders_coefficients.end(),
                activated.begin(),
                activated.end());
            if (tail_aware_scoring) {
                result.activated_by_tail_blend_count +=
                    static_cast<int>(activated.size());
            }

            const int excess =
                std::max(0, manager.activeCount() - maintenance_options.max_active_size);
            const int requested_deactivation_count =
                std::min(maintenance_options.deactivation_batch_size, excess);
            BendersCoefficientScoringSummary active_summary;
            CvarTailAwareBendersScoringSummary active_tail_summary;
            std::vector<std::pair<int, double>> deactivation_scores;
            std::vector<int> tail_protected_candidates;
            std::vector<std::pair<int, double>> top_tail_scores;
            if (tail_aware_scoring) {
                active_tail_summary = tail_scorer.scoreCandidates(
                    manager.candidateCount(),
                    opt.eligible_indices,
                    manager.activeCandidates(),
                    cut_pool.cuts(),
                    latest_scenario_losses_by_id,
                    risk_config.cvarBeta,
                    options.candidate_tail_score_gamma,
                    probability_by_id);
                deactivation_scores = active_tail_summary.blend_scores;
                top_tail_scores = topCvarTailAwareCandidates(
                    active_tail_summary.tail_scores,
                    tail_protection_size);
                tail_protected_candidates = candidate_ids_from_scores(top_tail_scores);
            } else {
                active_summary = scorer.scoreCandidates(
                    manager.candidateCount(),
                    opt.eligible_indices,
                    manager.activeCandidates(),
                    cut_pool.cuts(),
                    probability_by_id);
                deactivation_scores = active_summary.scores;
            }
            const auto selected_candidates = candidate_ids_from_selected_compact_indices(
                opt,
                result.last_restricted_stage_result.selected_firebreak_indices);
            const auto deactivation_candidates =
                maintenance_tracker.selectDeactivationCandidates(
                    manager,
                    deactivation_scores,
                    selected_candidates,
                    activated,
                    tail_protected_candidates,
                    maintenance_options,
                    requested_deactivation_count,
                    decision);
            const auto deactivated = manager.deactivateCandidates(deactivation_candidates);
            decision.deactivated = deactivated;
            maintenance_tracker.recordDeactivated(deactivated);
            decision.active_count_after_deactivation = manager.activeCount();
            decision.oscillation_event_count = maintenance_tracker.totalOscillationEvents();
            const auto top_tail_activation_candidates = tail_aware_scoring
                ? candidate_ids_from_scores(topCvarTailAwareCandidates(
                      inactive_tail_summary.tail_scores,
                      options.activation_batch_size))
                : std::vector<int>();
            const auto top_tail_protected_candidates =
                candidate_ids_from_scores(top_tail_scores);
            if (tail_aware_scoring) {
                result.activated_tail_top_k_overlap +=
                    overlap_count(activated, top_tail_activation_candidates);
                result.deactivated_tail_top_k_warning_count +=
                    overlap_count(deactivated, top_tail_protected_candidates);
            }

            if (activated.empty() && deactivated.empty()) {
                break;
            }

            bounds.apply(manager);
            const std::string stage_name =
                "benders_maintenance_round_" + std::to_string(activation_round + 1);
            const auto stage = solve_and_pool_stage(
                bounds.upperBounds(),
                stage_name,
                static_cast<int>(result.round_log.size()),
                manager.activeCount());
            if (stage.skipped_for_time_limit) {
                return result;
            }
            result.last_restricted_stage_result = stage.stage.model_result;
            latest_scenario_losses_by_id = stage.stage.scenario_losses_by_id;
            result.restricted_stage_cuts_reused += stage.stage.preloaded_cuts_added;
            result.restricted_activation_stage_runtime_total +=
                stage.stage.model_result.runtime_seconds;

            auto log = make_round_log(
                static_cast<int>(result.round_log.size()),
                "maintenance",
                result.risk_measure,
                manager.activeCount(),
                manager.candidateCount(),
                activated,
                "benders-coefficients-maintenance",
                top_scores,
                stage.stage.model_result,
                stage.stage_time_limit_seconds,
                stage.remaining_before,
                stage.remaining_after,
                stage.cut_pool_size_before,
                stage.new_cuts_added_to_pool,
                stage.duplicate_cuts_skipped,
                activation_cuts_used,
                activation_nonzero_coefficients);
            log.candidate_score_mode = options.candidate_score_mode;
            log.candidate_tail_score_gamma = options.candidate_tail_score_gamma;
            log.candidate_tail_protection_size = tail_protection_size;
            if (tail_aware_scoring) {
                const int diagnostic_top_k = std::max(
                    1,
                    std::min(
                        static_cast<int>(std::max<std::size_t>(
                            inactive_tail_summary.blend_scores.size(),
                            inactive_tail_summary.tail_scores.size())),
                        std::max(10, options.activation_batch_size)));
                log.top_blend_candidates = topCvarTailAwareCandidates(
                    inactive_tail_summary.blend_scores,
                    diagnostic_top_k);
                log.top_generic_candidates_for_score_mode =
                    topCvarTailAwareCandidates(
                        inactive_tail_summary.generic_scores,
                        diagnostic_top_k);
                log.top_tail_candidates = topCvarTailAwareCandidates(
                    inactive_tail_summary.tail_scores,
                    diagnostic_top_k);
                log.top_blend_tail_overlap = overlap_count(
                    candidate_ids_from_scores(log.top_blend_candidates),
                    candidate_ids_from_scores(log.top_tail_candidates));
                log.top_blend_generic_overlap = overlap_count(
                    candidate_ids_from_scores(log.top_blend_candidates),
                    candidate_ids_from_scores(log.top_generic_candidates_for_score_mode));
                log.activated_tail_top_k_overlap =
                    overlap_count(activated, top_tail_activation_candidates);
                log.deactivated_tail_top_k_warning_count =
                    overlap_count(deactivated, top_tail_protected_candidates);
            }
            attach_maintenance_decision(log, decision);
            append_tail_score_diagnostics_if_enabled(
                result,
                opt,
                options,
                cut_pool,
                stage.stage,
                log,
                active_before_round,
                manager.activeCandidates());
            result.round_log.push_back(log);
            record_maintenance_diagnostics(result, decision, maintenance_tracker);
            maintenance_tracker.completeRound(manager);

            if (result.time_budget_exhausted && !manager.allActive()) {
                stop_for_global_time_limit(
                    "Restricted-candidate global time budget exhausted after Benders-coefficient maintenance stage.");
                return result;
            }
        }
        result.active_candidate_count_after_restricted_stage = manager.activeCount();
    }

    if (options.solve_restricted_stage &&
        options.candidate_maintenance_policy == "none" &&
        options.activation_policy == "benders-coefficients" &&
        effective_candidate_round_limit(options) > 0 &&
        options.activation_batch_size > 0) {
        BendersCoefficientCandidateScorer scorer;
        const auto probability_by_id = scenario_probability_by_id(opt);
        for (int activation_round = 0;
             activation_round < effective_candidate_round_limit(options) && !manager.allActive();
             ++activation_round) {
            const auto active_before_round = manager.activeCandidates();
            const auto summary = scorer.scoreInactiveCandidates(
                manager.candidateCount(),
                opt.eligible_indices,
                manager.inactiveCandidates(),
                cut_pool.cuts(),
                probability_by_id);
            result.benders_coefficient_scores_available = true;
            result.number_of_cuts_used_for_activation = summary.cuts_used;
            result.number_of_nonzero_inactive_coefficients =
                summary.nonzero_inactive_coefficients;
            result.max_benders_coefficient_score = summary.max_score;
            result.avg_benders_coefficient_score = summary.average_score;
            result.top_benders_coefficient_candidates =
                topBendersCoefficientCandidates(
                    summary.scores,
                    static_cast<int>(std::min<std::size_t>(10, summary.scores.size())));

            const auto top_scores = topBendersCoefficientCandidates(
                summary.scores,
                options.activation_batch_size);
            const auto activated = manager.activateTopK(
                summary.scores,
                options.activation_batch_size);
            if (activated.empty()) {
                break;
            }
            result.candidates_activated_by_benders_coefficients.insert(
                result.candidates_activated_by_benders_coefficients.end(),
                activated.begin(),
                activated.end());

            bounds.apply(manager);
            const std::string stage_name =
                "benders_coefficients_round_" + std::to_string(activation_round + 1);
            const auto stage = solve_and_pool_stage(
                bounds.upperBounds(),
                stage_name,
                static_cast<int>(result.round_log.size()),
                manager.activeCount());
            if (stage.skipped_for_time_limit) {
                return result;
            }
            result.last_restricted_stage_result = stage.stage.model_result;
            latest_scenario_losses_by_id = stage.stage.scenario_losses_by_id;
            result.restricted_stage_cuts_reused += stage.stage.preloaded_cuts_added;
            result.restricted_activation_stage_runtime_total +=
                stage.stage.model_result.runtime_seconds;
            auto log = make_round_log(
                static_cast<int>(result.round_log.size()),
                "activation",
                result.risk_measure,
                manager.activeCount(),
                manager.candidateCount(),
                activated,
                "benders-coefficients",
                top_scores,
                stage.stage.model_result,
                stage.stage_time_limit_seconds,
                stage.remaining_before,
                stage.remaining_after,
                stage.cut_pool_size_before,
                stage.new_cuts_added_to_pool,
                stage.duplicate_cuts_skipped,
                summary.cuts_used,
                summary.nonzero_inactive_coefficients);
            append_tail_score_diagnostics_if_enabled(
                result,
                opt,
                options,
                cut_pool,
                stage.stage,
                log,
                active_before_round,
                manager.activeCandidates());
            result.round_log.push_back(std::move(log));
            if (result.time_budget_exhausted && !manager.allActive()) {
                stop_for_global_time_limit(
                    "Restricted-candidate global time budget exhausted after Benders-coefficient activation stage.");
                return result;
            }
        }
        result.active_candidate_count_after_restricted_stage = manager.activeCount();
    }

    if (!options.eventually_activate_all) {
        const FppRestrictedCandidateRoundLog* last_round =
            result.round_log.empty() ? nullptr : &result.round_log.back();
        const std::string last_status =
            last_round == nullptr ? result.restricted_stage_status : last_round->solve_status;
        result.restricted_objective =
            last_round == nullptr ? result.restricted_stage_objective : last_round->objective;
        result.restricted_best_bound =
            last_round == nullptr
                ? result.restricted_stage_result.best_bound
                : last_round->best_bound;
        result.restricted_bound_is_global = false;
        result.stopped_before_full_activation = !manager.allActive();
        result.global_optimality_certified = false;
        result.active_candidate_fraction_at_stop = manager.activeFraction();
        if (options.restricted_heuristic_mode) {
            result.reason_for_heuristic_stop =
                result.time_budget_exhausted
                    ? "global-time-budget-exhausted"
                    : (options.stop_after_candidate_rounds >= 0
                    ? "stop-after-candidate-rounds"
                    : "restricted-heuristic-mode");
            result.status = (result.time_budget_exhausted || status_is_time_limit(last_status))
                ? "RestrictedTimeLimit"
                : "RestrictedHeuristic";
        } else {
            result.reason_for_heuristic_stop =
                result.time_budget_exhausted
                    ? "global-time-budget-exhausted"
                    : "eventual full activation disabled outside heuristic mode";
            result.status = (result.time_budget_exhausted || status_is_time_limit(last_status))
                ? "RestrictedTimeLimit"
                : (last_status == "Optimal"
                ? "RestrictedFeasible"
                : last_status);
        }
        result.candidate_rounds = static_cast<int>(result.round_log.size());
        result.active_candidate_count_final = manager.activeCount();
        result.active_candidate_fraction_final = manager.activeFraction();
        result.eventually_activated_all = manager.allActive();
        result.activation_history = manager.activationHistory();
        refresh_elapsed_time();
        update_cut_pool_diagnostics(result, cut_pool);
        update_subproblem_diagnostics(result, subproblem_manager);
        return result;
    }

    const auto active_before_full_activation = manager.activeCandidates();
    const std::size_t history_size_before_full_activation = manager.activationHistory().size();
    manager.activateAll();
    std::vector<int> full_activation_batch;
    if (manager.activationHistory().size() > history_size_before_full_activation) {
        full_activation_batch = manager.activationHistory().back().activated;
    }
    result.full_activation_performed = true;
    result.activation_history = manager.activationHistory();

    bounds.apply(manager);
    const auto final_stage = solve_and_pool_stage(
        bounds.upperBounds(),
        "full",
        static_cast<int>(result.round_log.size()),
        manager.activeCount());
    if (final_stage.skipped_for_time_limit) {
        return result;
    }
    result.final_stage_result = final_stage.stage.model_result;
    result.cuts_reused_in_full_stage = final_stage.stage.preloaded_cuts_added;
    result.restricted_final_full_stage_runtime =
        result.final_stage_result.runtime_seconds;
    result.restricted_final_stage_time_limit = final_stage.stage_time_limit_seconds;
    result.final_stage_status = result.final_stage_result.status;
    result.final_full_objective = result.final_stage_result.objective_value;
    result.final_stage_lazy_cuts = result.final_stage_result.branch_benders_lazy_cuts_added;
    result.active_candidate_count_final = manager.activeCount();
    result.active_candidate_fraction_final = manager.activeFraction();
    result.eventually_activated_all = manager.allActive();
    result.final_lower_bound_is_global =
        manager.allActive() &&
        final_result_is_globally_optimal(result.final_stage_result, options.tolerance);
    result.global_optimality_certified = result.final_lower_bound_is_global;
    result.restricted_objective = result.restricted_stage_objective;
    result.restricted_best_bound = result.restricted_stage_result.best_bound;
    result.restricted_bound_is_global = false;
    result.stopped_before_full_activation = false;
    result.active_candidate_fraction_at_stop = manager.activeFraction();
    result.status = result.final_lower_bound_is_global
        ? "Optimal"
        : ((result.time_budget_exhausted || status_is_time_limit(result.final_stage_result.status))
            ? "RestrictedTimeLimit"
            : result.final_stage_result.status);

    auto final_log = make_round_log(
        static_cast<int>(result.round_log.size()),
        "final-full",
        result.risk_measure,
        manager.activeCount(),
        manager.candidateCount(),
        full_activation_batch,
        "activate-all-final",
        {},
        result.final_stage_result,
        final_stage.stage_time_limit_seconds,
        final_stage.remaining_before,
        final_stage.remaining_after,
        final_stage.cut_pool_size_before,
        final_stage.new_cuts_added_to_pool,
        final_stage.duplicate_cuts_skipped);
    append_tail_score_diagnostics_if_enabled(
        result,
        opt,
        options,
        cut_pool,
        final_stage.stage,
        final_log,
        active_before_full_activation,
        manager.activeCandidates());
    result.round_log.push_back(std::move(final_log));
    result.candidate_rounds = static_cast<int>(result.round_log.size());
    refresh_elapsed_time();
    update_cut_pool_diagnostics(result, cut_pool);
    update_subproblem_diagnostics(result, subproblem_manager);

    return result;
}

}  // namespace firebreak::benders
