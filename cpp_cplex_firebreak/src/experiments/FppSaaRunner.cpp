#include "experiments/FppSaaRunner.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "core/FirebreakSolution.hpp"
#include "eval/BurnedAreaEvaluator.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/ResultWriter.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/CplexEnvironment.hpp"
#include "solver/FppSaaCplexModel.hpp"
#include "solver/FppWeightedLossUtils.hpp"

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
    const eval::InstanceBurnedAreaResult& evaluation,
    const eval::FppRecourseResult& weighted_evaluation) {
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
    out << "Weight profile: " << weighted_evaluation.weight_profile << "\n";
    out << "Weight map hash: " << weighted_evaluation.weight_map_hash << "\n";
    out << "Evaluation expected weighted burn loss: "
        << weighted_evaluation.expected_weighted_burn_loss << "\n";
    out << "Weighted objective validation: " << result.validation_status << "\n";
}

void write_fpp_saa_json(
    const std::filesystem::path& output_path,
    const solver::ModelResult& result,
    const eval::InstanceBurnedAreaResult& evaluation,
    const eval::FppRecourseResult& weighted_evaluation,
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
    out << "    \"objective_metric\": \""
        << firebreak::io::json_escape(result.objective_metric) << "\",\n";
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
    out << ",\n";
    out << "    \"weight_profile\": \""
        << firebreak::io::json_escape(result.weight_profile) << "\",\n";
    out << "    \"weight_map_file\": \""
        << firebreak::io::json_escape(result.weight_map_file) << "\",\n";
    out << "    \"weight_map_hash\": \""
        << firebreak::io::json_escape(result.weight_map_hash) << "\",\n";
    out << "    \"weight_normalized\": "
        << (result.weight_normalized ? "true" : "false") << ",\n";
    out << "    \"weight_mean\": " << result.weight_mean << ",\n";
    out << "    \"weight_min\": " << result.weight_min << ",\n";
    out << "    \"weight_max\": " << result.weight_max << ",\n";
    out << "    \"weight_total\": " << result.weight_total << ",\n";
    out << "    \"solver_weighted_objective\": "
        << result.solver_weighted_objective << ",\n";
    out << "    \"evaluator_weighted_objective\": "
        << result.evaluator_weighted_objective << ",\n";
    out << "    \"objective_validation_abs_difference\": "
        << result.objective_validation_abs_difference << ",\n";
    out << "    \"objective_validation_rel_difference\": "
        << result.objective_validation_rel_difference << ",\n";
    out << "    \"objective_validation_passed\": "
        << (result.objective_validation_passed ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"weighted_evaluation\": {\n";
    out << "    \"expected_weighted_burn_loss\": "
        << weighted_evaluation.expected_weighted_burn_loss << ",\n";
    out << "    \"weighted_var\": "
        << weighted_evaluation.weighted_loss_statistics.var << ",\n";
    out << "    \"weighted_cvar\": "
        << weighted_evaluation.weighted_loss_statistics.cvar << ",\n";
    out << "    \"percentage_landscape_value_burned\": "
        << weighted_evaluation.expected_percentage_landscape_value_burned << ",\n";
    out << "    \"percentage_high_value_weight_burned\": "
        << weighted_evaluation.expected_percentage_high_value_weight_burned << "\n";
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
    const auto resolved_weight_map_path = options.weight_map_file.empty()
        ? std::filesystem::path()
        : firebreak::io::resolve_input_path(options.weight_map_file.string());
    solver::attach_weight_map_to_optimization_instance(opt_instance, resolved_weight_map_path);

    solver::FppSaaCplexModel model;
    auto result = model.solve(
        opt_instance,
        options.time_limit_seconds,
        options.mip_gap,
        options.threads,
        options.verbose);
    solver::attach_direct_fpp_weight_metadata(result, opt_instance, resolved_weight_map_path);

    const core::FirebreakSolution firebreaks(result.selected_firebreak_original_nodes);
    const auto evaluation = eval::evaluate_instance_burned_area(loaded_instance, firebreaks);
    eval::FppRecourseEvaluator weighted_evaluator(opt_instance);
    const auto weighted_evaluation =
        weighted_evaluator.evaluate(result.selected_firebreak_indices, true);
    const double evaluator_weighted_objective =
        solver::weighted_objective_from_recourse(
            weighted_evaluation,
            firebreak::risk::RiskMeasureConfig());
    solver::attach_direct_fpp_validation(result, evaluator_weighted_objective);
    if (result.weight_map_hash != weighted_evaluation.weight_map_hash) {
        throw std::runtime_error(
            "Optimization and evaluation weight map hashes differ in solve-fpp-saa.");
    }

    print_solver_summary(std::cout, result, evaluation, weighted_evaluation);
    write_fpp_saa_json(output_path, result, evaluation, weighted_evaluation, notes);
    std::cout << "Wrote summary: " << firebreak::io::path_to_string(output_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments
