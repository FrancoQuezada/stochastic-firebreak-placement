#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "core/Scenario.hpp"

namespace firebreak::analysis {

struct ScenarioGraphDiagnostics {
    int scenario_id = 0;
    std::string message_filename;
    int ignition_node = 0;

    std::size_t observed_node_count = 0;
    std::size_t directed_arc_count = 0;
    std::size_t distinct_directed_arc_count = 0;
    std::size_t duplicate_arc_count = 0;
    int max_observed_node_id = 0;

    std::size_t reachable_from_ignition_count = 0;
    double reachable_fraction_from_ignition = 0.0;
    std::size_t unreachable_observed_node_count = 0;
    std::vector<int> sample_unreachable_nodes;

    int ignition_in_degree = 0;
    std::size_t in_degree_zero_count = 0;
    std::size_t non_ignition_in_degree_zero_count = 0;
    std::size_t in_degree_one_count = 0;
    std::size_t in_degree_greater_than_one_count = 0;
    int max_in_degree = 0;
    double average_in_degree = 0.0;
    std::vector<int> sample_multiple_parent_nodes;

    std::size_t out_degree_zero_count = 0;
    int max_out_degree = 0;
    double average_out_degree = 0.0;

    bool is_rooted_arborescence = false;
    bool is_dag = false;
    bool has_cycles = false;
    bool fully_reachable_from_ignition = false;
    bool weakly_connected = false;
    bool has_multiple_parent_nodes = false;
    std::vector<int> sample_cycle_nodes;

    std::size_t weak_component_count = 0;
    std::size_t largest_weak_component_size = 0;
    long long arc_excess = 0;

    std::size_t edges_with_valid_time = 0;
    double min_edge_time = 0.0;
    double max_edge_time = 0.0;
    bool has_edge_time = false;
    std::size_t edges_with_valid_ros = 0;
    std::size_t edges_with_non_positive_ros = 0;
    bool has_ros = false;

    std::string classification;
};

struct GraphDiagnosticsAggregate {
    std::size_t total_scenarios = 0;
    std::map<std::string, std::size_t> classification_counts;

    double average_observed_nodes = 0.0;
    double average_distinct_arcs = 0.0;
    double average_arc_excess = 0.0;
    double average_reachable_fraction_from_ignition = 0.0;
    std::size_t maximum_multiple_parent_nodes = 0;
    double average_multiple_parent_nodes = 0.0;
    std::size_t scenarios_with_cycles = 0;
    std::size_t scenarios_not_fully_reachable_from_ignition = 0;
    std::size_t scenarios_with_duplicate_arcs = 0;
};

ScenarioGraphDiagnostics analyze_scenario_graph(const core::Scenario& scenario);
GraphDiagnosticsAggregate aggregate_graph_diagnostics(const std::vector<ScenarioGraphDiagnostics>& diagnostics);
std::string graph_classification_ratio_summary(const std::vector<core::Scenario>& scenarios);

}  // namespace firebreak::analysis
