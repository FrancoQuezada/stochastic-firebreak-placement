#include "experiments/DpvBranchBendersOutOfSampleRunner.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

#include "analysis/GraphDiagnostics.hpp"
#include "benders/DpvBranchBendersSolver.hpp"
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
    return firebreak::io::resolve_output_path("results/experiments/dpv_branch_benders_oos_results.csv");
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
    std::cout << "Lazy cuts added: " << result.branch_benders_lazy_cuts_added << "\n";
    std::cout << "Root user cuts added: "
              << result.branch_benders_root_user_cuts_added << "\n";
    std::cout << "Root user cut rounds: "
              << result.branch_benders_root_user_cut_rounds_executed << "\n";
    std::cout << "Candidate incumbents checked: "
              << result.branch_benders_candidate_incumbents_checked << "\n";
    std::cout << "Max final cut violation: " << result.branch_benders_max_cut_violation << "\n";
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
    std::cout << "Solution JSON: " << firebreak::io::path_to_string(solution_json_path) << "\n";
    std::cout << "Solution CSV: " << firebreak::io::path_to_string(solution_csv_path) << "\n";
}

}  // namespace

int DpvBranchBendersOutOfSampleRunner::run(
    const DpvBranchBendersOutOfSampleOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
    }
    if (options.run_id.empty()) {
        throw std::runtime_error("--run-id is required.");
    }
    if (options.tolerance < 0.0) {
        throw std::runtime_error("--tolerance must be nonnegative.");
    }
    if (options.use_root_user_cuts && options.root_user_cut_max_rounds <= 0) {
        throw std::runtime_error("--root-user-cut-max-rounds must be positive when root user cuts are enabled.");
    }
    if (std::isfinite(options.root_user_cut_tolerance) &&
        options.root_user_cut_tolerance < 0.0) {
        throw std::runtime_error("--root-user-cut-tolerance must be nonnegative.");
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

    benders::DpvBranchBendersOptions solver_options;
    solver_options.tolerance = options.tolerance;
    solver_options.time_limit_seconds = options.time_limit_seconds;
    solver_options.mip_gap = options.mip_gap;
    solver_options.threads = options.threads;
    solver_options.verbose = options.verbose;
    solver_options.use_lifted_lower_bounds = options.use_lifted_lower_bounds;
    solver_options.use_root_user_cuts = options.use_root_user_cuts;
    solver_options.root_user_cut_max_rounds = options.root_user_cut_max_rounds;
    solver_options.root_user_cut_tolerance = options.root_user_cut_tolerance;
    solver_options.dpv_ignition_policy =
        opt::parse_weighted_dpv_ignition_policy(options.dpv_ignition_policy);
    solver_options.warm_start = warm_start_ptr;

    benders::DpvBranchBendersSolver solver;
    auto solve_result = solver.solve(opt_instance, solver_options);

    io::FirebreakSolutionRecord solution_record;
    solution_record.method = "DPV-SAA-Branch-Benders";
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
    result.method = "DPV-SAA-Branch-Benders";
    result.objective_metric = "weighted_solution_dependent_DPV_product_pair_loss_branch_benders";
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
    result.explored_nodes = solve_result.explored_nodes;
    result.num_variables = solve_result.num_variables;
    result.num_constraints = solve_result.num_constraints;
    result.solver_iterations = solve_result.iterations;
    result.cuts_added = solve_result.cuts_added;
    result.max_cut_violation = solve_result.max_cut_violation;
    result.branch_benders_enabled = solve_result.branch_benders_enabled;
    result.branch_benders_callback_calls = solve_result.branch_benders_callback_calls;
    result.branch_benders_candidate_callback_calls =
        solve_result.branch_benders_candidate_callback_calls;
    result.branch_benders_incumbent_callback_calls =
        solve_result.branch_benders_incumbent_callback_calls;
    result.branch_benders_candidate_incumbents_checked =
        solve_result.branch_benders_candidate_incumbents_checked;
    result.branch_benders_subproblems_attempted =
        solve_result.branch_benders_subproblems_attempted;
    result.branch_benders_subproblems_solved = solve_result.branch_benders_subproblems_solved;
    result.branch_benders_lazy_cuts_added = solve_result.branch_benders_lazy_cuts_added;
    result.branch_benders_max_cut_violation = solve_result.branch_benders_max_cut_violation;
    result.branch_benders_largest_incumbent_cut_violation =
        solve_result.branch_benders_largest_incumbent_cut_violation;
    result.branch_benders_callback_time_sec = solve_result.branch_benders_callback_time_sec;
    result.branch_benders_subproblem_time_sec = solve_result.branch_benders_subproblem_time_sec;
    result.branch_benders_average_subproblem_time_sec =
        solve_result.branch_benders_average_subproblem_time_sec;
    result.branch_benders_max_subproblem_time_sec =
        solve_result.branch_benders_max_subproblem_time_sec;
    result.branch_benders_cut_construction_time_sec =
        solve_result.branch_benders_cut_construction_time_sec;
    result.branch_benders_lazy_cut_insertion_time_sec =
        solve_result.branch_benders_lazy_cut_insertion_time_sec;
    result.branch_benders_violated_cuts = solve_result.branch_benders_violated_cuts;
    result.branch_benders_nonviolated_cuts = solve_result.branch_benders_nonviolated_cuts;
    result.branch_benders_skipped_cuts = solve_result.branch_benders_skipped_cuts;
    result.branch_benders_duplicate_cuts = solve_result.branch_benders_duplicate_cuts;
    result.branch_benders_incumbent_log = solve_result.branch_benders_incumbent_log;
    result.branch_benders_use_root_user_cuts = solve_result.branch_benders_use_root_user_cuts;
    result.branch_benders_root_user_cut_max_rounds =
        solve_result.branch_benders_root_user_cut_max_rounds;
    result.branch_benders_root_user_cut_tolerance =
        solve_result.branch_benders_root_user_cut_tolerance;
    result.branch_benders_root_user_cut_rounds_executed =
        solve_result.branch_benders_root_user_cut_rounds_executed;
    result.branch_benders_root_user_cut_callback_calls =
        solve_result.branch_benders_root_user_cut_callback_calls;
    result.branch_benders_root_user_cuts_added =
        solve_result.branch_benders_root_user_cuts_added;
    result.branch_benders_root_user_cut_scenarios_solved =
        solve_result.branch_benders_root_user_cut_scenarios_solved;
    result.branch_benders_root_user_cut_max_violation =
        solve_result.branch_benders_root_user_cut_max_violation;
    result.branch_benders_root_user_cut_total_time_sec =
        solve_result.branch_benders_root_user_cut_total_time_sec;
    result.branch_benders_root_user_cut_subproblem_time_sec =
        solve_result.branch_benders_root_user_cut_subproblem_time_sec;
    result.branch_benders_root_user_cut_skipped_reason =
        solve_result.branch_benders_root_user_cut_skipped_reason;
    result.branch_benders_root_user_cut_only_at_root_confirmed =
        solve_result.branch_benders_root_user_cut_only_at_root_confirmed;
    result.branch_benders_root_user_cut_round_log =
        solve_result.branch_benders_root_user_cut_round_log;
    result.benders_use_lifted_lower_bounds = solve_result.benders_use_lifted_lower_bounds;
    result.benders_lifted_lower_bound_count = solve_result.benders_lifted_lower_bound_count;
    result.benders_lifted_lower_bound_precompute_time_sec =
        solve_result.benders_lifted_lower_bound_precompute_time_sec;
    result.benders_lifted_lower_bound_nonzero_coefficients =
        solve_result.benders_lifted_lower_bound_nonzero_coefficients;
    result.benders_lifted_lower_bound_min_rhs = solve_result.benders_lifted_lower_bound_min_rhs;
    result.benders_lifted_lower_bound_max_rhs = solve_result.benders_lifted_lower_bound_max_rhs;
    result.benders_lifted_lower_bound_weighted = solve_result.benders_lifted_lower_bound_weighted;
    result.benders_lifted_lower_bound_weight_map_hash =
        solve_result.benders_lifted_lower_bound_weight_map_hash;
    result.benders_lifted_lower_bound_scenarios_precomputed =
        solve_result.benders_lifted_lower_bound_scenarios_precomputed;
    result.benders_lifted_lower_bound_singletons_evaluated =
        solve_result.benders_lifted_lower_bound_singletons_evaluated;
    result.benders_lifted_lower_bound_no_firebreak_loss_min =
        solve_result.benders_lifted_lower_bound_no_firebreak_loss_min;
    result.benders_lifted_lower_bound_no_firebreak_loss_max =
        solve_result.benders_lifted_lower_bound_no_firebreak_loss_max;
    result.benders_lifted_lower_bound_singleton_benefit_min =
        solve_result.benders_lifted_lower_bound_singleton_benefit_min;
    result.benders_lifted_lower_bound_singleton_benefit_max =
        solve_result.benders_lifted_lower_bound_singleton_benefit_max;
    result.benders_lifted_lower_bound_constraints_added =
        solve_result.benders_lifted_lower_bound_constraints_added;
    result.benders_lifted_lower_bound_validity_mode =
        solve_result.benders_lifted_lower_bound_validity_mode;
    result.benders_lifted_lower_bound_notes = solve_result.benders_lifted_lower_bound_notes;
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
    result.solver_weighted_objective = solve_result.solver_weighted_objective;
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
    result.dpv_benders_iterations = solve_result.dpv_benders_iterations;
    result.dpv_benders_subproblems_solved = solve_result.dpv_benders_subproblems_solved;
    result.dpv_benders_cuts_generated = solve_result.dpv_benders_cuts_generated;
    result.dpv_benders_cuts_added = solve_result.dpv_benders_cuts_added;
    result.dpv_benders_duplicate_cuts = solve_result.dpv_benders_duplicate_cuts;
    result.dpv_benders_max_cut_violation = solve_result.dpv_benders_max_cut_violation;
    result.dpv_benders_max_tightness_error = solve_result.dpv_benders_max_tightness_error;
    result.dpv_benders_subproblem_time_sec = solve_result.dpv_benders_subproblem_time_sec;
    result.dpv_benders_cut_time_sec = solve_result.dpv_benders_cut_time_sec;
    result.dpv_llbi_enabled = solve_result.dpv_llbi_enabled;
    result.dpv_llbi_weighted = solve_result.dpv_llbi_weighted;
    result.dpv_llbi_type = solve_result.dpv_llbi_type;
    result.dpv_llbi_constraints_added = solve_result.dpv_llbi_constraints_added;
    result.dpv_llbi_precompute_time_sec = solve_result.dpv_llbi_precompute_time_sec;
    result.dpv_llbi_validity_mode = solve_result.dpv_llbi_validity_mode;
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
    result.notes.push_back("Branch-and-Benders tolerance: " + std::to_string(options.tolerance) + ".");
    result.notes.push_back(
        std::string("Lifted lower bounds enabled: ") +
        (options.use_lifted_lower_bounds ? "true." : "false."));
    result.notes.push_back(
        std::string("Root fractional user cuts enabled: ") +
        (options.use_root_user_cuts ? "true." : "false."));
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
