#include "benchmarks/StaticDpvBenchmark.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace firebreak::benchmarks {

namespace {

void validate_static_dpv_input(const opt::OptimizationInstance& opt, int budget) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("Static-DPV requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("Static-DPV requires at least one training scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("Static-DPV requires at least one eligible firebreak node.");
    }
    if (budget < 0) {
        throw std::runtime_error("Static-DPV budget must be nonnegative.");
    }
    if (budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("Static-DPV budget exceeds the number of eligible firebreak nodes.");
    }
    for (const auto& scenario : opt.scenarios) {
        if (!scenario.dpv.descendants_include_self || scenario.dpv.node_sets.empty()) {
            throw std::runtime_error(
                "Static-DPV requires DPV node sets built with closed reachability.");
        }
    }
}

}  // namespace

StaticDpvBenchmarkResult StaticDpvBenchmark::run(
    const opt::OptimizationInstance& opt,
    int budget,
    const StaticDpvBenchmarkOptions& options) const {
    validate_static_dpv_input(opt, budget);

    const auto start = std::chrono::steady_clock::now();

    StaticDpvBenchmarkResult result;
    opt::WeightedDpvScoringOptions scoring_options;
    scoring_options.variant = opt::WeightedDpvVariant::StaticClosedDescendantsTimesOutDegree;
    scoring_options.ignition_policy = options.ignition_policy;
    const auto score_report = opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        scoring_options);

    result.all_scores.reserve(score_report.candidate_scores.size());
    for (const auto& score : score_report.candidate_scores) {
        result.all_scores.push_back(StaticDpvNodeScore{
            score.compact_index,
            score.original_node,
            score.weighted_score,
        });
    }

    const auto selected_count = static_cast<std::size_t>(budget);
    result.selected_firebreak_indices.reserve(selected_count);
    result.selected_firebreak_original_nodes.reserve(selected_count);
    result.selected_scores.reserve(selected_count);

    for (std::size_t pos = 0; pos < selected_count; ++pos) {
        const auto& score = result.all_scores[pos];
        result.selected_firebreak_indices.push_back(score.compact_index);
        result.selected_firebreak_original_nodes.push_back(score.original_node);
        result.selected_scores.push_back(score.score);
        result.total_static_dpv_score += score.score;
    }
    result.dpv_variant = score_report.diagnostics.dpv_variant;
    result.dpv_structural_definition = score_report.diagnostics.dpv_structural_definition;
    result.dpv_ignition_policy = opt::weighted_dpv_ignition_policy_name(options.ignition_policy);
    result.dpv_weight_profile = score_report.diagnostics.dpv_weight_profile;
    result.dpv_weight_map_hash = score_report.diagnostics.dpv_weight_map_hash;
    result.dpv_candidates_scored = score_report.diagnostics.dpv_candidates;
    result.dpv_candidates_selected = static_cast<int>(result.selected_firebreak_indices.size());
    result.dpv_score_min = score_report.diagnostics.dpv_score_min;
    result.dpv_score_max = score_report.diagnostics.dpv_score_max;
    result.dpv_score_mean = score_report.diagnostics.dpv_score_mean;
    result.dpv_score_precompute_time_sec =
        score_report.diagnostics.dpv_precompute_time_sec +
        score_report.diagnostics.dpv_weighted_evaluation_time_sec;
    result.dpv_structural_cache_hit = score_report.diagnostics.dpv_structural_cache_hit;
    result.dpv_weighted_cache_hit = score_report.diagnostics.dpv_weighted_cache_hit;

    const auto end = std::chrono::steady_clock::now();
    result.runtime_seconds = std::chrono::duration<double>(end - start).count();
    result.dpv_selection_time_sec = result.runtime_seconds;
    return result;
}

}  // namespace firebreak::benchmarks
