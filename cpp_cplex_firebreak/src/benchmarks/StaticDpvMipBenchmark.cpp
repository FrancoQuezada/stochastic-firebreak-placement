#include "benchmarks/StaticDpvMipBenchmark.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace firebreak::benchmarks {

namespace {

void validate_static_dpv_mip_input(
    const opt::OptimizationInstance& opt,
    int budget,
    const StaticDpvMipOptions& options) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("Static-DPV-MIP requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("Static-DPV-MIP requires at least one training scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("Static-DPV-MIP requires at least one eligible firebreak node.");
    }
    if (budget < 0) {
        throw std::runtime_error("Static-DPV-MIP budget must be nonnegative.");
    }
    if (budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error(
            "Static-DPV-MIP budget exceeds the number of eligible firebreak nodes.");
    }
    if (!options.downstream_values_by_compact_index.empty() &&
        options.downstream_values_by_compact_index.size() !=
            static_cast<std::size_t>(opt.node_mapper.size())) {
        throw std::runtime_error(
            "Static-DPV-MIP downstream value vector must match the compact node count.");
    }
    if (!options.treatment_loss_by_compact_index.empty() &&
        options.treatment_loss_by_compact_index.size() !=
            static_cast<std::size_t>(opt.node_mapper.size())) {
        throw std::runtime_error(
            "Static-DPV-MIP treatment-loss vector must match the compact node count.");
    }
    if (options.enable_treatment_loss_constraint) {
        throw std::runtime_error(
            "Static-DPV-MIP with treatment-loss constraints requires CPLEX support.");
    }
    for (const auto& scenario : opt.scenarios) {
        if (!scenario.dpv.descendants_include_self || scenario.dpv.node_sets.empty()) {
            throw std::runtime_error(
                "Static-DPV-MIP requires DPV node sets built with closed reachability.");
        }
    }
}

double treatment_loss_for_node(
    const StaticDpvMipOptions& options,
    int compact_index) {
    if (options.treatment_loss_by_compact_index.empty()) {
        return 0.0;
    }
    return options.treatment_loss_by_compact_index[static_cast<std::size_t>(compact_index)];
}

}  // namespace

StaticDpvMipBenchmarkResult StaticDpvMipBenchmark::run(
    const opt::OptimizationInstance& opt,
    int budget,
    const StaticDpvMipOptions& options) const {
    validate_static_dpv_mip_input(opt, budget, options);

    const auto start = std::chrono::steady_clock::now();

    StaticDpvMipBenchmarkResult result;
    result.num_variables = opt.eligible_indices.size();
    result.num_constraints = 1;
    opt::WeightedDpvScoringOptions scoring_options;
    scoring_options.variant = opt::WeightedDpvVariant::StaticClosedDescendants;
    scoring_options.ignition_policy = options.ignition_policy;
    const auto structural_data = opt::build_weighted_dpv_structural_data(
        opt,
        opt.eligible_indices,
        scoring_options);
    const auto compact_weights = options.downstream_values_by_compact_index.empty()
        ? opt::canonical_compact_dpv_weights_or_unit(opt)
        : options.downstream_values_by_compact_index;
    const auto score_report = opt::evaluate_weighted_dpv_scores(
        opt,
        structural_data,
        compact_weights,
        scoring_options);

    result.all_scores.reserve(score_report.candidate_scores.size());
    for (const auto& score : score_report.candidate_scores) {
        result.all_scores.push_back(StaticDpvMipNodeScore{
            score.compact_index,
            score.original_node,
            score.weighted_score,
            treatment_loss_for_node(options, score.compact_index),
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
        result.selected_scores.push_back(score.dpv_score);
        result.total_static_dpv_score += score.dpv_score;
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
