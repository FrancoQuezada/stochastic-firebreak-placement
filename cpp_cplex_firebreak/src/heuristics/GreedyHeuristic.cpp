#include "heuristics/GreedyHeuristic.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "heuristics/CumulativePropagationGraph.hpp"

namespace firebreak::heuristics {

namespace {

struct WeightedArc {
    int node = -1;
    double weight = 0.0;
    double inverse_cost = 0.0;
};

struct CandidateChoice {
    int compact_index = -1;
    int original_node = 0;
    double score = 0.0;
    bool found = false;
};

void validate_input(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("Greedy heuristic requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("Greedy heuristic requires at least one training scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("Greedy heuristic requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0) {
        throw std::runtime_error("Greedy heuristic budget must be nonnegative.");
    }
    if (opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("Greedy heuristic budget exceeds the number of eligible firebreak nodes.");
    }
}

bool better_choice(double score, int original_node, const CandidateChoice& incumbent) {
    if (!incumbent.found) {
        return true;
    }
    if (score != incumbent.score) {
        return score > incumbent.score;
    }
    return original_node < incumbent.original_node;
}

class LowMemoryGreedyScorer {
public:
    LowMemoryGreedyScorer(
        const opt::OptimizationInstance& opt,
        const CumulativePropagationGraph& graph)
        : opt_(opt),
          graph_(graph),
          node_count_(graph.numNodes()),
          forward_(static_cast<std::size_t>(node_count_)),
          reverse_(static_cast<std::size_t>(node_count_)),
          blocked_(static_cast<std::size_t>(node_count_), 0),
          eligible_(static_cast<std::size_t>(node_count_), 0),
          scores_(static_cast<std::size_t>(node_count_), 0.0),
          active_out_freq_(static_cast<std::size_t>(node_count_), 0.0),
          visit_stamp_(static_cast<std::size_t>(node_count_), 0),
          bfs_distance_(static_cast<std::size_t>(node_count_), 0),
          dijkstra_distance_(
              static_cast<std::size_t>(node_count_),
              std::numeric_limits<double>::infinity()),
          brandes_centrality_(static_cast<std::size_t>(node_count_), 0.0),
          brandes_distance_(static_cast<std::size_t>(node_count_), -1),
          brandes_sigma_(static_cast<std::size_t>(node_count_), 0.0),
          brandes_delta_(static_cast<std::size_t>(node_count_), 0.0),
          brandes_predecessors_(static_cast<std::size_t>(node_count_)) {
        for (const int candidate : opt_.eligible_indices) {
            validate_node(candidate, "eligible candidate");
            eligible_[static_cast<std::size_t>(candidate)] = 1;
        }
        build_weighted_adjacency();
    }

    void initializeScores(GreedyMetricType metric) {
        std::fill(scores_.begin(), scores_.end(), 0.0);
        for (const int candidate : opt_.eligible_indices) {
            scores_[static_cast<std::size_t>(candidate)] = scoreCandidateExact(candidate, metric);
        }
    }

    CandidateChoice bestFromCachedScores() const {
        return bestFromScores(scores_);
    }

    CandidateChoice bestFromScores(const std::vector<double>& scores) const {
        CandidateChoice best;
        for (const int candidate : opt_.eligible_indices) {
            if (blocked_[static_cast<std::size_t>(candidate)]) {
                continue;
            }
            const double score = scores[static_cast<std::size_t>(candidate)];
            const int original_node = opt_.node_mapper.to_node(candidate);
            if (better_choice(score, original_node, best)) {
                best.compact_index = candidate;
                best.original_node = original_node;
                best.score = score;
                best.found = true;
            }
        }
        return best;
    }

    void blockSelectedAndRefreshAffected(int selected, GreedyMetricType metric) {
        const auto affected = reverseReachableActive(selected);
        blockSelected(selected);
        for (const int node : affected) {
            if (!eligible_[static_cast<std::size_t>(node)]) {
                continue;
            }
            scores_[static_cast<std::size_t>(node)] =
                blocked_[static_cast<std::size_t>(node)] ? 0.0 : scoreCandidateExact(node, metric);
        }
    }

    void blockSelected(int selected) {
        validate_node(selected, "selected node");
        if (blocked_[static_cast<std::size_t>(selected)]) {
            return;
        }
        blocked_[static_cast<std::size_t>(selected)] = 1;
        scores_[static_cast<std::size_t>(selected)] = 0.0;
        for (const auto& incoming : reverse_[static_cast<std::size_t>(selected)]) {
            auto& value = active_out_freq_[static_cast<std::size_t>(incoming.node)];
            value -= incoming.weight;
            if (value < 0.0 && std::fabs(value) <= 1.0e-12) {
                value = 0.0;
            }
        }
    }

    const std::vector<double>& computeBetweennessScores() {
        std::fill(brandes_centrality_.begin(), brandes_centrality_.end(), 0.0);

        for (const int source : graph_.nodes()) {
            if (blocked_[static_cast<std::size_t>(source)]) {
                continue;
            }

            runBrandesBfs(source);

            std::fill(brandes_delta_.begin(), brandes_delta_.end(), 0.0);
            brandes_order_.clear();
            brandes_order_.reserve(static_cast<std::size_t>(node_count_));
            for (int node = 0; node < node_count_; ++node) {
                if (brandes_distance_[static_cast<std::size_t>(node)] >= 0) {
                    brandes_order_.push_back(node);
                }
            }
            std::sort(brandes_order_.begin(), brandes_order_.end(), [&](int lhs, int rhs) {
                return brandes_distance_[static_cast<std::size_t>(lhs)] >
                    brandes_distance_[static_cast<std::size_t>(rhs)];
            });

            for (const int w : brandes_order_) {
                for (const int v : brandes_predecessors_[static_cast<std::size_t>(w)]) {
                    if (brandes_sigma_[static_cast<std::size_t>(w)] > 0.0) {
                        brandes_delta_[static_cast<std::size_t>(v)] +=
                            (brandes_sigma_[static_cast<std::size_t>(v)] /
                             brandes_sigma_[static_cast<std::size_t>(w)]) *
                            (1.0 + brandes_delta_[static_cast<std::size_t>(w)]);
                    }
                }
                if (w != source) {
                    brandes_centrality_[static_cast<std::size_t>(w)] +=
                        brandes_delta_[static_cast<std::size_t>(w)];
                }
            }
        }

        return brandes_centrality_;
    }

private:
    const opt::OptimizationInstance& opt_;
    const CumulativePropagationGraph& graph_;
    int node_count_ = 0;
    std::vector<std::vector<WeightedArc>> forward_;
    std::vector<std::vector<WeightedArc>> reverse_;
    std::vector<char> blocked_;
    std::vector<char> eligible_;
    std::vector<double> scores_;
    std::vector<double> active_out_freq_;

    std::vector<int> visit_stamp_;
    int current_stamp_ = 0;
    std::vector<int> queue_;
    std::vector<int> affected_;
    std::vector<int> bfs_distance_;

    std::vector<double> dijkstra_distance_;
    std::vector<int> dijkstra_touched_;
    std::vector<std::pair<double, int>> dijkstra_heap_;

    std::vector<double> brandes_centrality_;
    std::vector<int> brandes_distance_;
    std::vector<double> brandes_sigma_;
    std::vector<double> brandes_delta_;
    std::vector<std::vector<int>> brandes_predecessors_;
    std::vector<int> brandes_queue_;
    std::vector<int> brandes_order_;

    void validate_node(int node, const char* context) const {
        if (node < 0 || node >= node_count_) {
            throw std::runtime_error(std::string("Greedy scorer found out-of-range ") + context + ".");
        }
    }

    void build_weighted_adjacency() {
        for (const int u : graph_.nodes()) {
            validate_node(u, "graph node");
            for (const int v : graph_.successors(u)) {
                validate_node(v, "successor node");
                const double weight = graph_.arcWeight(u, v);
                if (weight <= 0.0) {
                    continue;
                }
                const WeightedArc forward_arc{v, weight, 1.0 / weight};
                const WeightedArc reverse_arc{u, weight, 1.0 / weight};
                forward_[static_cast<std::size_t>(u)].push_back(forward_arc);
                reverse_[static_cast<std::size_t>(v)].push_back(reverse_arc);
                active_out_freq_[static_cast<std::size_t>(u)] += weight;
            }
        }
    }

    int nextStamp() {
        if (current_stamp_ == std::numeric_limits<int>::max()) {
            std::fill(visit_stamp_.begin(), visit_stamp_.end(), 0);
            current_stamp_ = 0;
        }
        return ++current_stamp_;
    }

    std::vector<int> reverseReachableActive(int selected) {
        validate_node(selected, "selected node");
        affected_.clear();
        if (blocked_[static_cast<std::size_t>(selected)]) {
            return affected_;
        }

        const int stamp = nextStamp();
        queue_.clear();
        visit_stamp_[static_cast<std::size_t>(selected)] = stamp;
        queue_.push_back(selected);

        for (std::size_t head = 0; head < queue_.size(); ++head) {
            const int current = queue_[head];
            affected_.push_back(current);
            for (const auto& incoming : reverse_[static_cast<std::size_t>(current)]) {
                const int predecessor = incoming.node;
                if (blocked_[static_cast<std::size_t>(predecessor)]) {
                    continue;
                }
                if (visit_stamp_[static_cast<std::size_t>(predecessor)] == stamp) {
                    continue;
                }
                visit_stamp_[static_cast<std::size_t>(predecessor)] = stamp;
                queue_.push_back(predecessor);
            }
        }
        return affected_;
    }

    double scoreCandidateExact(int candidate, GreedyMetricType metric) {
        validate_node(candidate, "candidate node");
        if (blocked_[static_cast<std::size_t>(candidate)]) {
            return 0.0;
        }

        switch (metric) {
            case GreedyMetricType::DPV3:
                return dpv3Score(candidate);
            case GreedyMetricType::DPV2:
                return dpv2Score(candidate);
            case GreedyMetricType::Closeness:
                return closenessScore(candidate);
            case GreedyMetricType::Betweenness:
                throw std::runtime_error("Betweenness scores are computed as a full vector per iteration.");
        }
        return 0.0;
    }

    double dpv2Score(int candidate) {
        const double outgoing_sum = active_out_freq_[static_cast<std::size_t>(candidate)];
        if (outgoing_sum <= 0.0) {
            return 0.0;
        }
        return outgoing_sum * static_cast<double>(closedReachableCount(candidate));
    }

    double dpv3Score(int candidate) {
        const double outgoing_sum = active_out_freq_[static_cast<std::size_t>(candidate)];
        if (outgoing_sum <= 0.0) {
            return 0.0;
        }
        return outgoing_sum * inverseWeightedDownstream(candidate);
    }

    int closedReachableCount(int source) {
        const int stamp = nextStamp();
        queue_.clear();
        visit_stamp_[static_cast<std::size_t>(source)] = stamp;
        queue_.push_back(source);

        int count = 0;
        for (std::size_t head = 0; head < queue_.size(); ++head) {
            const int current = queue_[head];
            ++count;
            for (const auto& arc : forward_[static_cast<std::size_t>(current)]) {
                const int next = arc.node;
                if (blocked_[static_cast<std::size_t>(next)]) {
                    continue;
                }
                if (visit_stamp_[static_cast<std::size_t>(next)] == stamp) {
                    continue;
                }
                visit_stamp_[static_cast<std::size_t>(next)] = stamp;
                queue_.push_back(next);
            }
        }
        return count;
    }

    double inverseWeightedDownstream(int source) {
        const double infinity = std::numeric_limits<double>::infinity();
        dijkstra_touched_.clear();
        dijkstra_heap_.clear();
        setDijkstraDistance(source, 0.0);
        dijkstra_heap_.push_back({0.0, source});
        std::push_heap(dijkstra_heap_.begin(), dijkstra_heap_.end(), dijkstraHeapCompare);

        double downstream = 0.0;
        while (!dijkstra_heap_.empty()) {
            std::pop_heap(dijkstra_heap_.begin(), dijkstra_heap_.end(), dijkstraHeapCompare);
            const auto [current_distance, current] = dijkstra_heap_.back();
            dijkstra_heap_.pop_back();
            if (current_distance != dijkstra_distance_[static_cast<std::size_t>(current)]) {
                continue;
            }

            downstream += 1.0 / (1.0 + current_distance);
            for (const auto& arc : forward_[static_cast<std::size_t>(current)]) {
                const int next = arc.node;
                if (blocked_[static_cast<std::size_t>(next)]) {
                    continue;
                }
                const double candidate_distance = current_distance + arc.inverse_cost;
                if (candidate_distance < dijkstra_distance_[static_cast<std::size_t>(next)]) {
                    setDijkstraDistance(next, candidate_distance);
                    dijkstra_heap_.push_back({candidate_distance, next});
                    std::push_heap(dijkstra_heap_.begin(), dijkstra_heap_.end(), dijkstraHeapCompare);
                }
            }
        }

        for (const int node : dijkstra_touched_) {
            dijkstra_distance_[static_cast<std::size_t>(node)] = infinity;
        }
        return downstream;
    }

    static bool dijkstraHeapCompare(
        const std::pair<double, int>& lhs,
        const std::pair<double, int>& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first > rhs.first;
        }
        return lhs.second > rhs.second;
    }

    void setDijkstraDistance(int node, double distance) {
        auto& current = dijkstra_distance_[static_cast<std::size_t>(node)];
        if (!std::isfinite(current)) {
            dijkstra_touched_.push_back(node);
        }
        current = distance;
    }

    double closenessScore(int candidate) {
        const int stamp = nextStamp();
        queue_.clear();
        visit_stamp_[static_cast<std::size_t>(candidate)] = stamp;
        bfs_distance_[static_cast<std::size_t>(candidate)] = 0;
        queue_.push_back(candidate);

        int reachable_count = 0;
        double distance_sum = 0.0;
        for (std::size_t head = 0; head < queue_.size(); ++head) {
            const int current = queue_[head];
            const int current_distance = bfs_distance_[static_cast<std::size_t>(current)];
            if (current_distance > 0) {
                ++reachable_count;
                distance_sum += static_cast<double>(current_distance);
            }
            for (const auto& arc : forward_[static_cast<std::size_t>(current)]) {
                const int next = arc.node;
                if (blocked_[static_cast<std::size_t>(next)]) {
                    continue;
                }
                if (visit_stamp_[static_cast<std::size_t>(next)] == stamp) {
                    continue;
                }
                visit_stamp_[static_cast<std::size_t>(next)] = stamp;
                bfs_distance_[static_cast<std::size_t>(next)] = current_distance + 1;
                queue_.push_back(next);
            }
        }

        if (reachable_count == 0 || distance_sum <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(reachable_count) / distance_sum;
    }

    void runBrandesBfs(int source) {
        std::fill(brandes_distance_.begin(), brandes_distance_.end(), -1);
        std::fill(brandes_sigma_.begin(), brandes_sigma_.end(), 0.0);
        brandes_queue_.clear();

        brandes_predecessors_[static_cast<std::size_t>(source)].clear();
        brandes_distance_[static_cast<std::size_t>(source)] = 0;
        brandes_sigma_[static_cast<std::size_t>(source)] = 1.0;
        brandes_queue_.push_back(source);

        for (std::size_t head = 0; head < brandes_queue_.size(); ++head) {
            const int current = brandes_queue_[head];
            const int next_distance = brandes_distance_[static_cast<std::size_t>(current)] + 1;
            for (const auto& arc : forward_[static_cast<std::size_t>(current)]) {
                const int next = arc.node;
                if (blocked_[static_cast<std::size_t>(next)]) {
                    continue;
                }
                if (brandes_distance_[static_cast<std::size_t>(next)] < 0) {
                    brandes_distance_[static_cast<std::size_t>(next)] = next_distance;
                    brandes_sigma_[static_cast<std::size_t>(next)] = 0.0;
                    brandes_predecessors_[static_cast<std::size_t>(next)].clear();
                    brandes_queue_.push_back(next);
                }
                if (brandes_distance_[static_cast<std::size_t>(next)] == next_distance) {
                    brandes_sigma_[static_cast<std::size_t>(next)] +=
                        brandes_sigma_[static_cast<std::size_t>(current)];
                    brandes_predecessors_[static_cast<std::size_t>(next)].push_back(current);
                }
            }
        }
    }
};

std::vector<std::string> metric_notes_for_implementation(GreedyMetricType metric) {
    std::vector<std::string> notes;
    notes.push_back(greedyMetricFormulaNote(metric));
    switch (metric) {
        case GreedyMetricType::DPV3:
            notes.push_back(
                "Greedy-DPV3 scores are initialized once and exactly recomputed only for candidates that can reach the newly blocked node.");
            notes.push_back(
                "Outgoing frequency sums are maintained incrementally as selected firebreak nodes become blocked.");
            break;
        case GreedyMetricType::DPV2:
            notes.push_back(
                "Greedy-DPV2 scores are initialized once and exactly recomputed only for candidates that can reach the newly blocked node.");
            notes.push_back(
                "Outgoing frequency sums are maintained incrementally as selected firebreak nodes become blocked.");
            break;
        case GreedyMetricType::Betweenness:
            notes.push_back(
                "Greedy-Betweenness computes the full unweighted directed Brandes centrality vector once per greedy iteration.");
            break;
        case GreedyMetricType::Closeness:
            notes.push_back(
                "Greedy-Closeness scores are initialized once and exactly recomputed only for candidates that can reach the newly blocked node.");
            break;
    }
    notes.push_back("Selected firebreak nodes are treated as blocked for later scoring.");
    return notes;
}

}  // namespace

GreedyResult GreedyHeuristic::runGreedy(
    const opt::OptimizationInstance& opt,
    GreedyMetricType metric,
    bool recompute_each_iteration,
    bool verbose) const {
    (void)recompute_each_iteration;
    validate_input(opt);

    const auto start = std::chrono::steady_clock::now();

    CumulativePropagationGraph graph;
    graph.buildFromOptimizationInstance(opt);
    LowMemoryGreedyScorer scorer(opt, graph);

    GreedyResult result;
    result.method_name = greedyMethodName(metric);
    result.objective_metric = greedyObjectiveMetricName(metric);
    result.metric_notes = metric_notes_for_implementation(metric);

    if (metric != GreedyMetricType::Betweenness) {
        scorer.initializeScores(metric);
    }

    for (int step = 0; step < opt.budget; ++step) {
        const CandidateChoice best = metric == GreedyMetricType::Betweenness
            ? scorer.bestFromScores(scorer.computeBetweennessScores())
            : scorer.bestFromCachedScores();
        if (!best.found) {
            throw std::runtime_error("Greedy heuristic ran out of eligible candidates before filling the budget.");
        }
        result.selected_firebreak_indices.push_back(best.compact_index);
        result.selected_firebreak_original_nodes.push_back(best.original_node);
        result.selected_scores.push_back(best.score);
        result.total_score += best.score;

        if (verbose) {
            std::cout << result.method_name
                      << " step " << (step + 1)
                      << " selected original node " << best.original_node
                      << " score " << best.score << "\n";
        }

        if (step + 1 < opt.budget) {
            if (metric == GreedyMetricType::Betweenness) {
                scorer.blockSelected(best.compact_index);
            } else {
                scorer.blockSelectedAndRefreshAffected(best.compact_index, metric);
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.runtime_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

}  // namespace firebreak::heuristics
