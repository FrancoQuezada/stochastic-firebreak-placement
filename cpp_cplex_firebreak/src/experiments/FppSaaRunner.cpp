#include "experiments/FppSaaRunner.hpp"

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
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/CplexEnvironment.hpp"
#include "solver/FppSaaCplexModel.hpp"

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

void print_solver_summary(
    std::ostream& out,
    const solver::ModelResult& result,
    const eval::InstanceBurnedAreaResult& evaluation) {
    out << "Method: " << result.method << "\n";
    out << "Status: " << result.status << "\n";
    out << std::fixed << std::setprecision(6);
    out << "Objective value: " << result.objective_value << "\n";
    out << "Best bound: " << result.best_bound << "\n";
    out << "MIP gap: " << result.mip_gap << "\n";
    out << "Runtime seconds: " << result.runtime_seconds << "\n";
    out << "Variables: " << result.num_variables << "\n";
    out << "Constraints: " << result.num_constraints << "\n";
    out << "Selected firebreak original nodes:";
    for (const int node : result.selected_firebreak_original_nodes) {
        out << " " << node;
    }
    out << "\n";
    out << "Evaluation expected burned area: " << evaluation.expected_burned_area << "\n";
    out << "Evaluation worst 10% burned area: " << evaluation.worst_10pct_burned_area << "\n";
}

void write_fpp_saa_json(
    const std::filesystem::path& output_path,
    const solver::ModelResult& result,
    const eval::InstanceBurnedAreaResult& evaluation,
    const std::vector<std::string>& notes) {
    firebreak::io::ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open output file: " + output_path.string());
    }

    out << std::fixed << std::setprecision(8);
    out << "{\n";
    out << "  \"optimization_result\": {\n";
    out << "    \"method\": \"" << firebreak::io::json_escape(result.method) << "\",\n";
    out << "    \"status\": \"" << firebreak::io::json_escape(result.status) << "\",\n";
    out << "    \"objective_value\": " << result.objective_value << ",\n";
    out << "    \"best_bound\": " << result.best_bound << ",\n";
    out << "    \"mip_gap\": " << result.mip_gap << ",\n";
    out << "    \"runtime_seconds\": " << result.runtime_seconds << ",\n";
    out << "    \"solver_status_code\": " << result.solver_status_code << ",\n";
    out << "    \"num_variables\": " << result.num_variables << ",\n";
    out << "    \"num_constraints\": " << result.num_constraints << ",\n";
    out << "    \"selected_firebreak_indices\": ";
    write_int_array(out, result.selected_firebreak_indices);
    out << ",\n";
    out << "    \"selected_firebreak_original_nodes\": ";
    write_int_array(out, result.selected_firebreak_original_nodes);
    out << "\n";
    out << "  },\n";
    out << "  \"burned_area_evaluation\": {\n";
    out << "    \"number_of_scenarios\": " << evaluation.number_of_scenarios << ",\n";
    out << "    \"firebreak_count\": " << evaluation.firebreak_count << ",\n";
    out << "    \"expected_burned_area\": " << evaluation.expected_burned_area << ",\n";
    out << "    \"worst_10pct_burned_area\": " << evaluation.worst_10pct_burned_area << ",\n";
    out << "    \"runtime_seconds\": " << evaluation.total_runtime_seconds << ",\n";
    out << "    \"scenarios\": [\n";
    for (std::size_t i = 0; i < evaluation.per_scenario_results.size(); ++i) {
        const auto& scenario = evaluation.per_scenario_results[i];
        out << "      {\n";
        out << "        \"scenario_id\": " << scenario.scenario_id << ",\n";
        out << "        \"message_filename\": \""
            << firebreak::io::json_escape(scenario.message_filename) << "\",\n";
        out << "        \"ignition_node\": " << scenario.ignition_node << ",\n";
        out << "        \"burned_count\": " << scenario.burned_count << "\n";
        out << "      }" << (i + 1 == evaluation.per_scenario_results.size() ? "\n" : ",\n");
    }
    out << "    ]\n";
    out << "  },\n";
    out << "  \"notes\": [";
    if (!notes.empty() || !result.notes.empty()) {
        out << "\n";
        bool first = true;
        for (const auto& note : result.notes) {
            if (!first) {
                out << ",\n";
            }
            out << "    \"" << firebreak::io::json_escape(note) << "\"";
            first = false;
        }
        for (const auto& note : notes) {
            if (!first) {
                out << ",\n";
            }
            out << "    \"" << firebreak::io::json_escape(note) << "\"";
            first = false;
        }
        out << "\n  ]\n";
    } else {
        out << "]\n";
    }
    out << "}\n";
}

}  // namespace

int FppSaaRunner::run(const FppSaaOptions& options) const {
    if (!solver::cplex_support_enabled()) {
        throw std::runtime_error(solver::cplex_unavailable_message());
    }
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
        ? firebreak::io::resolve_output_path("results/fpp_saa_solve.json")
        : firebreak::io::resolve_output_path(options.output_path.string());

    const auto inventory = firebreak::io::detect_message_files(results_path);
    firebreak::io::validate_scenario_ids(inventory, options.scenario_ids);

    std::vector<std::string> notes;
    firebreak::io::Cell2FireReader reader;
    auto loaded_instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        options.scenario_ids,
        notes);

    opt::OptimizationInstanceBuilder builder;
    auto opt_instance = builder.build(loaded_instance, options.alpha, false);

    solver::FppSaaCplexModel model;
    auto result = model.solve(
        opt_instance,
        options.time_limit_seconds,
        options.mip_gap,
        options.threads,
        options.verbose);

    const core::FirebreakSolution firebreaks(result.selected_firebreak_original_nodes);
    const auto evaluation = eval::evaluate_instance_burned_area(loaded_instance, firebreaks);

    print_solver_summary(std::cout, result, evaluation);
    write_fpp_saa_json(output_path, result, evaluation, notes);
    std::cout << "Wrote summary: " << firebreak::io::path_to_string(output_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments

