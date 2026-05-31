#include "experiments/OptInstanceRunner.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/ResultWriter.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"

namespace firebreak::experiments {

namespace {

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
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

void write_double_array(std::ostream& out, const std::vector<double>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << values[i];
    }
    out << "]";
}

void print_opt_instance_summary(std::ostream& out, const opt::OptimizationInstance& instance) {
    out << "Landscape: " << instance.landscape_name << "\n";
    out << "NCells: " << instance.n_cells << "\n";
    out << std::fixed << std::setprecision(6);
    out << "Alpha: " << instance.alpha << "\n";
    out << "Budget: " << instance.budget << "\n";
    out << "Eligible nodes: " << instance.num_eligible_nodes() << "\n";
    out << "Mapped nodes: " << instance.num_mapped_nodes() << "\n";
    out << "Scenarios: " << instance.num_scenarios() << "\n";
    out << "Scenario probabilities:";
    for (const double probability : instance.scenario_probabilities) {
        out << " " << probability;
    }
    out << "\n";
    out << "Total arcs: " << instance.total_arcs << "\n";
    out << "Total DPV pairs: " << instance.total_dpv_pairs << "\n";
    for (const auto& scenario : instance.scenarios) {
        out << "Scenario " << scenario.scenario_id
            << " | probability=" << scenario.probability
            << " | ignition_original=" << scenario.ignition_original_node
            << " | ignition_index=" << scenario.ignition_index
            << " | arcs=" << scenario.arcs.size()
            << " | observed_nodes=" << scenario.observed_node_indices.size()
            << " | dpv_pairs=" << scenario.dpv.num_pairs()
            << "\n";
    }
    for (const auto& note : instance.metadata_notes) {
        out << "Note: " << note << "\n";
    }
}

void write_opt_instance_json(const std::filesystem::path& output_path, const opt::OptimizationInstance& instance) {
    firebreak::io::ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open output file: " + output_path.string());
    }

    out << std::fixed << std::setprecision(8);
    out << "{\n";
    out << "  \"landscape\": \"" << firebreak::io::json_escape(instance.landscape_name) << "\",\n";
    out << "  \"n_cells\": " << instance.n_cells << ",\n";
    out << "  \"alpha\": " << instance.alpha << ",\n";
    out << "  \"budget\": " << instance.budget << ",\n";
    out << "  \"eligible_node_count\": " << instance.num_eligible_nodes() << ",\n";
    out << "  \"mapped_node_count\": " << instance.num_mapped_nodes() << ",\n";
    out << "  \"scenario_count\": " << instance.num_scenarios() << ",\n";
    out << "  \"scenario_probabilities\": ";
    write_double_array(out, instance.scenario_probabilities);
    out << ",\n";
    out << "  \"total_arcs\": " << instance.total_arcs << ",\n";
    out << "  \"total_dpv_pairs\": " << instance.total_dpv_pairs << ",\n";
    out << "  \"eligible_original_nodes_sample\": ";
    const auto sample_size = std::min<std::size_t>(20, instance.eligible_original_nodes.size());
    write_int_array(out, std::vector<int>(
        instance.eligible_original_nodes.begin(),
        instance.eligible_original_nodes.begin() + static_cast<std::ptrdiff_t>(sample_size)));
    out << ",\n";
    out << "  \"scenarios\": [\n";
    for (std::size_t i = 0; i < instance.scenarios.size(); ++i) {
        const auto& scenario = instance.scenarios[i];
        out << "    {\n";
        out << "      \"scenario_id\": " << scenario.scenario_id << ",\n";
        out << "      \"probability\": " << scenario.probability << ",\n";
        out << "      \"message_filename\": \""
            << firebreak::io::json_escape(scenario.message_filename) << "\",\n";
        out << "      \"ignition_original_node\": " << scenario.ignition_original_node << ",\n";
        out << "      \"ignition_index\": " << scenario.ignition_index << ",\n";
        out << "      \"arc_count\": " << scenario.arcs.size() << ",\n";
        out << "      \"observed_node_count\": " << scenario.observed_node_indices.size() << ",\n";
        out << "      \"dpv_pair_count\": " << scenario.dpv.num_pairs() << ",\n";
        out << "      \"nodes_with_nonempty_successors\": "
            << scenario.dpv.nodes_with_nonempty_successors << ",\n";
        out << "      \"nodes_with_nonempty_descendants\": "
            << scenario.dpv.nodes_with_nonempty_descendants << ",\n";
        out << "      \"descendants_include_self\": "
            << (scenario.dpv.descendants_include_self ? "true" : "false") << "\n";
        out << "    }" << (i + 1 == instance.scenarios.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"metadata_notes\": [";
    if (!instance.metadata_notes.empty()) {
        out << "\n";
        for (std::size_t i = 0; i < instance.metadata_notes.size(); ++i) {
            out << "    \"" << firebreak::io::json_escape(instance.metadata_notes[i]) << "\""
                << (i + 1 == instance.metadata_notes.size() ? "\n" : ",\n");
        }
        out << "  ]\n";
    } else {
        out << "]\n";
    }
    out << "}\n";
}

}  // namespace

int OptInstanceRunner::run(const OptInstanceOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.scenario_ids.empty()) {
        throw std::runtime_error("--scenario-ids is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
    }

    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());
    const auto output_path = options.output_path.empty()
        ? firebreak::io::resolve_output_path("results/opt_instance_summary.json")
        : firebreak::io::resolve_output_path(options.output_path.string());

    const auto inventory = firebreak::io::detect_message_files(results_path);
    firebreak::io::validate_scenario_ids(inventory, options.scenario_ids);

    std::vector<std::string> reader_warnings;
    firebreak::io::Cell2FireReader reader;
    auto loaded_instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        options.scenario_ids,
        reader_warnings);

    opt::OptimizationInstanceBuilder builder;
    auto opt_instance = builder.build(loaded_instance, options.alpha, true);
    for (const auto& warning : reader_warnings) {
        opt_instance.metadata_notes.push_back("Reader warning: " + warning);
    }

    print_opt_instance_summary(std::cout, opt_instance);
    write_opt_instance_json(output_path, opt_instance);
    std::cout << "Wrote summary: " << firebreak::io::path_to_string(output_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments
