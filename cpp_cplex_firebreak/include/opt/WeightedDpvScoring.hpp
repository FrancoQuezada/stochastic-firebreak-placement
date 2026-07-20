#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::opt {

enum class WeightedDpvVariant {
    StaticClosedDescendants,
    StaticClosedDescendantsTimesOutDegree,
};

enum class WeightedDpvIgnitionPolicy {
    LegacyIncludeReachable,
    FppIgnitionNoProtection,
};

struct WeightedDpvScoringOptions {
    WeightedDpvVariant variant = WeightedDpvVariant::StaticClosedDescendants;
    WeightedDpvIgnitionPolicy ignition_policy = WeightedDpvIgnitionPolicy::FppIgnitionNoProtection;
    bool use_scenario_probabilities = true;
    std::string implementation_version = "weighted-dpv-phase7a-v1";
};

struct WeightedDpvCandidateStructure {
    int compact_index = -1;
    int original_node = 0;
    int successor_count = 0;
    std::vector<int> valued_node_indices;
};

struct WeightedDpvScenarioStructure {
    int scenario_id = 0;
    int scenario_position = -1;
    int ignition_index = -1;
    int ignition_original_node = 0;
    double probability = 0.0;
    std::vector<WeightedDpvCandidateStructure> candidates;
};

struct WeightedDpvStructuralCacheKey {
    std::string variant;
    std::string ignition_policy;
    std::string structural_definition;
    std::string candidate_universe_hash;
    std::string scenario_graph_hash;
    std::string implementation_version;
    std::string digest;
};

struct WeightedDpvStructuralData {
    std::vector<WeightedDpvScenarioStructure> scenarios;
    WeightedDpvStructuralCacheKey cache_key;
    std::size_t total_valued_incidence = 0;
    double precompute_time_sec = 0.0;
};

struct WeightedDpvNumericalCacheKey {
    std::string structural_digest;
    std::string weight_profile;
    std::string weight_map_hash;
    std::string probability_hash;
    std::string normalization_mode;
    std::string implementation_version;
    std::string digest;
};

struct WeightedDpvCandidateScore {
    int compact_index = -1;
    int original_node = 0;
    double unweighted_score = 0.0;
    double weighted_score = 0.0;
    int rank = 0;
};

struct WeightedDpvDiagnostics {
    std::string dpv_variant;
    bool dpv_weighted = true;
    std::string dpv_structural_definition;
    std::string dpv_weight_profile;
    std::string dpv_weight_map_hash;
    int dpv_scenarios = 0;
    int dpv_candidates = 0;
    std::size_t dpv_structural_sets_computed = 0;
    bool dpv_structural_cache_hit = false;
    bool dpv_weighted_cache_hit = false;
    std::size_t dpv_total_valued_incidence = 0;
    double dpv_score_min = 0.0;
    double dpv_score_max = 0.0;
    double dpv_score_mean = 0.0;
    int dpv_zero_score_candidates = 0;
    double dpv_precompute_time_sec = 0.0;
    double dpv_weighted_evaluation_time_sec = 0.0;
};

struct WeightedDpvScoreReport {
    WeightedDpvStructuralData structural_data;
    WeightedDpvNumericalCacheKey numerical_cache_key;
    std::vector<WeightedDpvCandidateScore> candidate_scores;
    WeightedDpvDiagnostics diagnostics;
};

std::string weighted_dpv_variant_name(WeightedDpvVariant variant);
std::string weighted_dpv_ignition_policy_name(WeightedDpvIgnitionPolicy policy);
WeightedDpvIgnitionPolicy parse_weighted_dpv_ignition_policy(const std::string& value);
std::string weighted_dpv_structural_definition(const WeightedDpvScoringOptions& options);

std::vector<double> canonical_compact_dpv_weights_or_unit(const OptimizationInstance& opt);

WeightedDpvStructuralData build_weighted_dpv_structural_data(
    const OptimizationInstance& opt,
    const std::vector<int>& candidate_compact_indices,
    const WeightedDpvScoringOptions& options);

WeightedDpvScoreReport evaluate_weighted_dpv_scores(
    const OptimizationInstance& opt,
    const WeightedDpvStructuralData& structural_data,
    const std::vector<double>& compact_weights,
    const WeightedDpvScoringOptions& options);

WeightedDpvScoreReport build_weighted_dpv_score_report(
    const OptimizationInstance& opt,
    const std::vector<int>& candidate_compact_indices,
    const WeightedDpvScoringOptions& options);

std::vector<WeightedDpvCandidateScore> rank_weighted_dpv_scores(
    std::vector<WeightedDpvCandidateScore> scores,
    double tolerance = 1.0e-12);

std::string hash_weighted_dpv_candidate_universe(
    const OptimizationInstance& opt,
    const std::vector<int>& candidate_compact_indices);

std::string hash_weighted_dpv_scenario_graphs(const OptimizationInstance& opt);
std::string hash_weighted_dpv_probabilities(const OptimizationInstance& opt);

}  // namespace firebreak::opt
