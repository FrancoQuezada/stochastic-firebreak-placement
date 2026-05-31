#include "io/ResultWriter.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

#include "io/PathUtils.hpp"

namespace firebreak::io {

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += ch;
        }
    }
    return out;
}

void print_smoke_summary(
    std::ostream& out,
    const core::Instance& instance,
    const ScenarioInventory& inventory,
    const std::vector<int>& requested_ids,
    const std::vector<std::string>& warnings) {
    out << "Landscape: " << instance.landscape_name << "\n";
    out << "Forest path: " << path_to_string(instance.forest_path) << "\n";
    out << "Results path: " << path_to_string(instance.results_path) << "\n";
    out << "Detected message files: " << inventory.count() << "\n";
    out << "Min scenario ID: " << inventory.min_id() << "\n";
    out << "Max scenario ID: " << inventory.max_id() << "\n";
    out << "Requested scenario IDs:";
    for (const int id : requested_ids) {
        out << " " << id;
    }
    out << "\n";
    if (instance.has_forest_size) {
        out << "Rows: " << instance.rows << "\n";
        out << "Cols: " << instance.cols << "\n";
        out << "NCells: " << instance.n_cells << "\n";
    } else {
        out << "NCells: unknown\n";
    }
    if (instance.available_nodes_known) {
        out << "Available nodes: " << instance.available_nodes.size() << "\n";
    } else {
        out << "Available nodes: unknown\n";
    }

    for (const auto& scenario : instance.scenarios) {
        out << "Scenario " << scenario.scenario_id
            << " | message=" << scenario.message_filename
            << " | ignition=" << scenario.ignition_node
            << " | observed_nodes=" << scenario.propagation_graph.num_nodes_observed()
            << " | edges=" << scenario.propagation_graph.num_edges() << "\n";
    }

    for (const auto& warning : warnings) {
        out << "Warning: " << warning << "\n";
    }
}

void write_smoke_summary_json(
    const std::filesystem::path& output_path,
    const core::Instance& instance,
    const ScenarioInventory& inventory,
    const std::vector<int>& requested_ids,
    const std::vector<std::string>& warnings) {
    ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open output file: " + output_path.string());
    }

    out << "{\n";
    out << "  \"landscape\": \"" << json_escape(instance.landscape_name) << "\",\n";
    out << "  \"forest_path\": \"" << json_escape(path_to_string(instance.forest_path)) << "\",\n";
    out << "  \"results_path\": \"" << json_escape(path_to_string(instance.results_path)) << "\",\n";
    out << "  \"message_inventory\": {\n";
    out << "    \"count\": " << inventory.count() << ",\n";
    out << "    \"min_id\": " << inventory.min_id() << ",\n";
    out << "    \"max_id\": " << inventory.max_id() << "\n";
    out << "  },\n";
    out << "  \"requested_scenario_ids\": [";
    for (std::size_t i = 0; i < requested_ids.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << requested_ids[i];
    }
    out << "],\n";
    out << "  \"forest\": {\n";
    out << "    \"rows\": " << instance.rows << ",\n";
    out << "    \"cols\": " << instance.cols << ",\n";
    out << "    \"n_cells\": " << instance.n_cells << ",\n";
    out << "    \"has_forest_size\": " << (instance.has_forest_size ? "true" : "false") << ",\n";
    out << "    \"available_nodes_known\": " << (instance.available_nodes_known ? "true" : "false") << ",\n";
    out << "    \"available_nodes_count\": " << instance.available_nodes.size() << "\n";
    out << "  },\n";
    out << "  \"scenarios\": [\n";
    for (std::size_t i = 0; i < instance.scenarios.size(); ++i) {
        const auto& scenario = instance.scenarios[i];
        out << "    {\n";
        out << "      \"scenario_id\": " << scenario.scenario_id << ",\n";
        out << "      \"message_filename\": \"" << json_escape(scenario.message_filename) << "\",\n";
        out << "      \"ignition_node\": " << scenario.ignition_node << ",\n";
        out << "      \"observed_nodes\": " << scenario.propagation_graph.num_nodes_observed() << ",\n";
        out << "      \"edges\": " << scenario.propagation_graph.num_edges() << "\n";
        out << "    }" << (i + 1 == instance.scenarios.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"warnings\": [";
    if (!warnings.empty()) {
        out << "\n";
        for (std::size_t i = 0; i < warnings.size(); ++i) {
            out << "    \"" << json_escape(warnings[i]) << "\""
                << (i + 1 == warnings.size() ? "\n" : ",\n");
        }
        out << "  ]\n";
    } else {
        out << "]\n";
    }
    out << "}\n";
}

}  // namespace firebreak::io

