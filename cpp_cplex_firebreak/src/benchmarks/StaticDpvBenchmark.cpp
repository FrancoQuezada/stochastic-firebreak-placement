#include "benchmarks/StaticDpvBenchmark.hpp"

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

    throw std::runtime_error("Static-DPV could not find a DPV node set for a mapped node.");
}

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
    int budget) const {
    validate_static_dpv_input(opt, budget);

    const auto start = std::chrono::steady_clock::now();

    StaticDpvBenchmarkResult result;
    result.all_scores.reserve(opt.eligible_indices.size());

    for (const int compact_index : opt.eligible_indices) {
        double score = 0.0;
        for (const auto& scenario : opt.scenarios) {
            const auto& node_set = node_set_for_index(scenario, compact_index);
            const double out_degree = static_cast<double>(node_set.successor_indices.size());
            const double reach_size = static_cast<double>(node_set.descendant_indices.size());
            score += scenario.probability * out_degree * reach_size;
        }

        result.all_scores.push_back(StaticDpvNodeScore{
            compact_index,
            opt.node_mapper.to_node(compact_index),
            score,
        });
    }

    std::sort(
        result.all_scores.begin(),
        result.all_scores.end(),
        [](const StaticDpvNodeScore& lhs, const StaticDpvNodeScore& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
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
        result.selected_scores.push_back(score.score);
        result.total_static_dpv_score += score.score;
    }

    const auto end = std::chrono::steady_clock::now();
    result.runtime_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

}  // namespace firebreak::benchmarks
