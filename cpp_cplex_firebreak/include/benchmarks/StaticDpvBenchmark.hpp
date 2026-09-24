#pragma once

#include <string>
#include <vector>

#include "opt/WeightedDpvScoring.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::benchmarks {

struct StaticDpvBenchmarkOptions {
    opt::WeightedDpvIgnitionPolicy ignition_policy =
        opt::WeightedDpvIgnitionPolicy::LegacyIncludeReachable;
};

struct StaticDpvNodeScore {
    int compact_index = -1;
    int original_node = 0;
    double score = 0.0;
};

struct StaticDpvBenchmarkResult {
    std::vector<int> selected_firebreak_indices;
    std::vector<int> selected_firebreak_original_nodes;
    std::vector<double> selected_scores;
    std::vector<StaticDpvNodeScore> all_scores;
    double total_static_dpv_score = 0.0;
    double runtime_seconds = 0.0;
    std::string dpv_variant;
    std::string dpv_structural_definition;
    std::string dpv_ignition_policy;
    std::string dpv_weight_profile;
    std::string dpv_weight_map_hash;
    std::string dpv_scenario_aggregation = "scenario_probability_weighted_sum";
    std::string dpv_normalization = "none";
    int dpv_candidates_scored = 0;
    int dpv_candidates_selected = 0;
    double dpv_score_min = 0.0;
    double dpv_score_max = 0.0;
    double dpv_score_mean = 0.0;
    double dpv_score_precompute_time_sec = 0.0;
    double dpv_selection_time_sec = 0.0;
    bool dpv_structural_cache_hit = false;
    bool dpv_weighted_cache_hit = false;
};

class StaticDpvBenchmark {
public:
    StaticDpvBenchmarkResult run(
        const opt::OptimizationInstance& opt,
        int budget,
        const StaticDpvBenchmarkOptions& options = {}) const;
};

}  // namespace firebreak::benchmarks
