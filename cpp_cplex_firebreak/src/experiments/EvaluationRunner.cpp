#include "experiments/EvaluationRunner.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "core/FirebreakSolution.hpp"
#include "eval/BurnedAreaEvaluator.hpp"
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

void print_evaluation_summary(
    std::ostream& out,
    const core::Instance& instance,
    const eval::InstanceBurnedAreaResult& result,
    const std::vector<std::string>& warnings) {
    out << "Landscape: " << instance.landscape_name << "\n";
    out << "Scenarios: " << result.number_of_scenarios << "\n";
    out << "Firebreak count: " << result.firebreak_count << "\n";
    out << std::fixed << std::setprecision(6);
    out << "Expected burned area: " << result.expected_burned_area << "\n";
    out << "Worst 10% burned area: " << result.worst_10pct_burned_area << "\n";
    out << "Runtime seconds: " << result.total_runtime_seconds << "\n";
    for (const auto& scenario_result : result.per_scenario_results) {
        out << "Scenario " << scenario_result.scenario_id
            << " | message=" << scenario_result.message_filename
            << " | ignition=" << scenario_result.ignition_node
            << " | ignition_is_firebreak=" << (scenario_result.ignition_is_firebreak ? "true" : "false")
            << " | burned=" << scenario_result.burned_count << "\n";
    }
    for (const auto& warning : warnings) {
        out << "Warning: " << warning << "\n";
    }
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

void write_evaluation_json(
    const std::filesystem::path& output_path,
    const core::Instance& instance,
    const core::FirebreakSolution& firebreaks,
    const eval::InstanceBurnedAreaResult& result,
    const std::vector<std::string>& warnings) {
    firebreak::io::ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open output file: " + output_path.string());
    }

    out << std::fixed << std::setprecision(8);
    out << "{\n";
    out << "  \"landscape\": \"" << firebreak::io::json_escape(instance.landscape_name) << "\",\n";
    out << "  \"forest_path\": \"" << firebreak::io::json_escape(firebreak::io::path_to_string(instance.forest_path)) << "\",\n";
    out << "  \"results_path\": \"" << firebreak::io::json_escape(firebreak::io::path_to_string(instance.results_path)) << "\",\n";
    out << "  \"number_of_scenarios\": " << result.number_of_scenarios << ",\n";
    out << "  \"firebreak_count\": " << result.firebreak_count << ",\n";
    out << "  \"firebreak_nodes\": ";
    write_int_array(out, firebreaks.sorted_nodes());
    out << ",\n";
    out << "  \"expected_burned_area\": " << result.expected_burned_area << ",\n";
    out << "  \"worst_10pct_burned_area\": " << result.worst_10pct_burned_area << ",\n";
    out << "  \"total_runtime_seconds\": " << result.total_runtime_seconds << ",\n";
    out << "  \"scenarios\": [\n";
    for (std::size_t i = 0; i < result.per_scenario_results.size(); ++i) {
        const auto& scenario_result = result.per_scenario_results[i];
        out << "    {\n";
        out << "      \"scenario_id\": " << scenario_result.scenario_id << ",\n";
        out << "      \"message_filename\": \""
            << firebreak::io::json_escape(scenario_result.message_filename) << "\",\n";
        out << "      \"ignition_node\": " << scenario_result.ignition_node << ",\n";
        out << "      \"ignition_is_firebreak\": "
            << (scenario_result.ignition_is_firebreak ? "true" : "false") << ",\n";
        out << "      \"burned_count\": " << scenario_result.burned_count << ",\n";
        out << "      \"burned_nodes\": ";
        write_int_array(out, scenario_result.burned_nodes);
        out << "\n";
        out << "    }" << (i + 1 == result.per_scenario_results.size() ? "\n" : ",\n");
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

int EvaluationRunner::run(const EvaluationOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.scenario_ids.empty()) {
        throw std::runtime_error("--scenario-ids is required.");
    }

    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());
    const auto output_path = options.output_path.empty()
        ? firebreak::io::resolve_output_path("results/evaluation_summary.json")
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

    const auto firebreaks = core::FirebreakSolution::from_csv(options.firebreaks_csv);
    const auto result = eval::evaluate_instance_burned_area(instance, firebreaks);

    print_evaluation_summary(std::cout, instance, result, warnings);
    write_evaluation_json(output_path, instance, firebreaks, result, warnings);
    std::cout << "Wrote summary: " << firebreak::io::path_to_string(output_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments

