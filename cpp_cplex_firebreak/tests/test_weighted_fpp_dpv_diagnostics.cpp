#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <random>
#include <unordered_set>
#include <vector>

#include "opt/DpvIndexBuilder.hpp"
#include "opt/WeightedDpvScoring.hpp"

namespace {

firebreak::opt::OptimizationInstance make_random_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_weighted_dpv_diagnostics";
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5, 6});
    opt.eligible_indices = {0, 1, 2, 3, 4, 5};
    opt.eligible_original_nodes = {1, 2, 3, 4, 5, 6};
    opt.compact_cell_weights = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

    std::mt19937 rng(7);
    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 42;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    for (int i = 0; i < opt.node_mapper.size(); ++i) {
        scenario.observed_node_indices.push_back(i);
    }
    for (int u = 0; u < opt.node_mapper.size(); ++u) {
        for (int v = 0; v < opt.node_mapper.size(); ++v) {
            if (u == v) {
                continue;
            }
            if ((rng() % 5) == 0) {
                scenario.arcs.push_back(firebreak::opt::CompactArc{
                    u,
                    v,
                    opt.node_mapper.to_node(u),
                    opt.node_mapper.to_node(v),
                });
            }
        }
    }
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});
    scenario.arcs.push_back(firebreak::opt::CompactArc{2, 1, 3, 2});

    firebreak::opt::DpvIndexBuilder builder;
    scenario.dpv = builder.build_for_scenario(scenario, opt.node_mapper);
    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.num_pairs();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

std::vector<int> reference_closed_reachable(
    const firebreak::opt::OptimizationScenario& scenario,
    int source,
    int node_count) {
    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(node_count));
    for (const auto& arc : scenario.arcs) {
        adjacency[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
    }
    std::vector<char> visited(static_cast<std::size_t>(node_count), 0);
    std::queue<int> queue;
    std::vector<int> reachable;
    visited[static_cast<std::size_t>(source)] = 1;
    queue.push(source);
    while (!queue.empty()) {
        const int current = queue.front();
        queue.pop();
        reachable.push_back(current);
        for (const int next : adjacency[static_cast<std::size_t>(current)]) {
            if (!visited[static_cast<std::size_t>(next)]) {
                visited[static_cast<std::size_t>(next)] = 1;
                queue.push(next);
            }
        }
    }
    std::sort(reachable.begin(), reachable.end());
    return reachable;
}

void test_structural_sets_match_reference_and_diagnostics_are_populated() {
    const auto opt = make_random_instance();
    firebreak::opt::WeightedDpvScoringOptions options;
    options.ignition_policy = firebreak::opt::WeightedDpvIgnitionPolicy::LegacyIncludeReachable;
    const auto report = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        options);

    const auto& scenario_data = report.structural_data.scenarios.front();
    for (const auto& candidate : scenario_data.candidates) {
        const auto expected = reference_closed_reachable(
            opt.scenarios.front(),
            candidate.compact_index,
            opt.node_mapper.size());
        assert(candidate.valued_node_indices == expected);
    }

    assert(report.diagnostics.dpv_variant == "static_closed_descendants");
    assert(report.diagnostics.dpv_weighted);
    assert(report.diagnostics.dpv_scenarios == 1);
    assert(report.diagnostics.dpv_candidates == 6);
    assert(report.diagnostics.dpv_structural_sets_computed == 6);
    assert(report.diagnostics.dpv_total_valued_incidence > 0);
    assert(report.diagnostics.dpv_score_max >= report.diagnostics.dpv_score_min);
    assert(report.diagnostics.dpv_score_mean >= report.diagnostics.dpv_score_min - 1.0e-12);
    assert(!report.diagnostics.dpv_weight_map_hash.empty());
}

void test_deterministic_ranking_tie_breaks_by_original_node() {
    std::vector<firebreak::opt::WeightedDpvCandidateScore> scores = {
        firebreak::opt::WeightedDpvCandidateScore{2, 30, 1.0, 5.0, 0},
        firebreak::opt::WeightedDpvCandidateScore{1, 20, 1.0, 5.0 + 1.0e-13, 0},
        firebreak::opt::WeightedDpvCandidateScore{0, 10, 1.0, 1.0, 0},
    };
    const auto ranked = firebreak::opt::rank_weighted_dpv_scores(std::move(scores), 1.0e-12);
    assert(ranked[0].original_node == 20);
    assert(ranked[1].original_node == 30);
    assert(ranked[2].original_node == 10);
    assert(ranked[0].rank == 1);
    assert(ranked[1].rank == 2);
    assert(ranked[2].rank == 3);
}

}  // namespace

int main() {
    test_structural_sets_match_reference_and_diagnostics_are_populated();
    test_deterministic_ranking_tie_breaks_by_original_node();
    std::cout << "All weighted FPP DPV diagnostic tests passed.\n";
    return 0;
}
