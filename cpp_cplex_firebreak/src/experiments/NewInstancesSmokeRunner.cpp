#include "experiments/NewInstancesSmokeRunner.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "analysis/GraphDiagnostics.hpp"
#include "core/Scenario.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/ResultWriter.hpp"
#include "io/ScenarioFileUtils.hpp"

namespace firebreak::experiments {

namespace fs = std::filesystem;

namespace {

struct MinMax {
    int min = std::numeric_limits<int>::max();
    int max = 0;

    void observe(int value) {
        min = std::min(min, value);
        max = std::max(max, value);
    }

    int printable_min() const {
        return max == 0 ? 0 : min;
    }
};

struct FolderSummary {
    std::string folder;
    std::string graph_variant;
    std::string layout;
    std::string status = "ok";
    std::string notes;

    int declared_n_cells = 0;
    bool metadata_consistent = true;
    int message_file_count = 0;
    int ignition_record_count = 0;
    int message_min_scenario_id = 0;
    int message_max_scenario_id = 0;
    int ignition_min_scenario_id = 0;
    int ignition_max_scenario_id = 0;
    int missing_messages = 0;
    int missing_ignitions = 0;
    int filename_mismatches = 0;

    int min_source = 0;
    int max_source = 0;
    int min_target = 0;
    int max_target = 0;
    int min_ignition = 0;
    int max_ignition = 0;
    int inferred_n_cells = 0;
    int scenarios_with_ignition_outside_node_universe = 0;
    int scenarios_with_ignition_absent_from_message_graph = 0;

    std::size_t analyzed_scenarios = 0;
    double rooted_tree_ratio = 0.0;
    double acyclic_dag_ratio = 0.0;
    double general_directed_graph_ratio = 0.0;
    double not_fully_reachable_ratio = 0.0;
    double empty_ratio = 0.0;
};

std::string csv_escape(const std::string& value) {
    bool needs_quotes = false;
    for (const char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }
    std::string out = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out += ch;
        }
    }
    out += "\"";
    return out;
}

std::string expected_new_message_filename(int scenario_id) {
    std::ostringstream out;
    out << "MessagesFile";
    out.width(5);
    out.fill('0');
    out << scenario_id << ".csv";
    return out.str();
}

int declared_n_cells_from_folder_name(const fs::path& folder) {
    std::string name = folder.filename().string();
    const std::string suffix = "_reburn";
    if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
        name = name.substr(0, name.size() - suffix.size());
    }
    const auto x_pos = name.find('x');
    if (x_pos == std::string::npos || name.find('x', x_pos + 1) != std::string::npos) {
        return 0;
    }
    try {
        const int rows = std::stoi(name.substr(0, x_pos));
        const int cols = std::stoi(name.substr(x_pos + 1));
        if (rows <= 0 || cols <= 0) {
            return 0;
        }
        return rows * cols;
    } catch (...) {
        return 0;
    }
}

std::string graph_variant_from_folder_name(const fs::path& folder) {
    const std::string name = folder.filename().string();
    const std::string suffix = "_reburn";
    if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
        return "reburn_all_paths";
    }
    return "shortest_path";
}

std::vector<fs::path> discover_instance_folders(const fs::path& instances_root) {
    if (!fs::is_directory(instances_root)) {
        throw std::runtime_error("Missing instances root directory: " + instances_root.string());
    }

    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(instances_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (fs::is_directory(entry.path() / "Messages")) {
            folders.push_back(entry.path());
        }
    }
    std::sort(folders.begin(), folders.end());
    if (folders.empty()) {
        throw std::runtime_error("No new instance folders with a Messages directory found under " + instances_root.string());
    }
    return folders;
}

std::pair<int, int> scenario_min_max(const std::vector<int>& ids) {
    if (ids.empty()) {
        return {0, 0};
    }
    const auto minmax = std::minmax_element(ids.begin(), ids.end());
    return {*minmax.first, *minmax.second};
}

double ratio_for(
    const analysis::GraphDiagnosticsAggregate& aggregate,
    const std::string& classification) {
    if (aggregate.total_scenarios == 0) {
        return 0.0;
    }
    const auto it = aggregate.classification_counts.find(classification);
    const std::size_t count = it == aggregate.classification_counts.end() ? 0 : it->second;
    return static_cast<double>(count) / static_cast<double>(aggregate.total_scenarios);
}

std::string join_notes(const std::vector<std::string>& notes) {
    std::string out;
    for (const auto& note : notes) {
        if (!out.empty()) {
            out += " | ";
        }
        out += note;
    }
    return out;
}

bool has_metadata_inconsistency(const std::vector<std::string>& warnings) {
    return std::any_of(warnings.begin(), warnings.end(), [](const std::string& warning) {
        return warning.find("Metadata inconsistency") != std::string::npos;
    });
}

FolderSummary analyze_folder(const fs::path& folder) {
    FolderSummary summary;
    summary.folder = folder.filename().string();
    summary.graph_variant = graph_variant_from_folder_name(folder);
    summary.declared_n_cells = declared_n_cells_from_folder_name(folder);

    firebreak::io::Cell2FireReader reader;
    std::vector<std::string> warnings;

    const auto inventory = firebreak::io::detect_message_files(folder);
    const auto ignition_metadata = reader.read_ignition_metadata(folder, warnings);
    summary.layout = firebreak::io::layout_name(ignition_metadata.layout);
    summary.message_file_count = inventory.count();
    summary.message_min_scenario_id = inventory.min_id();
    summary.message_max_scenario_id = inventory.max_id();
    summary.ignition_record_count = static_cast<int>(ignition_metadata.records.size());

    std::vector<int> ignition_ids;
    ignition_ids.reserve(ignition_metadata.records.size());
    MinMax ignition_minmax;
    for (const auto& item : ignition_metadata.records) {
        ignition_ids.push_back(item.first);
        ignition_minmax.observe(item.second.ignition_node);
    }
    const auto ignition_id_bounds = scenario_min_max(ignition_ids);
    summary.ignition_min_scenario_id = ignition_id_bounds.first;
    summary.ignition_max_scenario_id = ignition_id_bounds.second;
    summary.min_ignition = ignition_minmax.printable_min();
    summary.max_ignition = ignition_minmax.max;

    std::vector<int> sample_ids;
    if (!inventory.files.empty()) {
        sample_ids.push_back(inventory.files.front().scenario_id);
        auto instance = reader.load_instance(
            summary.folder,
            folder,
            folder,
            sample_ids,
            warnings);
        summary.inferred_n_cells = instance.n_cells;
    }

    for (const auto& item : ignition_metadata.records) {
        if (!inventory.contains(item.first)) {
            ++summary.missing_messages;
        }
    }

    MinMax source_minmax;
    MinMax target_minmax;
    std::vector<analysis::ScenarioGraphDiagnostics> graph_diagnostics;
    graph_diagnostics.reserve(inventory.files.size());

    for (const auto& file : inventory.files) {
        const auto ignition_it = ignition_metadata.records.find(file.scenario_id);
        if (ignition_it == ignition_metadata.records.end()) {
            ++summary.missing_ignitions;
            continue;
        }
        if (ignition_metadata.layout == firebreak::io::Cell2FireLayout::NewInstances &&
            file.filename != expected_new_message_filename(file.scenario_id)) {
            ++summary.filename_mismatches;
        }

        std::vector<std::string> graph_warnings;
        auto graph = reader.read_message_graph(file.path, graph_warnings);
        for (const auto& edge : graph.edges()) {
            source_minmax.observe(edge.from);
            target_minmax.observe(edge.to);
        }

        const int ignition = ignition_it->second.ignition_node;
        if (summary.inferred_n_cells > 0 &&
            (ignition < 1 || ignition > summary.inferred_n_cells)) {
            ++summary.scenarios_with_ignition_outside_node_universe;
        }
        if (!graph.has_node(ignition)) {
            ++summary.scenarios_with_ignition_absent_from_message_graph;
        }

        core::Scenario scenario;
        scenario.scenario_id = file.scenario_id;
        scenario.message_filename = file.filename;
        scenario.ignition_node = ignition;
        scenario.weather_metadata = ignition_it->second.weather;
        scenario.propagation_graph = std::move(graph);
        graph_diagnostics.push_back(analysis::analyze_scenario_graph(scenario));
    }

    summary.min_source = source_minmax.printable_min();
    summary.max_source = source_minmax.max;
    summary.min_target = target_minmax.printable_min();
    summary.max_target = target_minmax.max;

    const auto aggregate = analysis::aggregate_graph_diagnostics(graph_diagnostics);
    summary.analyzed_scenarios = aggregate.total_scenarios;
    summary.rooted_tree_ratio = ratio_for(aggregate, "rooted_arborescence");
    summary.acyclic_dag_ratio = ratio_for(aggregate, "dag_not_tree");
    summary.general_directed_graph_ratio = ratio_for(aggregate, "general_directed_graph");
    summary.not_fully_reachable_ratio = ratio_for(aggregate, "not_fully_reachable_from_ignition");
    summary.empty_ratio = ratio_for(aggregate, "empty_or_invalid");

    if (summary.filename_mismatches > 0) {
        warnings.push_back(
            "New-instances filename validation found " +
            std::to_string(summary.filename_mismatches) +
            " scenarios whose file name does not match MessagesFile%05d.csv.");
    }
    if (has_metadata_inconsistency(warnings)) {
        summary.status = "metadata_warning";
        summary.metadata_consistent = false;
    }
    if (summary.declared_n_cells > 0 &&
        summary.inferred_n_cells > 0 &&
        summary.declared_n_cells != summary.inferred_n_cells) {
        summary.metadata_consistent = false;
        if (summary.status == "ok") {
            summary.status = "metadata_warning";
        }
    }
    if (!warnings.empty()) {
        summary.notes = join_notes(warnings);
    }
    return summary;
}

void write_csv(const fs::path& output_path, const std::vector<FolderSummary>& summaries) {
    firebreak::io::ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open output CSV: " + output_path.string());
    }

    out << std::fixed << std::setprecision(8);
    out << "folder,graph_variant,layout,status,declared_n_cells,inferred_n_cells,metadata_consistent,"
        << "message_files_detected,ignition_records_detected,"
        << "message_min_scenario_id,message_max_scenario_id,"
        << "ignition_min_scenario_id,ignition_max_scenario_id,"
        << "min_source,max_source,min_target,max_target,min_ignition,max_ignition,"
        << "missing_messages,missing_ignitions,filename_mismatches,"
        << "scenarios_with_ignition_outside_node_universe,"
        << "scenarios_with_ignition_absent_from_message_graph,"
        << "analyzed_scenarios,rooted_tree_ratio,acyclic_dag_ratio,"
        << "general_directed_graph_ratio,not_fully_reachable_ratio,empty_ratio,notes\n";

    for (const auto& summary : summaries) {
        out << csv_escape(summary.folder) << ","
            << csv_escape(summary.graph_variant) << ","
            << csv_escape(summary.layout) << ","
            << csv_escape(summary.status) << ","
            << summary.declared_n_cells << ","
            << summary.inferred_n_cells << ","
            << (summary.metadata_consistent ? "true" : "false") << ","
            << summary.message_file_count << ","
            << summary.ignition_record_count << ","
            << summary.message_min_scenario_id << ","
            << summary.message_max_scenario_id << ","
            << summary.ignition_min_scenario_id << ","
            << summary.ignition_max_scenario_id << ","
            << summary.min_source << ","
            << summary.max_source << ","
            << summary.min_target << ","
            << summary.max_target << ","
            << summary.min_ignition << ","
            << summary.max_ignition << ","
            << summary.missing_messages << ","
            << summary.missing_ignitions << ","
            << summary.filename_mismatches << ","
            << summary.scenarios_with_ignition_outside_node_universe << ","
            << summary.scenarios_with_ignition_absent_from_message_graph << ","
            << summary.analyzed_scenarios << ","
            << summary.rooted_tree_ratio << ","
            << summary.acyclic_dag_ratio << ","
            << summary.general_directed_graph_ratio << ","
            << summary.not_fully_reachable_ratio << ","
            << summary.empty_ratio << ","
            << csv_escape(summary.notes) << "\n";
    }
}

}  // namespace

int NewInstancesSmokeRunner::run(const NewInstancesSmokeOptions& options) const {
    if (options.instances_root.empty()) {
        throw std::runtime_error("--instances-root is required.");
    }
    const auto instances_root = firebreak::io::resolve_input_path(options.instances_root.string());
    const auto output_path = options.output_path.empty()
        ? firebreak::io::resolve_output_path("results/new_instances_smoke_summary.csv")
        : firebreak::io::resolve_output_path(options.output_path.string());

    const auto folders = discover_instance_folders(instances_root);
    std::vector<FolderSummary> summaries;
    summaries.reserve(folders.size());

    bool has_error = false;
    bool has_metadata_warning = false;
    for (const auto& folder : folders) {
        std::cout << "Analyzing new instance folder: " << firebreak::io::path_to_string(folder) << "\n";
        try {
            auto summary = analyze_folder(folder);
            if (summary.status == "metadata_warning") {
                has_metadata_warning = true;
            }
            summaries.push_back(std::move(summary));
        } catch (const std::exception& ex) {
            FolderSummary summary;
            summary.folder = folder.filename().string();
            summary.status = "error";
            summary.notes = ex.what();
            summaries.push_back(std::move(summary));
            has_error = true;
        }
    }

    write_csv(output_path, summaries);
    std::cout << "Wrote new-instances smoke summary: " << firebreak::io::path_to_string(output_path) << "\n";

    if (has_error || (options.strict_metadata && has_metadata_warning)) {
        return 2;
    }
    return 0;
}

}  // namespace firebreak::experiments
