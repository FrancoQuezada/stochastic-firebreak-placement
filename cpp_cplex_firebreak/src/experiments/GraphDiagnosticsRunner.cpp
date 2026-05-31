#include "experiments/GraphDiagnosticsRunner.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "analysis/GraphDiagnostics.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/ResultWriter.hpp"
#include "io/ScenarioFileUtils.hpp"

namespace firebreak::experiments {

namespace {

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

double percentage(std::size_t count, std::size_t total) {
    if (total == 0) {
        return 0.0;
    }
    return 100.0 * static_cast<double>(count) / static_cast<double>(total);
}

std::size_t classification_count(const analysis::GraphDiagnosticsAggregate& aggregate, const std::string& key) {
    const auto it = aggregate.classification_counts.find(key);
    if (it == aggregate.classification_counts.end()) {
        return 0;
    }
    return it->second;
}

void write_int_array(std::ostream& out, const std::vector<int>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << values[i];
    }
    out << "]";
}

void print_summary(std::ostream& out, const analysis::GraphDiagnosticsAggregate& aggregate) {
    const auto total = aggregate.total_scenarios;
    const auto rooted = classification_count(aggregate, "rooted_arborescence");
    const auto dag = classification_count(aggregate, "dag_not_tree");
    const auto general = classification_count(aggregate, "general_directed_graph");
    const auto not_reachable = classification_count(aggregate, "not_fully_reachable_from_ignition");
    const auto empty = classification_count(aggregate, "empty_or_invalid");

    out << "Scenarios analyzed: " << total << "\n";
    out << std::fixed << std::setprecision(6);
    out << "Rooted arborescence: " << rooted << " (" << percentage(rooted, total) << "%)\n";
    out << "DAG not tree: " << dag << " (" << percentage(dag, total) << "%)\n";
    out << "General directed graph: " << general << " (" << percentage(general, total) << "%)\n";
    out << "Not fully reachable classification: " << not_reachable
        << " (" << percentage(not_reachable, total) << "%)\n";
    out << "Empty or invalid: " << empty << " (" << percentage(empty, total) << "%)\n";
    out << "Scenarios not fully reachable flag: "
        << aggregate.scenarios_not_fully_reachable_from_ignition << "\n";
    out << "Scenarios with cycles: " << aggregate.scenarios_with_cycles << "\n";
    out << "Scenarios with duplicate arcs: " << aggregate.scenarios_with_duplicate_arcs << "\n";
    out << "Average observed nodes: " << aggregate.average_observed_nodes << "\n";
    out << "Average distinct arcs: " << aggregate.average_distinct_arcs << "\n";
    out << "Average arc excess: " << aggregate.average_arc_excess << "\n";
    out << "Average reachable fraction: " << aggregate.average_reachable_fraction_from_ignition << "\n";
    out << "Average multiple-parent nodes: " << aggregate.average_multiple_parent_nodes << "\n";
    out << "Maximum multiple-parent nodes: " << aggregate.maximum_multiple_parent_nodes << "\n";
}

void write_json(
    const std::filesystem::path& output_path,
    const analysis::GraphDiagnosticsAggregate& aggregate,
    const std::vector<analysis::ScenarioGraphDiagnostics>& diagnostics,
    const std::vector<std::string>& warnings) {
    firebreak::io::ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open output file: " + output_path.string());
    }

    out << std::fixed << std::setprecision(8);
    out << "{\n";
    out << "  \"aggregate\": {\n";
    out << "    \"total_scenarios\": " << aggregate.total_scenarios << ",\n";
    out << "    \"classification_counts\": {\n";
    std::size_t class_pos = 0;
    for (const auto& item : aggregate.classification_counts) {
        out << "      \"" << firebreak::io::json_escape(item.first) << "\": " << item.second
            << (++class_pos == aggregate.classification_counts.size() ? "\n" : ",\n");
    }
    out << "    },\n";
    out << "    \"classification_percentages\": {\n";
    class_pos = 0;
    for (const auto& item : aggregate.classification_counts) {
        out << "      \"" << firebreak::io::json_escape(item.first) << "\": "
            << percentage(item.second, aggregate.total_scenarios)
            << (++class_pos == aggregate.classification_counts.size() ? "\n" : ",\n");
    }
    out << "    },\n";
    out << "    \"average_observed_nodes\": " << aggregate.average_observed_nodes << ",\n";
    out << "    \"average_distinct_arcs\": " << aggregate.average_distinct_arcs << ",\n";
    out << "    \"average_arc_excess\": " << aggregate.average_arc_excess << ",\n";
    out << "    \"average_reachable_fraction_from_ignition\": "
        << aggregate.average_reachable_fraction_from_ignition << ",\n";
    out << "    \"maximum_multiple_parent_nodes\": " << aggregate.maximum_multiple_parent_nodes << ",\n";
    out << "    \"average_multiple_parent_nodes\": " << aggregate.average_multiple_parent_nodes << ",\n";
    out << "    \"scenarios_with_cycles\": " << aggregate.scenarios_with_cycles << ",\n";
    out << "    \"scenarios_not_fully_reachable_from_ignition\": "
        << aggregate.scenarios_not_fully_reachable_from_ignition << ",\n";
    out << "    \"scenarios_with_duplicate_arcs\": " << aggregate.scenarios_with_duplicate_arcs << "\n";
    out << "  },\n";

    out << "  \"scenarios\": [\n";
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        const auto& d = diagnostics[i];
        out << "    {\n";
        out << "      \"scenario_id\": " << d.scenario_id << ",\n";
        out << "      \"message_filename\": \"" << firebreak::io::json_escape(d.message_filename) << "\",\n";
        out << "      \"ignition_node\": " << d.ignition_node << ",\n";
        out << "      \"classification\": \"" << firebreak::io::json_escape(d.classification) << "\",\n";
        out << "      \"observed_node_count\": " << d.observed_node_count << ",\n";
        out << "      \"directed_arc_count\": " << d.directed_arc_count << ",\n";
        out << "      \"distinct_directed_arc_count\": " << d.distinct_directed_arc_count << ",\n";
        out << "      \"duplicate_arc_count\": " << d.duplicate_arc_count << ",\n";
        out << "      \"max_observed_node_id\": " << d.max_observed_node_id << ",\n";
        out << "      \"reachable_from_ignition_count\": " << d.reachable_from_ignition_count << ",\n";
        out << "      \"reachable_fraction_from_ignition\": " << d.reachable_fraction_from_ignition << ",\n";
        out << "      \"unreachable_observed_node_count\": " << d.unreachable_observed_node_count << ",\n";
        out << "      \"sample_unreachable_nodes\": ";
        write_int_array(out, d.sample_unreachable_nodes);
        out << ",\n";
        out << "      \"ignition_in_degree\": " << d.ignition_in_degree << ",\n";
        out << "      \"in_degree_zero_count\": " << d.in_degree_zero_count << ",\n";
        out << "      \"non_ignition_in_degree_zero_count\": " << d.non_ignition_in_degree_zero_count << ",\n";
        out << "      \"in_degree_one_count\": " << d.in_degree_one_count << ",\n";
        out << "      \"in_degree_greater_than_one_count\": " << d.in_degree_greater_than_one_count << ",\n";
        out << "      \"max_in_degree\": " << d.max_in_degree << ",\n";
        out << "      \"average_in_degree\": " << d.average_in_degree << ",\n";
        out << "      \"sample_multiple_parent_nodes\": ";
        write_int_array(out, d.sample_multiple_parent_nodes);
        out << ",\n";
        out << "      \"out_degree_zero_count\": " << d.out_degree_zero_count << ",\n";
        out << "      \"max_out_degree\": " << d.max_out_degree << ",\n";
        out << "      \"average_out_degree\": " << d.average_out_degree << ",\n";
        out << "      \"is_rooted_arborescence\": " << (d.is_rooted_arborescence ? "true" : "false") << ",\n";
        out << "      \"is_dag\": " << (d.is_dag ? "true" : "false") << ",\n";
        out << "      \"has_cycles\": " << (d.has_cycles ? "true" : "false") << ",\n";
        out << "      \"fully_reachable_from_ignition\": "
            << (d.fully_reachable_from_ignition ? "true" : "false") << ",\n";
        out << "      \"weakly_connected\": " << (d.weakly_connected ? "true" : "false") << ",\n";
        out << "      \"has_multiple_parent_nodes\": " << (d.has_multiple_parent_nodes ? "true" : "false") << ",\n";
        out << "      \"sample_cycle_nodes\": ";
        write_int_array(out, d.sample_cycle_nodes);
        out << ",\n";
        out << "      \"weak_component_count\": " << d.weak_component_count << ",\n";
        out << "      \"largest_weak_component_size\": " << d.largest_weak_component_size << ",\n";
        out << "      \"arc_excess\": " << d.arc_excess << ",\n";
        out << "      \"edges_with_valid_time\": " << d.edges_with_valid_time << ",\n";
        out << "      \"has_edge_time\": " << (d.has_edge_time ? "true" : "false") << ",\n";
        out << "      \"min_edge_time\": " << d.min_edge_time << ",\n";
        out << "      \"max_edge_time\": " << d.max_edge_time << ",\n";
        out << "      \"edges_with_valid_ros\": " << d.edges_with_valid_ros << ",\n";
        out << "      \"has_ros\": " << (d.has_ros ? "true" : "false") << ",\n";
        out << "      \"edges_with_non_positive_ros\": " << d.edges_with_non_positive_ros << "\n";
        out << "    }" << (i + 1 == diagnostics.size() ? "\n" : ",\n");
    }
    out << "  ],\n";

    out << "  \"warnings\": [";
    if (!warnings.empty()) {
        out << "\n";
        for (std::size_t i = 0; i < warnings.size(); ++i) {
            out << "    \"" << firebreak::io::json_escape(warnings[i]) << "\""
                << (i + 1 == warnings.size() ? "\n" : ",\n");
        }
        out << "  ]\n";
    } else {
        out << "]\n";
    }
    out << "}\n";
}

}  // namespace

int GraphDiagnosticsRunner::run(const GraphDiagnosticsOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.scenario_ids.empty()) {
        throw std::runtime_error("--scenario-ids or --scenario-range is required.");
    }

    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());
    const auto output_path = options.output_path.empty()
        ? firebreak::io::resolve_output_path("results/graph_diagnostics.json")
        : firebreak::io::resolve_output_path(options.output_path.string());

    const auto inventory = firebreak::io::detect_message_files(results_path);
    firebreak::io::validate_scenario_ids(inventory, options.scenario_ids);

    std::vector<std::string> warnings;
    firebreak::io::Cell2FireReader reader;
    auto instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        options.scenario_ids,
        warnings);

    std::vector<analysis::ScenarioGraphDiagnostics> diagnostics;
    diagnostics.reserve(instance.scenarios.size());
    for (const auto& scenario : instance.scenarios) {
        diagnostics.push_back(analysis::analyze_scenario_graph(scenario));
    }
    const auto aggregate = analysis::aggregate_graph_diagnostics(diagnostics);

    print_summary(std::cout, aggregate);
    write_json(output_path, aggregate, diagnostics, warnings);
    std::cout << "Wrote summary: " << firebreak::io::path_to_string(output_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments
