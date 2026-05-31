#include "benchmarks/StaticDpvMipBenchmark.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace firebreak::benchmarks {

namespace {

const opt::DpvNodeIndexSet& node_set_for_index(
    const opt::OptimizationScenario& scenario,
    int node_index) {
    if (node_index >= 0 && node_index < static_cast<int>(scenario.dpv.node_sets.size())) {
        const auto& candidate = scenario.dpv.node_sets[static_cast<std::size_t>(node_index)];
        if (candidate.node_index == node_index) {
            return candidate;
        }
    }

    for (const auto& node_set : scenario.dpv.node_sets) {
        if (node_set.node_index == node_index) {
            return node_set;
        }
    }

    throw std::runtime_error("Static-DPV-MIP could not find a DPV node set for a mapped node.");
}

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

double downstream_value_for_node(
    const StaticDpvMipOptions& options,
    int compact_index) {
    if (options.downstream_values_by_compact_index.empty()) {
        return 1.0;
    }
    return options.downstream_values_by_compact_index[static_cast<std::size_t>(compact_index)];
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
    result.all_scores.reserve(opt.eligible_indices.size());

    for (const int compact_index : opt.eligible_indices) {
        double dpv_score = 0.0;
        for (const auto& scenario : opt.scenarios) {
            const auto& node_set = node_set_for_index(scenario, compact_index);
            double scenario_score = 0.0;
            for (const int descendant_index : node_set.descendant_indices) {
                scenario_score += downstream_value_for_node(options, descendant_index);
            }
            dpv_score += scenario.probability * scenario_score;
        }

        result.all_scores.push_back(StaticDpvMipNodeScore{
            compact_index,
            opt.node_mapper.to_node(compact_index),
            dpv_score,
            treatment_loss_for_node(options, compact_index),
        });
    }

    std::sort(
        result.all_scores.begin(),
        result.all_scores.end(),
        [](const StaticDpvMipNodeScore& lhs, const StaticDpvMipNodeScore& rhs) {
            if (lhs.dpv_score != rhs.dpv_score) {
                return lhs.dpv_score > rhs.dpv_score;
            }
            return lhs.original_node < rhs.original_node;
        });

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

    const auto end = std::chrono::steady_clock::now();
    result.runtime_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

}  // namespace firebreak::benchmarks
