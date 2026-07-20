#include "experiments/FppBendersOutOfSampleRunner.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "analysis/GraphDiagnostics.hpp"
#include "benders/FppBendersSolver.hpp"
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
#include "solver/CplexEnvironment.hpp"
#include "solver/FppWeightedLossUtils.hpp"

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
    return firebreak::io::resolve_output_path("results/experiments/fpp_benders_oos_results.csv");
}

std::filesystem::path default_solution_json_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + "_solution.json");
}

std::filesystem::path default_solution_csv_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + "_solution.csv");
}

std::string format_cut_number(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::string encode_cut_coefficients(
    const std::vector<std::pair<int, double>>& coefficients,
    double threshold) {
    std::ostringstream out;
    bool first = true;
    for (const auto& [compact_index, coefficient] : coefficients) {
        if (std::fabs(coefficient) <= threshold) {
            continue;
        }
        if (!first) {
            out << ";";
        }
        out << compact_index << ":" << format_cut_number(coefficient);
        first = false;
    }
    return out.str();
}

int count_nonzero_cut_coefficients(
    const std::vector<std::pair<int, double>>& coefficients,
    double threshold) {
    int count = 0;
    for (const auto& [compact_index, coefficient] : coefficients) {
        (void)compact_index;
        if (std::fabs(coefficient) > threshold) {
            ++count;
        }
    }
    return count;
}

std::string format_compact_double(double value) {
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

risk::RiskMeasureConfig effective_risk_config_from(const risk::RiskMeasureConfig& config) {
    risk::RiskMeasureConfig effective = config;
    if (effective.type == risk::RiskMeasureType::CVaR) {
        effective.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective);
    return effective;
}

std::string objective_metric_for_risk(const risk::RiskMeasureConfig& config) {
    std::string label = solver::weighted_objective_metric_label(config);
    if (config.type == risk::RiskMeasureType::CVaR) {
        label += "_beta_" + format_compact_double(config.cvarBeta);
    } else if (config.type == risk::RiskMeasureType::MeanCVaR) {
        label += "_beta_" + format_compact_double(config.cvarBeta) +
            "_lambda_" + format_compact_double(config.cvarLambda);
    }
    return label;
}

std::string method_label_for_risk(const risk::RiskMeasureConfig& config) {
    if (config.type == risk::RiskMeasureType::CVaR) {
        return "FPP-Benders-CVaR";
    }
    if (config.type == risk::RiskMeasureType::MeanCVaR) {
        return "FPP-Benders-MeanCVaR";
    }
    return "FPP-Benders";
}

void write_benders_cut_export_csv(
    const std::filesystem::path& output_path,
    const std::vector<solver::BendersAddedCutRecord>& cuts,
    const solver::ModelResult& solve_result) {
    constexpr double kCoefficientThreshold = 1.0e-12;
    firebreak::io::ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open Benders cut export CSV file: " + output_path.string());
    }

    out << "iteration,scenario_id,objective_metric,weight_profile,weight_map_hash,"
        << "subproblem_value,scenario_weighted_recourse_value,cut_value_at_incumbent,"
        << "violation,cut_violation,rhs_constant,num_nonzero_coeffs,coefficients\n";
    for (const auto& cut : cuts) {
        out << cut.iteration << ","
            << cut.scenario_id << ","
            << solve_result.objective_metric << ","
            << solve_result.weight_profile << ","
            << solve_result.weight_map_hash << ","
            << format_cut_number(cut.subproblem_value) << ","
            << format_cut_number(cut.subproblem_value) << ","
            << format_cut_number(cut.subproblem_value) << ","
            << format_cut_number(cut.violation) << ","
            << format_cut_number(cut.violation) << ","
            << format_cut_number(cut.rhs_constant) << ","
            << count_nonzero_cut_coefficients(cut.coefficients_by_compact_index, kCoefficientThreshold) << ","
            << encode_cut_coefficients(cut.coefficients_by_compact_index, kCoefficientThreshold)
            << "\n";
    }
}

void write_lifted_lower_bound_export_csv(
    const std::filesystem::path& output_path,
    const std::vector<solver::BendersLiftedLowerBoundRecord>& inequalities) {
    constexpr double kCoefficientThreshold = 1.0e-12;
    firebreak::io::ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error(
            "Could not open lifted lower-bound export CSV file: " + output_path.string());
    }

    out << "scenario_id,f_empty,rhs_constant,num_nonzero_coeffs,coefficients\n";
    for (const auto& inequality : inequalities) {
        out << inequality.scenario_id << ","
            << format_cut_number(inequality.f_empty) << ","
            << format_cut_number(inequality.rhs_constant) << ","
            << count_nonzero_cut_coefficients(
                inequality.coefficients_by_compact_index,
                kCoefficientThreshold) << ","
            << encode_cut_coefficients(
                inequality.coefficients_by_compact_index,
                kCoefficientThreshold)
            << "\n";
    }
}

void print_summary(
    const io::StandardExperimentResult& result,
    const std::filesystem::path& solution_json_path,
    const std::filesystem::path& solution_csv_path) {
    std::cout << "Run ID: " << result.run_id << "\n";
    std::cout << "Landscape: " << result.landscape << "\n";
    std::cout << "Method: " << result.method << "\n";
    std::cout << "Objective metric: " << result.objective_metric << "\n";
    std::cout << "Risk measure: " << result.risk_measure << "\n";
    std::cout << "Graph type note: " << result.graph_type_note << "\n";
    std::cout << "Train graph type ratios: " << result.train_graph_classification_ratios << "\n";
    std::cout << "Test graph type ratios: " << result.test_graph_classification_ratios << "\n";
    std::cout << "Train scenarios: " << result.train_scenario_count << "\n";
    std::cout << "Test scenarios: " << result.test_scenario_count << "\n";
    std::cout << "Solver status: " << result.solver_status << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Objective in-sample: " << result.objective_in_sample << "\n";
    std::cout << "Weight profile: " << result.weight_profile << "\n";
    std::cout << "Weight map hash: " << result.weight_map_hash << "\n";
    std::cout << "Evaluator weighted objective: " << result.evaluator_weighted_objective << "\n";
    std::cout << "Objective validation diff: "
              << result.objective_validation_abs_difference << "\n";
    std::cout << "Best bound: " << result.best_bound << "\n";
    std::cout << "MIP gap: " << result.mip_gap << "\n";
    std::cout << "Iterations: " << result.solver_iterations << "\n";
    std::cout << "Cuts added: " << result.cuts_added << "\n";
    std::cout << "Max cut violation: " << result.max_cut_violation << "\n";
    if (!result.benders_termination_reason.empty()) {
        std::cout << "Benders termination reason: " << result.benders_termination_reason << "\n";
        std::cout << "Largest intermediate Benders violation: "
                  << result.benders_largest_intermediate_cut_violation << "\n";
    }
    std::cout << "Runtime seconds: " << result.runtime_seconds << "\n";
    std::cout << "Selected firebreaks:";
    for (const int node : result.selected_firebreaks) {
        std::cout << " " << node;
    }
    std::cout << "\n";
    std::cout << "Train expected burned area: " << result.train_expected_burned_area << "\n";
    std::cout << "Train worst 10% burned area: " << result.train_worst_10pct_burned_area << "\n";
    std::cout << "Test expected burned area: " << result.test_expected_burned_area << "\n";
    std::cout << "Train expected weighted burn loss: "
              << result.train_expected_weighted_burn_loss << "\n";
    std::cout << "Test expected weighted burn loss: "
              << result.test_expected_weighted_burn_loss << "\n";
    std::cout << "Test worst 10% burned area: " << result.test_worst_10pct_burned_area << "\n";
    std::cout << "Solution JSON: " << firebreak::io::path_to_string(solution_json_path) << "\n";
    std::cout << "Solution CSV: " << firebreak::io::path_to_string(solution_csv_path) << "\n";
}

}  // namespace

int FppBendersOutOfSampleRunner::run(const FppBendersOutOfSampleOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
    }
    if (options.run_id.empty()) {
        throw std::runtime_error("--run-id is required.");
    }
    if (options.max_iterations <= 0) {
        throw std::runtime_error("--max-iterations must be positive.");
    }
    if (options.tolerance < 0.0) {
        throw std::runtime_error("--tolerance must be nonnegative.");
    }
    if (!options.lifted_lower_bound_export_path.empty() && !options.use_lifted_lower_bounds) {
        throw std::runtime_error("--export-lifted-lower-bounds requires --use-lifted-lower-bounds.");
    }
    const auto effective_risk_config = effective_risk_config_from(options.risk_config);
    const std::string method_label = method_label_for_risk(effective_risk_config);
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
    const auto benders_cut_export_path = options.benders_cut_export_path.empty()
        ? std::filesystem::path()
        : firebreak::io::resolve_output_path(options.benders_cut_export_path.string());
    const auto lifted_lower_bound_export_path = options.lifted_lower_bound_export_path.empty()
        ? std::filesystem::path()
        : firebreak::io::resolve_output_path(options.lifted_lower_bound_export_path.string());

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
    auto opt_instance = builder.build(train_instance, options.alpha, false);
    const auto resolved_weight_map_path = options.weight_map_file.empty()
        ? std::filesystem::path()
        : firebreak::io::resolve_input_path(options.weight_map_file.string());
    firebreak::solver::attach_weight_map_to_optimization_instance(
        opt_instance,
        resolved_weight_map_path);

    benders::FppBendersOptions solver_options;
    solver_options.max_iterations = options.max_iterations;
    solver_options.tolerance = options.tolerance;
    solver_options.time_limit_seconds = options.time_limit_seconds;
    solver_options.mip_gap = options.mip_gap;
    solver_options.threads = options.threads;
    solver_options.verbose = options.verbose;
    solver_options.risk_config = effective_risk_config;
    solver_options.use_lifted_lower_bounds = options.use_lifted_lower_bounds;

    benders::FppBendersSolver solver;
    auto solve_result = solver.solve(opt_instance, solver_options);
    solve_result.method = method_label;
    firebreak::solver::attach_direct_fpp_weight_metadata(
        solve_result,
        opt_instance,
        resolved_weight_map_path);
    if (!benders_cut_export_path.empty()) {
        write_benders_cut_export_csv(
            benders_cut_export_path,
            solve_result.benders_added_cuts,
            solve_result);
    }
    if (!lifted_lower_bound_export_path.empty()) {
        write_lifted_lower_bound_export_csv(
            lifted_lower_bound_export_path,
            solve_result.benders_lifted_lower_bounds);
    }

    eval::FppRecourseEvaluator recourse_evaluator(opt_instance);
    const auto recourse_validation =
        recourse_evaluator.evaluate(
            solve_result.selected_firebreak_indices,
            false,
            effective_risk_config.cvarBeta);
    const double evaluator_weighted_objective =
        firebreak::solver::weighted_objective_from_recourse(
            recourse_validation,
            effective_risk_config);
    firebreak::solver::attach_direct_fpp_validation(solve_result, evaluator_weighted_objective);
    if (solve_result.weight_map_hash != recourse_validation.weight_map_hash) {
        throw std::runtime_error(
            "Optimization and train evaluation weight map hashes differ in run-fpp-benders-oos.");
    }
    const double evaluator_abs_diff = solve_result.objective_validation_abs_difference;
    const double evaluator_rel_diff = solve_result.objective_validation_rel_difference;
    const std::string validation_status = solve_result.validation_status;

    io::FirebreakSolutionRecord solution_record;
    solution_record.method = method_label;
    solution_record.landscape = options.landscape;
    solution_record.alpha = options.alpha;
    solution_record.budget = opt_instance.budget;
    solution_record.selected_firebreak_original_nodes = solve_result.selected_firebreak_original_nodes;
    solution_record.selected_firebreak_indices = solve_result.selected_firebreak_indices;
    solution_record.objective_metric = objective_metric_for_risk(effective_risk_config);
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
    auto test_opt_instance = builder.build(test_instance, options.alpha, false);
    firebreak::solver::attach_weight_map_to_optimization_instance(
        test_opt_instance,
        resolved_weight_map_path);
    std::vector<int> selected_test_compact_indices;
    selected_test_compact_indices.reserve(solve_result.selected_firebreak_original_nodes.size());
    for (const int original_node : solve_result.selected_firebreak_original_nodes) {
        selected_test_compact_indices.push_back(test_opt_instance.node_mapper.to_index(original_node));
    }
    eval::FppRecourseEvaluator test_recourse_evaluator(test_opt_instance);
    const auto test_weighted_eval = test_recourse_evaluator.evaluate(
        selected_test_compact_indices,
        false,
        effective_risk_config.cvarBeta);
    if (solve_result.weight_map_hash != test_weighted_eval.weight_map_hash) {
        throw std::runtime_error(
            "Optimization and test evaluation weight map hashes differ in run-fpp-benders-oos.");
    }

    io::StandardExperimentResult result;
    result.run_id = options.run_id;
    result.timestamp = io::current_timestamp_utc();
    result.landscape = options.landscape;
    result.method = method_label;
    result.objective_metric = objective_metric_for_risk(effective_risk_config);
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
    result.solver_iterations = solve_result.iterations;
    result.cuts_added = solve_result.cuts_added;
    result.max_cut_violation = solve_result.max_cut_violation;
    result.benders_status = solve_result.benders_status;
    result.benders_iterations = solve_result.benders_iterations;
    result.benders_cuts_added = solve_result.benders_cuts_added;
    result.benders_final_max_cut_violation = solve_result.benders_final_max_cut_violation;
    result.benders_largest_intermediate_cut_violation =
        solve_result.benders_largest_intermediate_cut_violation;
    result.benders_termination_reason = solve_result.benders_termination_reason;
    result.benders_master_solve_time_sec = solve_result.benders_master_solve_time_sec;
    result.benders_subproblem_time_sec = solve_result.benders_subproblem_time_sec;
    result.benders_subproblems_solved = solve_result.benders_subproblems_solved;
    result.benders_average_subproblem_time_sec =
        solve_result.benders_average_subproblem_time_sec;
    result.benders_max_subproblem_time_sec = solve_result.benders_max_subproblem_time_sec;
    result.benders_use_lifted_lower_bounds = solve_result.benders_use_lifted_lower_bounds;
    result.benders_lifted_lower_bound_count = solve_result.benders_lifted_lower_bound_count;
    result.benders_lifted_lower_bound_precompute_time_sec =
        solve_result.benders_lifted_lower_bound_precompute_time_sec;
    result.benders_lifted_lower_bound_nonzero_coefficients =
        solve_result.benders_lifted_lower_bound_nonzero_coefficients;
    result.benders_lifted_lower_bound_min_rhs = solve_result.benders_lifted_lower_bound_min_rhs;
    result.benders_lifted_lower_bound_max_rhs = solve_result.benders_lifted_lower_bound_max_rhs;
    result.benders_lifted_lower_bound_weighted =
        solve_result.benders_lifted_lower_bound_weighted;
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
    result.benders_lifted_lower_bound_cache_hit =
        solve_result.benders_lifted_lower_bound_cache_hit;
    result.benders_lifted_lower_bound_validity_mode =
        solve_result.benders_lifted_lower_bound_validity_mode;
    result.benders_lifted_lower_bound_notes = solve_result.benders_lifted_lower_bound_notes;
    result.coverage_llbi_enabled = solve_result.coverage_llbi_enabled;
    result.coverage_llbi_num_zeta_vars = solve_result.coverage_llbi_num_zeta_vars;
    result.coverage_llbi_num_constraints = solve_result.coverage_llbi_num_constraints;
    result.coverage_llbi_precompute_time_sec = solve_result.coverage_llbi_precompute_time_sec;
    result.coverage_llbi_weighted = solve_result.coverage_llbi_weighted;
    result.coverage_llbi_weight_map_hash = solve_result.coverage_llbi_weight_map_hash;
    result.coverage_llbi_scenarios_precomputed =
        solve_result.coverage_llbi_scenarios_precomputed;
    result.coverage_llbi_baseline_cells = solve_result.coverage_llbi_baseline_cells;
    result.coverage_llbi_auxiliary_variables =
        solve_result.coverage_llbi_auxiliary_variables;
    result.coverage_llbi_linking_constraints =
        solve_result.coverage_llbi_linking_constraints;
    result.coverage_llbi_loss_constraints = solve_result.coverage_llbi_loss_constraints;
    result.coverage_llbi_nonempty_coverage_sets =
        solve_result.coverage_llbi_nonempty_coverage_sets;
    result.coverage_llbi_total_incidence_terms =
        solve_result.coverage_llbi_total_incidence_terms;
    result.coverage_llbi_build_time_sec = solve_result.coverage_llbi_build_time_sec;
    result.coverage_llbi_validity_mode = solve_result.coverage_llbi_validity_mode;
    result.path_llbi_enabled = solve_result.path_llbi_enabled;
    result.path_llbi_num_b_vars = solve_result.path_llbi_num_b_vars;
    result.path_llbi_num_path_constraints = solve_result.path_llbi_num_path_constraints;
    result.path_llbi_num_paths_used = solve_result.path_llbi_num_paths_used;
    result.path_llbi_weighted = solve_result.path_llbi_weighted;
    result.path_llbi_weight_map_hash = solve_result.path_llbi_weight_map_hash;
    result.path_llbi_scenarios_precomputed = solve_result.path_llbi_scenarios_precomputed;
    result.path_llbi_baseline_nodes = solve_result.path_llbi_baseline_nodes;
    result.path_llbi_auxiliary_variables = solve_result.path_llbi_auxiliary_variables;
    result.path_llbi_path_constraints = solve_result.path_llbi_path_constraints;
    result.path_llbi_loss_constraints = solve_result.path_llbi_loss_constraints;
    result.path_llbi_total_paths = solve_result.path_llbi_total_paths;
    result.path_llbi_total_candidate_incidence_terms =
        solve_result.path_llbi_total_candidate_incidence_terms;
    result.path_llbi_nodes_without_paths = solve_result.path_llbi_nodes_without_paths;
    result.path_llbi_path_enumeration_complete =
        solve_result.path_llbi_path_enumeration_complete;
    result.path_llbi_paths_truncated = solve_result.path_llbi_paths_truncated;
    result.path_llbi_precompute_time_sec = solve_result.path_llbi_precompute_time_sec;
    result.path_llbi_build_time_sec = solve_result.path_llbi_build_time_sec;
    result.path_llbi_validity_mode = solve_result.path_llbi_validity_mode;
    result.benders_iteration_log = solve_result.benders_iteration_log;
    result.fpp_mode = "fpp_benders_explicit_loop";
    result.formulation = "benders";
    result.compact_node_count = opt_instance.node_mapper.size();
    result.eligible_node_count = static_cast<int>(opt_instance.eligible_indices.size());
    for (const auto& scenario : opt_instance.scenarios) {
        result.total_observed_scenario_nodes += static_cast<int>(scenario.observed_node_indices.size());
    }
    result.total_scenario_arcs = static_cast<int>(opt_instance.total_arcs);
    result.evaluator_objective = solve_result.evaluator_objective;
    result.evaluator_abs_diff = evaluator_abs_diff;
    result.evaluator_rel_diff = evaluator_rel_diff;
    result.validation_status = validation_status;
    result.weight_profile = solve_result.weight_profile;
    result.weight_map_file = solve_result.weight_map_file;
    result.weight_map_hash = solve_result.weight_map_hash;
    result.weight_normalized = solve_result.weight_normalized;
    result.weight_mean = solve_result.weight_mean;
    result.weight_min = solve_result.weight_min;
    result.weight_max = solve_result.weight_max;
    result.weight_total = solve_result.weight_total;
    result.solver_weighted_objective = solve_result.solver_weighted_objective;
    result.evaluator_weighted_objective = solve_result.evaluator_weighted_objective;
    result.objective_validation_abs_difference =
        solve_result.objective_validation_abs_difference;
    result.objective_validation_rel_difference =
        solve_result.objective_validation_rel_difference;
    result.objective_validation_passed = solve_result.objective_validation_passed;
    result.selected_firebreaks = solve_result.selected_firebreak_original_nodes;
    result.train_expected_burned_area = train_eval.expected_burned_area;
    result.train_worst_10pct_burned_area = train_eval.worst_10pct_burned_area;
    result.test_expected_burned_area = test_eval.expected_burned_area;
    result.test_worst_10pct_burned_area = test_eval.worst_10pct_burned_area;
    result.train_empirical_var_burned_area = train_eval.empirical_var_90pct_burned_area;
    result.train_empirical_cvar_burned_area = train_eval.empirical_cvar_90pct_burned_area;
    result.test_empirical_var_burned_area = test_eval.empirical_var_90pct_burned_area;
    result.test_empirical_cvar_burned_area = test_eval.empirical_cvar_90pct_burned_area;
    result.train_expected_weighted_burn_loss =
        recourse_validation.expected_weighted_burn_loss;
    result.test_expected_weighted_burn_loss =
        test_weighted_eval.expected_weighted_burn_loss;
    result.train_weighted_var = recourse_validation.weighted_loss_statistics.var;
    result.test_weighted_var = test_weighted_eval.weighted_loss_statistics.var;
    result.train_weighted_cvar = recourse_validation.weighted_loss_statistics.cvar;
    result.test_weighted_cvar = test_weighted_eval.weighted_loss_statistics.cvar;
    result.train_percentage_landscape_value_burned =
        recourse_validation.expected_percentage_landscape_value_burned;
    result.test_percentage_landscape_value_burned =
        test_weighted_eval.expected_percentage_landscape_value_burned;
    result.train_percentage_high_value_weight_burned =
        recourse_validation.expected_percentage_high_value_weight_burned;
    result.test_percentage_high_value_weight_burned =
        test_weighted_eval.expected_percentage_high_value_weight_burned;
    result.risk_measure = solve_result.risk_measure;
    result.cvar_beta = solve_result.cvar_beta;
    result.cvar_lambda = solve_result.cvar_lambda;
    result.risk_threshold_value = solve_result.risk_threshold_value;
    result.expected_loss_component = solve_result.expected_loss_component;
    result.cvar_loss_component = solve_result.cvar_loss_component;
    result.train_evaluation_runtime_seconds = train_eval.total_runtime_seconds;
    result.test_evaluation_runtime_seconds = test_eval.total_runtime_seconds;
    result.test_scenario_loading_runtime_seconds = test_loading_seconds;
    result.train_graph_classification_ratios =
        analysis::graph_classification_ratio_summary(train_instance.scenarios);
    result.test_graph_classification_ratios =
        analysis::graph_classification_ratio_summary(test_instance.scenarios);
    result.notes = solve_result.notes;
    result.notes.push_back(
        validation_status == "pass"
            ? "Weighted FppRecourseEvaluator objective validation passed for final selected firebreaks."
            : "Weighted FppRecourseEvaluator objective validation warning for final selected firebreaks.");
    result.notes.insert(result.notes.end(), notes.begin(), notes.end());
    result.notes.push_back("Benders max iterations: " + std::to_string(options.max_iterations) + ".");
    result.notes.push_back("Benders tolerance: " + std::to_string(options.tolerance) + ".");
    result.notes.push_back(
        std::string("FPP Benders lifted lower bounds enabled: ") +
        (options.use_lifted_lower_bounds ? "true." : "false."));
    if (!benders_cut_export_path.empty()) {
        result.notes.push_back(
            "Added Benders cuts were exported to " +
            firebreak::io::path_to_string(benders_cut_export_path) + ".");
    }
    if (!lifted_lower_bound_export_path.empty()) {
        result.notes.push_back(
            "FPP lifted lower-bound inequalities were exported to " +
            firebreak::io::path_to_string(lifted_lower_bound_export_path) + ".");
    }
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
    if (!benders_cut_export_path.empty()) {
        std::cout << "Wrote Benders cut export CSV: "
                  << firebreak::io::path_to_string(benders_cut_export_path) << "\n";
    }
    if (!lifted_lower_bound_export_path.empty()) {
        std::cout << "Wrote lifted lower-bound export CSV: "
                  << firebreak::io::path_to_string(lifted_lower_bound_export_path) << "\n";
    }
    return 0;
}

}  // namespace firebreak::experiments
