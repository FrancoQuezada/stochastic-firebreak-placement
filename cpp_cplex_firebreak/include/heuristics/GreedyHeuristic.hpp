#pragma once

#include <string>
#include <vector>

#include "heuristics/GreedyMetrics.hpp"
#include "opt/OptimizationInstance.hpp"
#include "opt/WeightedDpvScoring.hpp"

namespace firebreak::heuristics {

struct GreedyResult {
    std::string method_name;
    std::string objective_metric;
    std::vector<int> selected_firebreak_indices;
    std::vector<int> selected_firebreak_original_nodes;
    std::vector<double> selected_scores;
    double total_score = 0.0;
    double runtime_seconds = 0.0;
    std::vector<std::string> metric_notes;
    bool dpv_weighted = false;
    std::string dpv_variant;
    std::string dpv_structural_definition;
    std::string dpv_ignition_policy;
    std::string dpv_weight_profile;
    std::string dpv_weight_map_hash;
    std::string dpv_scenario_aggregation;
    std::string dpv_normalization;
    int dpv_candidates_scored = 0;
    int dpv_candidates_selected = 0;
    double dpv_score_min = 0.0;
    double dpv_score_max = 0.0;
    double dpv_score_mean = 0.0;
    double dpv_selected_score_sum = 0.0;
    bool dpv_structural_cache_hit = false;
    bool dpv_weighted_cache_hit = false;
    double dpv_score_precompute_time_sec = 0.0;
    double dpv_selection_time_sec = 0.0;
    double dpv_surrogate_objective = 0.0;
    int dpv_greedy_iterations = 0;
    int dpv_score_recomputations = 0;
    int dpv_marginal_scores_evaluated = 0;
    double dpv_overlap_value_removed = 0.0;
};

struct GreedyHeuristicOptions {
    opt::WeightedDpvIgnitionPolicy dpv_ignition_policy =
        opt::WeightedDpvIgnitionPolicy::LegacyIncludeReachable;
};

class GreedyHeuristic {
public:
    GreedyResult runGreedy(
        const opt::OptimizationInstance& opt,
        GreedyMetricType metric,
        bool recompute_each_iteration,
        bool verbose) const;

    GreedyResult runGreedy(
        const opt::OptimizationInstance& opt,
        GreedyMetricType metric,
        bool recompute_each_iteration,
        bool verbose,
        const GreedyHeuristicOptions& options) const;
};

}  // namespace firebreak::heuristics
