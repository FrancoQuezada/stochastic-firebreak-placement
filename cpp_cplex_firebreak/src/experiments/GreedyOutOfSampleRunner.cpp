#include "experiments/GreedyOutOfSampleRunner.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "analysis/GraphDiagnostics.hpp"
#include "core/FirebreakSolution.hpp"
#include "eval/BurnedAreaEvaluator.hpp"
#include "heuristics/GreedyHeuristic.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/ExperimentResultWriter.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "io/ScenarioSplitUtils.hpp"
#include "io/SolutionIO.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"

namespace firebreak::experiments {

namespace {

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

void validate_disjoint(const std::vector<int>& train_ids, const std::vector<int>& test_ids) {
    std::unordered_set<int> train_set(train_ids.begin(), train_ids.end());
    for (const int id : test_ids) {
        if (train_set.find(id) != train_set.end()) {
            throw std::runtime_error("Train and test scenario IDs must be disjoint.");
        }
    }
}

std::filesystem::path default_experiment_json_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + ".json");
}

std::filesystem::path default_experiment_csv_path() {
    return firebreak::io::resolve_output_path("results/experiments/greedy_oos_results.csv");
}

std::filesystem::path default_solution_json_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + "_solution.json");
}

std::filesystem::path default_solution_csv_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + "_solution.csv");
}

void print_summary(
    const io::StandardExperimentResult& result,
    const std::filesystem::path& solution_json_path,
    const std::filesystem::path& solution_csv_path) {
    std::cout << "Run ID: " << result.run_id << "\n";
    std::cout << "Landscape: " << result.landscape << "\n";
    std::cout << "Method: " << result.method << "\n";
    std::cout << "Objective metric: " << result.objective_metric << "\n";
    std::cout << "Graph type note: " << result.graph_type_note << "\n";
    std::cout << "Train graph type ratios: " << result.train_graph_classification_ratios << "\n";
    std::cout << "Test graph type ratios: " << result.test_graph_classification_ratios << "\n";
    std::cout << "Train scenarios: " << result.train_scenario_count << "\n";
    std::cout << "Test scenarios: " << result.test_scenario_count << "\n";
    std::cout << "Solver status: " << result.solver_status << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Total greedy selected-node score: " << result.objective_in_sample << "\n";
    std::cout << "Runtime seconds: " << result.runtime_seconds << "\n";
    std::cout << "Selected firebreaks:";
    for (const int node : result.selected_firebreaks) {
        std::cout << " " << node;
    }
    std::cout << "\n";
    std::cout << "Train expected burned area: " << result.train_expected_burned_area << "\n";
    std::cout << "Train worst 10% burned area: " << result.train_worst_10pct_burned_area << "\n";
    std::cout << "Test expected burned area: " << result.test_expected_burned_area << "\n";
    std::cout << "Test worst 10% burned area: " << result.test_worst_10pct_burned_area << "\n";
    std::cout << "Train evaluation runtime seconds: " << result.train_evaluation_runtime_seconds << "\n";
    std::cout << "Test scenario loading runtime seconds: " << result.test_scenario_loading_runtime_seconds << "\n";
    std::cout << "Test evaluation runtime seconds: " << result.test_evaluation_runtime_seconds << "\n";
    std::cout << "Solution JSON: " << firebreak::io::path_to_string(solution_json_path) << "\n";
    std::cout << "Solution CSV: " << firebreak::io::path_to_string(solution_csv_path) << "\n";
}

}  // namespace

int GreedyOutOfSampleRunner::run(const GreedyOutOfSampleOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
    }
    if (options.metric.empty()) {
        throw std::runtime_error("--metric is required.");
    }
    if (options.run_id.empty()) {
        throw std::runtime_error("--run-id is required.");
    }
    if (options.use_generated_split) {
        if (!options.train_ids.empty() || !options.test_ids.empty()) {
            throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
        }
        if (options.train_count == 0 || options.test_count == 0) {
            throw std::runtime_error("--train-count and --test-count must be positive for generated split mode.");
        }
    } else if (options.train_ids.empty() || options.test_ids.empty()) {
        throw std::runtime_error("Explicit mode requires --train-ids and --test-ids.");
    }

    const auto metric = heuristics::parseGreedyMetricType(options.metric);

    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());

    const auto output_json_path = options.output_json_path.empty()
        ? default_experiment_json_path(options.run_id)
        : firebreak::io::resolve_output_path(options.output_json_path.string());
    const auto output_csv_path = options.output_csv_path.empty()
        ? default_experiment_csv_path()
        : firebreak::io::resolve_output_path(options.output_csv_path.string());
    const auto solution_json_path = options.solution_json_path.empty()
        ? default_solution_json_path(options.run_id)
        : firebreak::io::resolve_output_path(options.solution_json_path.string());
    const auto solution_csv_path = options.solution_csv_path.empty()
        ? default_solution_csv_path(options.run_id)
        : firebreak::io::resolve_output_path(options.solution_csv_path.string());

    const auto inventory = firebreak::io::detect_message_files(results_path);

    io::ScenarioSplit split;
    std::vector<std::string> notes;
    if (options.use_generated_split) {
        split = firebreak::io::generate_train_test_split(
            inventory.ids(),
            options.seed,
            options.train_count,
            options.test_count);
        firebreak::io::save_train_test_split(
            firebreak::io::resolve_output_path("results/splits"),
            options.landscape,
            options.seed,
            options.train_count,
            options.test_count,
            split);
        notes.push_back("Generated deterministic train/test split with seed " + std::to_string(options.seed) + ".");
    } else {
        split.train_ids = options.train_ids;
        split.test_ids = options.test_ids;
        notes.push_back("Used explicit train/test scenario IDs.");
    }

    validate_disjoint(split.train_ids, split.test_ids);
    firebreak::io::validate_scenario_ids(inventory, split.train_ids);
    firebreak::io::validate_scenario_ids(inventory, split.test_ids);

    std::vector<std::string> train_warnings;
    firebreak::io::Cell2FireReader reader;
    auto train_instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        split.train_ids,
        train_warnings);

    opt::OptimizationInstanceBuilder builder;
    auto opt_instance = builder.build(train_instance, options.alpha, true);

    heuristics::GreedyHeuristic greedy;
    const auto greedy_result = greedy.runGreedy(opt_instance, metric, true, options.verbose);

    io::FirebreakSolutionRecord solution_record;
    solution_record.method = greedy_result.method_name;
    solution_record.landscape = options.landscape;
    solution_record.alpha = options.alpha;
    solution_record.budget = opt_instance.budget;
    solution_record.selected_firebreak_original_nodes = greedy_result.selected_firebreak_original_nodes;
    solution_record.selected_firebreak_indices = greedy_result.selected_firebreak_indices;
    solution_record.objective_metric = greedy_result.objective_metric;
    solution_record.selected_firebreak_scores = greedy_result.selected_scores;
    firebreak::io::save_firebreak_solution_json(solution_json_path, solution_record);
    firebreak::io::save_firebreak_solution_csv(
        solution_csv_path,
        greedy_result.selected_firebreak_original_nodes);

    const core::FirebreakSolution firebreaks(greedy_result.selected_firebreak_original_nodes);
    const auto train_eval = eval::evaluate_instance_burned_area(train_instance, firebreaks);

    std::vector<std::string> test_warnings;
    const auto test_load_start = std::chrono::steady_clock::now();
    auto test_instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        split.test_ids,
        test_warnings);
    const auto test_load_end = std::chrono::steady_clock::now();
    const double test_loading_seconds = std::chrono::duration<double>(test_load_end - test_load_start).count();
    const auto test_eval = eval::evaluate_instance_burned_area(test_instance, firebreaks);

    io::StandardExperimentResult result;
    result.run_id = options.run_id;
    result.timestamp = io::current_timestamp_utc();
    result.landscape = options.landscape;
    result.method = greedy_result.method_name;
    result.objective_metric = greedy_result.objective_metric;
    result.alpha = options.alpha;
    result.budget = opt_instance.budget;
    result.train_scenario_count = static_cast<int>(split.train_ids.size());
    result.test_scenario_count = static_cast<int>(split.test_ids.size());
    result.train_ids = split.train_ids;
    result.test_ids = split.test_ids;
    result.solver_status = "NotApplicable";
    result.objective_in_sample = greedy_result.total_score;
    result.best_bound = std::numeric_limits<double>::quiet_NaN();
    result.mip_gap = std::numeric_limits<double>::quiet_NaN();
    result.runtime_seconds = greedy_result.runtime_seconds;
    result.solver_status_code = 0;
    result.num_variables = 0;
    result.num_constraints = 0;
    result.selected_firebreaks = greedy_result.selected_firebreak_original_nodes;
    result.train_expected_burned_area = train_eval.expected_burned_area;
    result.train_worst_10pct_burned_area = train_eval.worst_10pct_burned_area;
    result.test_expected_burned_area = test_eval.expected_burned_area;
    result.test_worst_10pct_burned_area = test_eval.worst_10pct_burned_area;
    result.train_empirical_var_burned_area = train_eval.empirical_var_90pct_burned_area;
    result.train_empirical_cvar_burned_area = train_eval.empirical_cvar_90pct_burned_area;
    result.test_empirical_var_burned_area = test_eval.empirical_var_90pct_burned_area;
    result.test_empirical_cvar_burned_area = test_eval.empirical_cvar_90pct_burned_area;
    result.train_evaluation_runtime_seconds = train_eval.total_runtime_seconds;
    result.test_evaluation_runtime_seconds = test_eval.total_runtime_seconds;
    result.test_scenario_loading_runtime_seconds = test_loading_seconds;
    result.train_graph_classification_ratios =
        analysis::graph_classification_ratio_summary(train_instance.scenarios);
    result.test_graph_classification_ratios =
        analysis::graph_classification_ratio_summary(test_instance.scenarios);
    result.notes = greedy_result.metric_notes;
    result.notes.push_back("Greedy scores are computed from training scenarios only.");
    result.notes.push_back("Tie-breaking uses larger score first, then smaller original Cell2Fire node ID.");
    result.notes.insert(result.notes.end(), notes.begin(), notes.end());
    for (const auto& warning : train_warnings) {
        result.notes.push_back("Train reader warning: " + warning);
    }
    for (const auto& warning : test_warnings) {
        result.notes.push_back("Test reader warning: " + warning);
    }
    result.notes.push_back("Test scenarios were used only for out-of-sample evaluation.");

    firebreak::io::write_experiment_result_json(output_json_path, result);
    firebreak::io::append_experiment_result_csv(output_csv_path, result);

    print_summary(result, solution_json_path, solution_csv_path);
    std::cout << "Wrote result JSON: " << firebreak::io::path_to_string(output_json_path) << "\n";
    std::cout << "Appended result CSV: " << firebreak::io::path_to_string(output_csv_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments
