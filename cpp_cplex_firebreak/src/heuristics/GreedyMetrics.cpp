#include "heuristics/GreedyMetrics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace firebreak::heuristics {

namespace {

std::string normalized(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        if (ch == '-' || ch == '_' || std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool is_active_node(int node, const CumulativePropagationGraph& graph, const std::unordered_set<int>& blocked_nodes) {
    return graph.hasNode(node) && blocked_nodes.find(node) == blocked_nodes.end();
}

double outgoing_frequency_sum(
    const CumulativePropagationGraph& graph,
    int node,
    const std::unordered_set<int>& blocked_nodes) {
    double sum = 0.0;
    for (const int successor : graph.successors(node)) {
        if (blocked_nodes.find(successor) == blocked_nodes.end()) {
            sum += graph.arcWeight(node, successor);
        }
    }
    return sum;
}

std::vector<double> inverse_weight_distances(
    const CumulativePropagationGraph& graph,
    int source,
    const std::unordered_set<int>& blocked_nodes) {
    const int n = graph.numNodes();
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> distance(static_cast<std::size_t>(n), infinity);
    if (!is_active_node(source, graph, blocked_nodes)) {
        return distance;
    }

    using QueueEntry = std::pair<double, int>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;
    distance[static_cast<std::size_t>(source)] = 0.0;
    queue.push({0.0, source});

    while (!queue.empty()) {
        const auto [current_distance, current] = queue.top();
        queue.pop();
        if (current_distance > distance[static_cast<std::size_t>(current)]) {
            continue;
        }

        for (const int next : graph.successors(current)) {
            if (blocked_nodes.find(next) != blocked_nodes.end()) {
                continue;
            }
            const double cost = graph.inverseArcWeight(current, next);
            if (cost <= 0.0) {
                continue;
            }
            const double candidate = current_distance + cost;
            if (candidate < distance[static_cast<std::size_t>(next)]) {
                distance[static_cast<std::size_t>(next)] = candidate;
                queue.push({candidate, next});
            }
        }
    }

    return distance;
}

std::vector<int> unweighted_distances(
    const CumulativePropagationGraph& graph,
    int source,
    const std::unordered_set<int>& blocked_nodes,
    std::vector<std::vector<int>>* predecessors = nullptr,
    std::vector<double>* sigma = nullptr) {
    const int n = graph.numNodes();
    std::vector<int> distance(static_cast<std::size_t>(n), -1);
    if (!is_active_node(source, graph, blocked_nodes)) {
        return distance;
    }

    if (predecessors) {
        predecessors->assign(static_cast<std::size_t>(n), {});
    }
    if (sigma) {
        sigma->assign(static_cast<std::size_t>(n), 0.0);
        (*sigma)[static_cast<std::size_t>(source)] = 1.0;
    }

    std::queue<int> queue;
    distance[static_cast<std::size_t>(source)] = 0;
    queue.push(source);

    while (!queue.empty()) {
        const int current = queue.front();
        queue.pop();

        for (const int next : graph.successors(current)) {
            if (blocked_nodes.find(next) != blocked_nodes.end()) {
                continue;
            }
            if (distance[static_cast<std::size_t>(next)] < 0) {
                distance[static_cast<std::size_t>(next)] = distance[static_cast<std::size_t>(current)] + 1;
                queue.push(next);
            }
            if (distance[static_cast<std::size_t>(next)] == distance[static_cast<std::size_t>(current)] + 1) {
                if (sigma) {
                    (*sigma)[static_cast<std::size_t>(next)] += (*sigma)[static_cast<std::size_t>(current)];
                }
                if (predecessors) {
                    (*predecessors)[static_cast<std::size_t>(next)].push_back(current);
                }
            }
        }
    }

    return distance;
}

double dpv3_score(
    const CumulativePropagationGraph& graph,
    int candidate,
    const std::unordered_set<int>& blocked_nodes) {
    const double outgoing_sum = outgoing_frequency_sum(graph, candidate, blocked_nodes);
    if (outgoing_sum <= 0.0) {
        return 0.0;
    }

    const auto distances = inverse_weight_distances(graph, candidate, blocked_nodes);
    double downstream = 0.0;
    for (const double distance : distances) {
        if (std::isfinite(distance)) {
            downstream += 1.0 / (1.0 + distance);
        }
    }
    return outgoing_sum * downstream;
}

double dpv2_score(
    const CumulativePropagationGraph& graph,
    int candidate,
    const std::unordered_set<int>& blocked_nodes) {
    const double outgoing_sum = outgoing_frequency_sum(graph, candidate, blocked_nodes);
    if (outgoing_sum <= 0.0) {
        return 0.0;
    }
    return outgoing_sum * static_cast<double>(graph.reachableFrom(candidate, blocked_nodes).size());
}

std::vector<double> betweenness_scores(
    const CumulativePropagationGraph& graph,
    const std::unordered_set<int>& blocked_nodes) {
    const int n = graph.numNodes();
    std::vector<double> centrality(static_cast<std::size_t>(n), 0.0);

    for (const int source : graph.nodes()) {
        if (!is_active_node(source, graph, blocked_nodes)) {
            continue;
        }

        std::vector<std::vector<int>> predecessors;
        std::vector<double> sigma;
        const auto distance = unweighted_distances(graph, source, blocked_nodes, &predecessors, &sigma);

        std::vector<int> order;
        order.reserve(static_cast<std::size_t>(n));
        for (int node = 0; node < n; ++node) {
            if (distance[static_cast<std::size_t>(node)] >= 0) {
                order.push_back(node);
            }
        }
        std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
            return distance[static_cast<std::size_t>(lhs)] > distance[static_cast<std::size_t>(rhs)];
        });

        std::vector<double> delta(static_cast<std::size_t>(n), 0.0);
        for (const int w : order) {
            for (const int v : predecessors[static_cast<std::size_t>(w)]) {
                if (sigma[static_cast<std::size_t>(w)] > 0.0) {
                    delta[static_cast<std::size_t>(v)] +=
                        (sigma[static_cast<std::size_t>(v)] / sigma[static_cast<std::size_t>(w)]) *
                        (1.0 + delta[static_cast<std::size_t>(w)]);
                }
            }
            if (w != source) {
                centrality[static_cast<std::size_t>(w)] += delta[static_cast<std::size_t>(w)];
            }
        }
    }

    return centrality;
}

double closeness_score(
    const CumulativePropagationGraph& graph,
    int candidate,
    const std::unordered_set<int>& blocked_nodes) {
    const auto distances = unweighted_distances(graph, candidate, blocked_nodes);
    int reachable_count = 0;
    double distance_sum = 0.0;
    for (const int distance : distances) {
        if (distance > 0) {
            ++reachable_count;
            distance_sum += static_cast<double>(distance);
        }
    }
    if (reachable_count == 0 || distance_sum <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(reachable_count) / distance_sum;
}

}  // namespace

GreedyMetricType parseGreedyMetricType(const std::string& value) {
    const std::string key = normalized(value);
    if (key == "dpv3") {
        return GreedyMetricType::DPV3;
    }
    if (key == "dpv2") {
        return GreedyMetricType::DPV2;
    }
    if (key == "betweenness") {
        return GreedyMetricType::Betweenness;
    }
    if (key == "closeness") {
        return GreedyMetricType::Closeness;
    }
    throw std::runtime_error(
        "Invalid greedy metric '" + value + "'. Supported metrics: DPV3, DPV2, Betweenness, Closeness.");
}

std::string greedyMethodName(GreedyMetricType metric) {
    switch (metric) {
        case GreedyMetricType::DPV3:
            return "Greedy-DPV3";
        case GreedyMetricType::DPV2:
            return "Greedy-DPV2";
        case GreedyMetricType::Betweenness:
            return "Greedy-Betweenness";
        case GreedyMetricType::Closeness:
            return "Greedy-Closeness";
    }
    return "Greedy-Unknown";
}

std::string greedyObjectiveMetricName(GreedyMetricType metric) {
    switch (metric) {
        case GreedyMetricType::DPV3:
            return "greedy_DPV3_inverse_weighted_reachability";
        case GreedyMetricType::DPV2:
            return "greedy_DPV2_frequency_reachability";
        case GreedyMetricType::Betweenness:
            return "greedy_betweenness_unweighted";
        case GreedyMetricType::Closeness:
            return "greedy_closeness_unweighted";
    }
    return "greedy_unknown";
}

std::string greedyMetricFormulaNote(GreedyMetricType metric) {
    switch (metric) {
        case GreedyMetricType::DPV3:
            return "Greedy-DPV3 score = outgoing_frequency_sum(i) * sum_{k reachable from i} 1 / (1 + dist_inv(i,k)); dist_inv uses shortest paths with arc cost 1/frequency.";
        case GreedyMetricType::DPV2:
            return "Greedy-DPV2 score = outgoing_frequency_sum(i) * closed_reachable_count(i) on the active cumulative graph.";
        case GreedyMetricType::Betweenness:
            return "Greedy-Betweenness uses unweighted directed Brandes betweenness on the active cumulative graph.";
        case GreedyMetricType::Closeness:
            return "Greedy-Closeness score = reachable_count(i) / sum of unweighted directed distances from i over reachable nodes.";
    }
    return "Unknown greedy metric.";
}

double scoreCandidate(
    const CumulativePropagationGraph& graph,
    int candidate,
    const std::unordered_set<int>& blocked_nodes,
    GreedyMetricType metric) {
    if (!is_active_node(candidate, graph, blocked_nodes)) {
        return 0.0;
    }

    switch (metric) {
        case GreedyMetricType::DPV3:
            return dpv3_score(graph, candidate, blocked_nodes);
        case GreedyMetricType::DPV2:
            return dpv2_score(graph, candidate, blocked_nodes);
        case GreedyMetricType::Betweenness:
            return betweenness_scores(graph, blocked_nodes)[static_cast<std::size_t>(candidate)];
        case GreedyMetricType::Closeness:
            return closeness_score(graph, candidate, blocked_nodes);
    }
    return 0.0;
}

}  // namespace firebreak::heuristics
