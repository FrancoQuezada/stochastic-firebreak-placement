#include "analysis/GraphDiagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace firebreak::analysis {

namespace {

constexpr std::size_t kSampleLimit = 10;

void append_sample(std::vector<int>& sample, int node) {
    if (sample.size() < kSampleLimit) {
        sample.push_back(node);
    }
}

std::unordered_set<int> to_set(const std::vector<int>& nodes) {
    return std::unordered_set<int>(nodes.begin(), nodes.end());
}

std::unordered_map<int, std::vector<int>> build_directed_adjacency(
    const std::set<std::pair<int, int>>& distinct_arcs,
    const std::vector<int>& observed_nodes) {
    std::unordered_map<int, std::vector<int>> adjacency;
    for (const int node : observed_nodes) {
        adjacency[node] = {};
    }
    for (const auto& arc : distinct_arcs) {
        adjacency[arc.first].push_back(arc.second);
    }
    for (auto& item : adjacency) {
        auto& successors = item.second;
        std::sort(successors.begin(), successors.end());
    }
    return adjacency;
}

std::unordered_map<int, std::vector<int>> build_weak_adjacency(
    const std::set<std::pair<int, int>>& distinct_arcs,
    const std::vector<int>& observed_nodes) {
    std::unordered_map<int, std::vector<int>> adjacency;
    for (const int node : observed_nodes) {
        adjacency[node] = {};
    }
    for (const auto& arc : distinct_arcs) {
        adjacency[arc.first].push_back(arc.second);
        adjacency[arc.second].push_back(arc.first);
    }
    for (auto& item : adjacency) {
        auto& neighbors = item.second;
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    return adjacency;
}

std::unordered_set<int> reachable_from(
    int root,
    const std::unordered_map<int, std::vector<int>>& adjacency) {
    std::unordered_set<int> visited;
    std::queue<int> frontier;
    visited.insert(root);
    frontier.push(root);

    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        const auto it = adjacency.find(current);
        if (it == adjacency.end()) {
            continue;
        }
        for (const int next : it->second) {
            if (visited.insert(next).second) {
                frontier.push(next);
            }
        }
    }
    return visited;
}

struct CycleCheckResult {
    bool has_cycle = false;
    std::vector<int> sample_cycle;
};

CycleCheckResult detect_directed_cycle(
    const std::vector<int>& observed_nodes,
    const std::unordered_map<int, std::vector<int>>& adjacency) {
    std::unordered_map<int, int> color;
    std::unordered_map<int, std::size_t> stack_position;
    std::vector<int> stack;
    for (const int node : observed_nodes) {
        color[node] = 0;
    }

    CycleCheckResult result;
    std::function<bool(int)> dfs = [&](int node) {
        color[node] = 1;
        stack_position[node] = stack.size();
        stack.push_back(node);

        const auto it = adjacency.find(node);
        if (it != adjacency.end()) {
            for (const int next : it->second) {
                const auto color_it = color.find(next);
                const int next_color = color_it == color.end() ? 0 : color_it->second;
                if (next_color == 0) {
                    if (dfs(next)) {
                        return true;
                    }
                } else if (next_color == 1) {
                    result.has_cycle = true;
                    const auto pos_it = stack_position.find(next);
                    if (pos_it != stack_position.end()) {
                        for (std::size_t i = pos_it->second; i < stack.size() && result.sample_cycle.size() < kSampleLimit; ++i) {
                            result.sample_cycle.push_back(stack[i]);
                        }
                        if (result.sample_cycle.size() < kSampleLimit) {
                            result.sample_cycle.push_back(next);
                        }
                    }
                    return true;
                }
            }
        }

        stack.pop_back();
        stack_position.erase(node);
        color[node] = 2;
        return false;
    };

    for (const int node : observed_nodes) {
        if (color[node] == 0 && dfs(node)) {
            break;
        }
    }
    return result;
}

std::pair<std::size_t, std::size_t> weak_components(
    const std::vector<int>& observed_nodes,
    const std::unordered_map<int, std::vector<int>>& weak_adjacency) {
    std::unordered_set<int> visited;
    std::size_t component_count = 0;
    std::size_t largest_size = 0;

    for (const int start : observed_nodes) {
        if (visited.find(start) != visited.end()) {
            continue;
        }
        ++component_count;
        std::size_t size = 0;
        std::queue<int> frontier;
        visited.insert(start);
        frontier.push(start);

        while (!frontier.empty()) {
            const int current = frontier.front();
            frontier.pop();
            ++size;
            const auto it = weak_adjacency.find(current);
            if (it == weak_adjacency.end()) {
                continue;
            }
            for (const int next : it->second) {
                if (visited.insert(next).second) {
                    frontier.push(next);
                }
            }
        }
        largest_size = std::max(largest_size, size);
    }

    return {component_count, largest_size};
}

}  // namespace

ScenarioGraphDiagnostics analyze_scenario_graph(const core::Scenario& scenario) {
    ScenarioGraphDiagnostics result;
    result.scenario_id = scenario.scenario_id;
    result.message_filename = scenario.message_filename;
    result.ignition_node = scenario.ignition_node;

    const auto observed_nodes = scenario.graph().observed_nodes();
    const auto observed_set = to_set(observed_nodes);
    result.observed_node_count = observed_nodes.size();
    result.directed_arc_count = scenario.graph().edges().size();
    result.max_observed_node_id = scenario.graph().max_node_id_observed();

    std::set<std::pair<int, int>> distinct_arcs;
    for (const auto& edge : scenario.graph().edges()) {
        distinct_arcs.insert({edge.from, edge.to});
        if (edge.has_time && std::isfinite(edge.time)) {
            if (!result.has_edge_time) {
                result.min_edge_time = edge.time;
                result.max_edge_time = edge.time;
                result.has_edge_time = true;
            } else {
                result.min_edge_time = std::min(result.min_edge_time, edge.time);
                result.max_edge_time = std::max(result.max_edge_time, edge.time);
            }
            ++result.edges_with_valid_time;
        }
        if (edge.has_ros && std::isfinite(edge.ros)) {
            result.has_ros = true;
            ++result.edges_with_valid_ros;
            if (edge.ros <= 0.0) {
                ++result.edges_with_non_positive_ros;
            }
        }
    }
    result.distinct_directed_arc_count = distinct_arcs.size();
    result.duplicate_arc_count = result.directed_arc_count - result.distinct_directed_arc_count;

    if (result.observed_node_count == 0 || result.directed_arc_count == 0) {
        result.classification = "empty_or_invalid";
        result.is_dag = true;
        return result;
    }

    const auto directed_adjacency = build_directed_adjacency(distinct_arcs, observed_nodes);
    const auto weak_adjacency = build_weak_adjacency(distinct_arcs, observed_nodes);

    std::unordered_map<int, int> in_degree;
    std::unordered_map<int, int> out_degree;
    for (const int node : observed_nodes) {
        in_degree[node] = 0;
        out_degree[node] = 0;
    }
    for (const auto& arc : distinct_arcs) {
        ++out_degree[arc.first];
        ++in_degree[arc.second];
    }

    long long total_in_degree = 0;
    long long total_out_degree = 0;
    bool every_non_ignition_has_one_parent = true;
    for (const int node : observed_nodes) {
        const int in = in_degree[node];
        const int out = out_degree[node];
        total_in_degree += in;
        total_out_degree += out;
        result.max_in_degree = std::max(result.max_in_degree, in);
        result.max_out_degree = std::max(result.max_out_degree, out);

        if (in == 0) {
            ++result.in_degree_zero_count;
            if (node != scenario.ignition_node) {
                ++result.non_ignition_in_degree_zero_count;
            }
        } else if (in == 1) {
            ++result.in_degree_one_count;
        } else {
            ++result.in_degree_greater_than_one_count;
            append_sample(result.sample_multiple_parent_nodes, node);
        }

        if (node != scenario.ignition_node && in != 1) {
            every_non_ignition_has_one_parent = false;
        }
        if (out == 0) {
            ++result.out_degree_zero_count;
        }
    }

    result.ignition_in_degree = in_degree[scenario.ignition_node];
    result.has_multiple_parent_nodes = result.in_degree_greater_than_one_count > 0;
    result.average_in_degree = static_cast<double>(total_in_degree) / static_cast<double>(result.observed_node_count);
    result.average_out_degree = static_cast<double>(total_out_degree) / static_cast<double>(result.observed_node_count);

    const auto reachable = reachable_from(scenario.ignition_node, directed_adjacency);
    for (const int node : observed_nodes) {
        if (reachable.find(node) != reachable.end()) {
            ++result.reachable_from_ignition_count;
        } else {
            append_sample(result.sample_unreachable_nodes, node);
        }
    }
    result.unreachable_observed_node_count = result.observed_node_count - result.reachable_from_ignition_count;
    result.reachable_fraction_from_ignition =
        static_cast<double>(result.reachable_from_ignition_count) / static_cast<double>(result.observed_node_count);
    result.fully_reachable_from_ignition = result.reachable_from_ignition_count == result.observed_node_count;

    const auto cycle_result = detect_directed_cycle(observed_nodes, directed_adjacency);
    result.has_cycles = cycle_result.has_cycle;
    result.is_dag = !result.has_cycles;
    result.sample_cycle_nodes = cycle_result.sample_cycle;

    const auto weak_result = weak_components(observed_nodes, weak_adjacency);
    result.weak_component_count = weak_result.first;
    result.largest_weak_component_size = weak_result.second;
    result.weakly_connected = result.weak_component_count == 1;

    result.arc_excess =
        static_cast<long long>(result.distinct_directed_arc_count) -
        (static_cast<long long>(result.observed_node_count) - 1LL);

    result.is_rooted_arborescence =
        result.observed_node_count > 0 &&
        result.fully_reachable_from_ignition &&
        result.distinct_directed_arc_count == result.observed_node_count - 1 &&
        result.ignition_in_degree == 0 &&
        every_non_ignition_has_one_parent &&
        !result.has_cycles;

    if (result.observed_node_count == 0 || result.directed_arc_count == 0) {
        result.classification = "empty_or_invalid";
    } else if (!result.fully_reachable_from_ignition) {
        result.classification = "not_fully_reachable_from_ignition";
    } else if (result.is_rooted_arborescence) {
        result.classification = "rooted_arborescence";
    } else if (result.is_dag) {
        result.classification = "dag_not_tree";
    } else {
        result.classification = "general_directed_graph";
    }

    return result;
}

GraphDiagnosticsAggregate aggregate_graph_diagnostics(const std::vector<ScenarioGraphDiagnostics>& diagnostics) {
    GraphDiagnosticsAggregate aggregate;
    aggregate.total_scenarios = diagnostics.size();
    if (diagnostics.empty()) {
        return aggregate;
    }

    double total_observed_nodes = 0.0;
    double total_distinct_arcs = 0.0;
    double total_arc_excess = 0.0;
    double total_reachable_fraction = 0.0;
    double total_multiple_parent_nodes = 0.0;

    for (const auto& item : diagnostics) {
        ++aggregate.classification_counts[item.classification];
        total_observed_nodes += static_cast<double>(item.observed_node_count);
        total_distinct_arcs += static_cast<double>(item.distinct_directed_arc_count);
        total_arc_excess += static_cast<double>(item.arc_excess);
        total_reachable_fraction += item.reachable_fraction_from_ignition;
        total_multiple_parent_nodes += static_cast<double>(item.in_degree_greater_than_one_count);
        aggregate.maximum_multiple_parent_nodes = std::max(
            aggregate.maximum_multiple_parent_nodes,
            item.in_degree_greater_than_one_count);
        if (item.has_cycles) {
            ++aggregate.scenarios_with_cycles;
        }
        if (!item.fully_reachable_from_ignition) {
            ++aggregate.scenarios_not_fully_reachable_from_ignition;
        }
        if (item.duplicate_arc_count > 0) {
            ++aggregate.scenarios_with_duplicate_arcs;
        }
    }

    const double n = static_cast<double>(diagnostics.size());
    aggregate.average_observed_nodes = total_observed_nodes / n;
    aggregate.average_distinct_arcs = total_distinct_arcs / n;
    aggregate.average_arc_excess = total_arc_excess / n;
    aggregate.average_reachable_fraction_from_ignition = total_reachable_fraction / n;
    aggregate.average_multiple_parent_nodes = total_multiple_parent_nodes / n;
    return aggregate;
}

std::string graph_classification_ratio_summary(const std::vector<core::Scenario>& scenarios) {
    std::vector<ScenarioGraphDiagnostics> diagnostics;
    diagnostics.reserve(scenarios.size());
    for (const auto& scenario : scenarios) {
        diagnostics.push_back(analyze_scenario_graph(scenario));
    }
    const auto aggregate = aggregate_graph_diagnostics(diagnostics);
    const double total = static_cast<double>(aggregate.total_scenarios);

    const auto ratio = [&](const std::string& classification) {
        if (total <= 0.0) {
            return 0.0;
        }
        const auto it = aggregate.classification_counts.find(classification);
        const std::size_t count = it == aggregate.classification_counts.end() ? 0 : it->second;
        return static_cast<double>(count) / total;
    };

    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << "RT=" << ratio("rooted_arborescence")
        << ";ADAG=" << ratio("dag_not_tree")
        << ";GDG=" << ratio("general_directed_graph")
        << ";NFR=" << ratio("not_fully_reachable_from_ignition")
        << ";EMPTY=" << ratio("empty_or_invalid");
    return out.str();
}

}  // namespace firebreak::analysis
