#include "experiments/DpvSaaOutOfSampleRunner.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

#include "analysis/GraphDiagnostics.hpp"
#include "core/FirebreakSolution.hpp"
#include "eval/BurnedAreaEvaluator.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/ExperimentResultWriter.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "io/ScenarioSplitUtils.hpp"
#include "io/SolutionIO.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "opt/WeightedDpvScoring.hpp"
#include "solver/CplexEnvironment.hpp"
#include "solver/DpvSaaCplexModel.hpp"
#include "solver/FppWeightedLossUtils.hpp"
#include "solver/WarmStart.hpp"

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
    return firebreak::io::resolve_output_path("results/experiments/dpv_saa_oos_results.csv");
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
    std::cout << "Objective in-sample: " << result.objective_in_sample << "\n";
    std::cout << "Best bound: " << result.best_bound << "\n";
    std::cout << "MIP gap: " << result.mip_gap << "\n";
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

int DpvSaaOutOfSampleRunner::run(const DpvSaaOutOfSampleOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
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
    if (!solver::cplex_support_enabled()) {
        throw std::runtime_error(solver::cplex_unavailable_message());
    }

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
    solver::attach_weight_map_to_optimization_instance(opt_instance, options.weight_map_file);

    solver::WarmStart warm_start;
    const solver::WarmStart* warm_start_ptr = nullptr;
    if (!options.warm_start_solution_path.empty()) {
        const auto warm_start_path = firebreak::io::resolve_input_path(options.warm_start_solution_path.string());
        warm_start = solver::load_warm_start_from_csv(warm_start_path, opt_instance, opt_instance.budget);
        warm_start_ptr = &warm_start;
    }

    solver::DpvSaaCplexModel model;
    auto solve_result = model.solve(
        opt_instance,
        options.time_limit_seconds,
        options.mip_gap,
        options.threads,
        options.verbose,
        warm_start_ptr,
        opt::parse_weighted_dpv_ignition_policy(options.dpv_ignition_policy));

    io::FirebreakSolutionRecord solution_record;
    solution_record.method = "DPV-SAA";
    solution_record.landscape = options.landscape;
    solution_record.alpha = options.alpha;
    solution_record.budget = opt_instance.budget;
    solution_record.selected_firebreak_original_nodes = solve_result.selected_firebreak_original_nodes;
    solution_record.selected_firebreak_indices = solve_result.selected_firebreak_indices;
    firebreak::io::save_firebreak_solution_json(solution_json_path, solution_record);
    firebreak::io::save_firebreak_solution_csv(solution_csv_path, solve_result.selected_firebreak_original_nodes);

    const core::FirebreakSolution firebreaks(solve_result.selected_firebreak_original_nodes);
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
    auto test_opt_instance = builder.build(test_instance, options.alpha, true);
    solver::attach_weight_map_to_optimization_instance(test_opt_instance, options.weight_map_file);
    const auto train_recourse =
        eval::FppRecourseEvaluator(opt_instance).evaluate(solve_result.selected_firebreak_indices);
    std::vector<int> test_selected_compact;
    for (const int original_node : solve_result.selected_firebreak_original_nodes) {
        if (test_opt_instance.node_mapper.contains_node(original_node)) {
            test_selected_compact.push_back(test_opt_instance.node_mapper.to_index(original_node));
        }
    }
    const auto test_recourse =
        eval::FppRecourseEvaluator(test_opt_instance).evaluate(test_selected_compact);

    io::StandardExperimentResult result;
    result.run_id = options.run_id;
    result.timestamp = io::current_timestamp_utc();
    result.landscape = options.landscape;
    result.method = "DPV-SAA";
    result.objective_metric = "weighted_solution_dependent_DPV_product_pair_loss";
    result.alpha = options.alpha;
    result.budget = opt_instance.budget;
    result.train_scenario_count = static_cast<int>(split.train_ids.size());
    result.test_scenario_count = static_cast<int>(split.test_ids.size());
    result.train_ids = split.train_ids;
    result.test_ids = split.test_ids;
    result.solver_status = solve_result.status;
    result.objective_in_sample = solve_result.objective_value;
    result.best_bound = solve_result.best_bound;
    result.mip_gap = solve_result.mip_gap;
    result.runtime_seconds = solve_result.runtime_seconds;
    result.solver_status_code = solve_result.solver_status_code;
    result.num_variables = solve_result.num_variables;
    result.num_constraints = solve_result.num_constraints;
    result.selected_firebreaks = solve_result.selected_firebreak_original_nodes;
    result.warm_start_used = solve_result.warm_start_used;
    result.warm_start_source = solve_result.warm_start_source;
    result.warm_start_valid_nodes = solve_result.warm_start_valid_nodes;
    result.warm_start_ignored_nodes = solve_result.warm_start_ignored_nodes;
    result.warm_start_notes = solve_result.warm_start_notes;
    result.weight_profile = train_recourse.weight_profile;
    result.weight_map_file = options.weight_map_file.empty() ? "" : options.weight_map_file.string();
    result.weight_map_hash = train_recourse.weight_map_hash;
    result.weight_total = train_recourse.total_landscape_weight;
    // A DPV decomposition solves the DPV surrogate, not the true weighted
    // wildfire loss: never copy the surrogate value into the generic
    // solver_weighted_objective field (Phase 10 section 10; matches the
    // existing Greedy/Static-DPV convention of leaving this field NaN for
    // non-exact-FPP methods). The surrogate itself is reported separately
    // via dpv_surrogate_objective below, and the true evaluated loss via
    // evaluator_weighted_objective.
    result.solver_weighted_objective = std::numeric_limits<double>::quiet_NaN();
    result.evaluator_weighted_objective = train_recourse.expected_weighted_burn_loss;
    result.train_expected_weighted_burn_loss = train_recourse.expected_weighted_burn_loss;
    result.test_expected_weighted_burn_loss = test_recourse.expected_weighted_burn_loss;
    result.train_weighted_var = train_recourse.weighted_loss_statistics.var;
    result.test_weighted_var = test_recourse.weighted_loss_statistics.var;
    result.train_weighted_cvar = train_recourse.weighted_loss_statistics.cvar;
    result.test_weighted_cvar = test_recourse.weighted_loss_statistics.cvar;
    result.dpv_weighted = solve_result.dpv_model_weighted;
    result.dpv_model_weighted = solve_result.dpv_model_weighted;
    result.dpv_model_type = solve_result.dpv_model_type;
    result.dpv_variant = solve_result.dpv_variant;
    result.dpv_structural_definition = solve_result.dpv_structural_definition;
    result.dpv_ignition_policy = solve_result.dpv_ignition_policy;
    result.dpv_weight_profile = solve_result.dpv_weight_profile;
    result.dpv_weight_map_hash = solve_result.dpv_weight_map_hash;
    result.dpv_scenario_aggregation = solve_result.dpv_scenario_aggregation;
    result.dpv_normalization = solve_result.dpv_normalization;
    result.dpv_risk_measure = solve_result.dpv_risk_measure;
    result.dpv_surrogate_objective = solve_result.dpv_surrogate_objective;
    result.dpv_surrogate_best_bound = solve_result.dpv_surrogate_best_bound;
    result.dpv_surrogate_gap = solve_result.dpv_surrogate_gap;
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
    result.notes = solve_result.notes;
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
