#pragma once

#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "benders/BendersCut.hpp"

namespace firebreak::benders {

struct TailScoreCandidateRank {
    int candidate_id = -1;
    double score = 0.0;
    int rank = 0;
};

struct TailScoreCandidateEventDiagnostic {
    int candidate_id = -1;
    std::string event_type;
    double generic_score = 0.0;
    double tail_empirical_score = 0.0;
    double tail_excess_score = 0.0;
    double recent_tail_score = 0.0;
    double tail_blend_score = 0.0;
    bool was_active_before = false;
    bool was_active_after = false;
    bool was_selected = false;
    bool was_protected = false;
    bool was_tail_protected = false;
    bool was_deactivated = false;
    bool was_activated = false;
    int rank_generic = 0;
    int rank_tail_empirical = 0;
    int rank_tail_excess = 0;
    int rank_tail_blend = 0;
    std::string warning;
};

struct TailScoreScenarioDiagnostic {
    int scenario_id = -1;
    double scenario_probability = 0.0;
    double weighted_loss = std::numeric_limits<double>::quiet_NaN();
    double weighted_var_threshold = std::numeric_limits<double>::quiet_NaN();
    bool tail_membership = false;
    double tail_excess = 0.0;
};

struct CvarTailScoreRoundDiagnostics {
    int round_index = 0;
    std::string risk_measure = "expected";
    double cvar_beta = 0.9;
    bool weighted = false;
    std::string weight_profile;
    std::string weight_map_hash;
    double risk_threshold = std::numeric_limits<double>::quiet_NaN();
    std::string tail_definition_used;
    int tail_scenario_count = 0;
    std::vector<int> tail_scenario_ids;
    std::vector<TailScoreScenarioDiagnostic> scenario_diagnostics;
    int candidate_count = 0;
    int active_count_before_round = 0;
    int active_count_after_round = 0;
    std::vector<int> activated_candidates;
    std::vector<int> deactivated_candidates;
    std::vector<int> selected_candidates;
    std::vector<int> protected_selected_candidates;
    std::vector<TailScoreCandidateRank> top_generic_candidates;
    std::vector<TailScoreCandidateRank> top_tail_empirical_candidates;
    std::vector<TailScoreCandidateRank> top_tail_excess_candidates;
    std::vector<TailScoreCandidateRank> top_recent_tail_candidates;
    std::vector<TailScoreCandidateRank> top_tail_blend_candidates;
    std::vector<TailScoreCandidateRank> bottom_generic_active_candidates;
    std::vector<TailScoreCandidateRank> bottom_tail_empirical_active_candidates;
    std::vector<TailScoreCandidateRank> bottom_tail_excess_active_candidates;
    int top_k_overlap_generic_tail = 0;
    int top_k_overlap_blend_tail = 0;
    int top_k_overlap_blend_generic = 0;
    int activated_tail_top_k_overlap = 0;
    int deactivated_tail_bottom_k_overlap = 0;
    int deactivated_tail_top_k_warning_count = 0;
    int deactivation_blocked_by_tail_protection_count = 0;
    int selected_tail_top_k_overlap = 0;
    double spearman_generic_tail = std::numeric_limits<double>::quiet_NaN();
    double pearson_generic_tail = std::numeric_limits<double>::quiet_NaN();
    std::vector<TailScoreCandidateEventDiagnostic> candidate_events;
    std::vector<std::string> notes;
};

struct CvarTailScoreDiagnosticsInput {
    int round_index = 0;
    std::string risk_measure = "expected";
    double cvar_beta = 0.9;
    bool weighted = false;
    std::string weight_profile;
    std::string weight_map_hash;
    double risk_threshold = std::numeric_limits<double>::quiet_NaN();
    int candidate_count = 0;
    std::vector<int> eligible_compact_indices;
    std::map<int, double> scenario_probability_by_id;
    std::vector<BendersCut> accumulated_cuts;
    std::vector<BendersCut> recent_cuts;
    std::vector<std::pair<int, double>> scenario_losses_by_id;
    std::vector<std::pair<int, double>> cvar_excess_by_id;
    std::vector<int> active_candidates_before_round;
    std::vector<int> active_candidates_after_round;
    std::vector<int> activated_candidates;
    std::vector<int> deactivated_candidates;
    std::vector<int> selected_candidates;
    std::vector<int> protected_selected_candidates;
    int top_k = 10;
    double tolerance = 1.0e-6;
};

int tailScoreTopKOverlap(
    const std::vector<TailScoreCandidateRank>& lhs,
    const std::vector<TailScoreCandidateRank>& rhs);

CvarTailScoreRoundDiagnostics computeCvarTailScoreRoundDiagnostics(
    const CvarTailScoreDiagnosticsInput& input);

}  // namespace firebreak::benders
