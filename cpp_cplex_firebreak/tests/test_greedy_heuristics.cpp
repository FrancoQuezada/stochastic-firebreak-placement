#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "heuristics/CumulativePropagationGraph.hpp"
#include "heuristics/GreedyHeuristic.hpp"
#include "heuristics/GreedyMetrics.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance(
    const std::vector<int>& original_nodes,
    const std::vector<std::vector<firebreak::opt::CompactArc>>& scenario_arcs,
    int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = budget;
    opt.node_mapper.build_from_nodes(original_nodes);
    opt.eligible_original_nodes = original_nodes;
    for (const int original_node : original_nodes) {
        opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
    }

    const double probability = scenario_arcs.empty()
        ? 1.0
        : 1.0 / static_cast<double>(scenario_arcs.size());
    for (std::size_t i = 0; i < scenario_arcs.size(); ++i) {
        firebreak::opt::OptimizationScenario scenario;
        scenario.scenario_id = static_cast<int>(i + 1);
        scenario.probability = probability;
        scenario.ignition_index = 0;
        scenario.ignition_original_node = original_nodes.front();
        scenario.message_filename = "synthetic_" + std::to_string(i + 1) + ".csv";
        scenario.arcs = scenario_arcs[i];
        opt.total_arcs += scenario.arcs.size();
        opt.scenarios.push_back(std::move(scenario));
        opt.scenario_probabilities.push_back(probability);
    }
    return opt;
}

firebreak::opt::OptimizationInstance make_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    int budget) {
    return make_instance(original_nodes, std::vector<std::vector<firebreak::opt::CompactArc>>{arcs}, budget);
}

struct ReferenceCandidateScore {
    int compact_index = -1;
    int original_node = 0;
    double score = 0.0;
};

struct ReferenceResult {
    std::vector<int> selected_firebreak_indices;
    std::vector<int> selected_firebreak_original_nodes;
    std::vector<double> selected_scores;
    double total_score = 0.0;
};

ReferenceResult run_slow_reference(
    const firebreak::opt::OptimizationInstance& opt,
    firebreak::heuristics::GreedyMetricType metric) {
    firebreak::heuristics::CumulativePropagationGraph graph;
    graph.buildFromOptimizationInstance(opt);

    ReferenceResult result;
    std::unordered_set<int> selected;
    for (int step = 0; step < opt.budget; ++step) {
        std::vector<ReferenceCandidateScore> scores;
        for (const int candidate : opt.eligible_indices) {
            if (selected.find(candidate) != selected.end()) {
                continue;
            }
            scores.push_back(ReferenceCandidateScore{
                candidate,
                opt.node_mapper.to_node(candidate),
                firebreak::heuristics::scoreCandidate(graph, candidate, selected, metric),
            });
        }
        if (scores.empty()) {
            throw std::runtime_error("Slow reference ran out of candidates.");
        }
        std::sort(scores.begin(), scores.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            return lhs.original_node < rhs.original_node;
        });

        const auto& best = scores.front();
        selected.insert(best.compact_index);
        result.selected_firebreak_indices.push_back(best.compact_index);
        result.selected_firebreak_original_nodes.push_back(best.original_node);
        result.selected_scores.push_back(best.score);
        result.total_score += best.score;
    }
    return result;
}

void assert_scores_close(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    assert(lhs.size() == rhs.size());
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const double scale = std::max(1.0, std::max(std::fabs(lhs[i]), std::fabs(rhs[i])));
        assert(std::fabs(lhs[i] - rhs[i]) <= 1.0e-9 * scale);
    }
}

void assert_matches_reference(
    const firebreak::opt::OptimizationInstance& opt,
    firebreak::heuristics::GreedyMetricType metric) {
    firebreak::heuristics::GreedyHeuristic greedy;
    const auto optimized = greedy.runGreedy(opt, metric, true, false);
    const auto reference = run_slow_reference(opt, metric);

    assert(optimized.selected_firebreak_indices == reference.selected_firebreak_indices);
    assert(optimized.selected_firebreak_original_nodes == reference.selected_firebreak_original_nodes);
    assert_scores_close(optimized.selected_scores, reference.selected_scores);
    assert(std::fabs(optimized.total_score - reference.total_score) <=
           1.0e-9 * std::max(1.0, std::fabs(reference.total_score)));
}

void assert_all_metrics_match_reference(const firebreak::opt::OptimizationInstance& opt) {
    assert_matches_reference(opt, firebreak::heuristics::GreedyMetricType::DPV3);
    assert_matches_reference(opt, firebreak::heuristics::GreedyMetricType::DPV2);
    assert_matches_reference(opt, firebreak::heuristics::GreedyMetricType::Betweenness);
    assert_matches_reference(opt, firebreak::heuristics::GreedyMetricType::Closeness);
}

void test_chain_dpv3_selects_root() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);

    firebreak::heuristics::GreedyHeuristic greedy;
    const auto result = greedy.runGreedy(opt, firebreak::heuristics::GreedyMetricType::DPV3, true, false);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{1}));
}

void test_branch_dpv3_selects_root() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
    }, 1);

    firebreak::heuristics::GreedyHeuristic greedy;
    const auto result = greedy.runGreedy(opt, firebreak::heuristics::GreedyMetricType::DPV3, true, false);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{1}));
}

void test_tie_breaks_by_smaller_original_node() {
    const auto opt = make_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
    }, 1);

    firebreak::heuristics::GreedyHeuristic greedy;
    const auto result = greedy.runGreedy(opt, firebreak::heuristics::GreedyMetricType::DPV3, true, false);

    assert((result.selected_firebreak_original_nodes == std::vector<int>{1}));
}

void test_budget_two_selects_distinct_nodes() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 2);

    firebreak::heuristics::GreedyHeuristic greedy;
    const auto result = greedy.runGreedy(opt, firebreak::heuristics::GreedyMetricType::DPV3, true, false);

    assert(result.selected_firebreak_original_nodes.size() == 2);
    assert(result.selected_firebreak_original_nodes[0] == 1);
    assert(result.selected_firebreak_original_nodes[1] == 2);
}

void test_zero_budget_all_metrics() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 0);

    firebreak::heuristics::GreedyHeuristic greedy;
    for (const auto metric : {
             firebreak::heuristics::GreedyMetricType::DPV3,
             firebreak::heuristics::GreedyMetricType::DPV2,
             firebreak::heuristics::GreedyMetricType::Betweenness,
             firebreak::heuristics::GreedyMetricType::Closeness,
         }) {
        const auto result = greedy.runGreedy(opt, metric, true, false);
        assert(result.selected_firebreak_original_nodes.empty());
        assert(result.total_score == 0.0);
    }
}

void test_budget_exceeds_useful_positive_scores() {
    const auto opt = make_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
    }, 4);

    assert_all_metrics_match_reference(opt);
    firebreak::heuristics::GreedyHeuristic greedy;
    const auto result = greedy.runGreedy(opt, firebreak::heuristics::GreedyMetricType::DPV2, true, false);
    assert(result.selected_firebreak_original_nodes.size() == 4);
    std::unordered_set<int> selected(
        result.selected_firebreak_original_nodes.begin(),
        result.selected_firebreak_original_nodes.end());
    assert(selected.size() == 4);
}

void test_blocking_shared_successor_updates_outgoing_sums() {
    const auto opt = make_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
        firebreak::opt::CompactArc{2, 4, 3, 5},
    }, 3);

    assert_all_metrics_match_reference(opt);
}

void test_disconnected_graph_matches_reference() {
    const auto opt = make_instance({1, 2, 3, 4, 5, 6}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{3, 4, 4, 5},
    }, 3);

    assert_all_metrics_match_reference(opt);
}

void test_cycle_graph_matches_reference() {
    const auto opt = make_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 0, 3, 1},
        firebreak::opt::CompactArc{2, 3, 3, 4},
        firebreak::opt::CompactArc{3, 4, 4, 5},
    }, 3);

    assert_all_metrics_match_reference(opt);
}

void test_repeated_cumulative_arc_weights_match_reference() {
    const auto opt = make_instance({1, 2, 3, 4}, {
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
        },
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 3, 2, 4},
        },
        {
            firebreak::opt::CompactArc{2, 3, 3, 4},
        },
    }, 3);

    assert_all_metrics_match_reference(opt);
}

void test_betweenness_metric_note_documents_once_per_iteration() {
    const auto opt = make_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);

    firebreak::heuristics::GreedyHeuristic greedy;
    const auto result = greedy.runGreedy(
        opt,
        firebreak::heuristics::GreedyMetricType::Betweenness,
        true,
        false);
    bool found_note = false;
    for (const auto& note : result.metric_notes) {
        if (note.find("once per greedy iteration") != std::string::npos) {
            found_note = true;
        }
    }
    assert(found_note);
}

}  // namespace

int main() {
    test_chain_dpv3_selects_root();
    test_branch_dpv3_selects_root();
    test_tie_breaks_by_smaller_original_node();
    test_budget_two_selects_distinct_nodes();
    test_zero_budget_all_metrics();
    test_budget_exceeds_useful_positive_scores();
    test_blocking_shared_successor_updates_outgoing_sums();
    test_disconnected_graph_matches_reference();
    test_cycle_graph_matches_reference();
    test_repeated_cumulative_arc_weights_match_reference();
    test_betweenness_metric_note_documents_once_per_iteration();
    std::cout << "All greedy heuristic tests passed.\n";
    return 0;
}
