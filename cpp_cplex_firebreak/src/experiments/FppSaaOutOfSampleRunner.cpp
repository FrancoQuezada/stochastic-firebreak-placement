#include "experiments/FppSaaOutOfSampleRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "analysis/GraphDiagnostics.hpp"
#include "core/FirebreakSolution.hpp"
#include "eval/BurnedAreaEvaluator.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "experiments/BatchExperimentConfig.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/ExperimentResultWriter.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "io/ScenarioSplitUtils.hpp"
#include "io/SolutionIO.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/CplexEnvironment.hpp"
#include "solver/FppCutReachabilityCplexModel.hpp"
#include "solver/FppSaaCplexModel.hpp"
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

std::string format_compact_double(double value) {
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

std::string objective_metric_for_risk(const risk::RiskMeasureConfig& config) {
    if (config.type == risk::RiskMeasureType::Expected) {
        return "expected_burned_area";
    }
    if (config.type == risk::RiskMeasureType::CVaR) {
        return "cvar_burned_area_beta_" + format_compact_double(config.cvarBeta);
    }
    return "mean_cvar_burned_area_beta_" +
        format_compact_double(config.cvarBeta) +
        "_lambda_" +
        format_compact_double(config.cvarLambda);
}

std::string append_strengthening_label_suffix(
    std::string label,
    const benders::FppStrengtheningOptions& strengthening_options) {
    if (strengthening_options.use_global_dominance_preprocessing) {
        label += "-DominancePreprocess";
    }
    if (strengthening_options.use_coverage_llbi) {
        label += "-CoverageLLBI";
    }
    if (strengthening_options.use_path_llbi) {
        label += "-PathLLBI";
    }
    if (strengthening_options.use_conditional_zero_benefit_fixing) {
        label += "-ConditionalZeroFixing";
    }
    return label;
}

std::string method_label_for_risk(
    const risk::RiskMeasureConfig& config,
    const benders::FppStrengtheningOptions& strengthening_options) {
    std::string label = "FPP-SAA";
    if (config.type == risk::RiskMeasureType::CVaR) {
        label += "-CVaR";
    } else if (config.type == risk::RiskMeasureType::MeanCVaR) {
        label += "-MeanCVaR";
    }
    return append_strengthening_label_suffix(std::move(label), strengthening_options);
}

std::vector<std::pair<std::string, std::string>> strengthening_summary_fields(
    const io::StandardExperimentResult& result) {
    return {
        {"coverage_llbi_enabled", result.coverage_llbi_enabled ? "true" : "false"},
        {"coverage_llbi_num_zeta_vars", std::to_string(result.coverage_llbi_num_zeta_vars)},
        {"coverage_llbi_num_constraints", std::to_string(result.coverage_llbi_num_constraints)},
        {"coverage_llbi_precompute_time_sec", format_compact_double(result.coverage_llbi_precompute_time_sec)},
        {"path_llbi_enabled", result.path_llbi_enabled ? "true" : "false"},
        {"path_llbi_num_b_vars", std::to_string(result.path_llbi_num_b_vars)},
        {"path_llbi_num_path_constraints", std::to_string(result.path_llbi_num_path_constraints)},
        {"path_llbi_num_paths_used", std::to_string(result.path_llbi_num_paths_used)},
        {"path_llbi_precompute_time_sec", format_compact_double(result.path_llbi_precompute_time_sec)},
        {"global_dominance_enabled", result.global_dominance_enabled ? "true" : "false"},
        {"global_dominance_candidates_removed", std::to_string(result.global_dominance_candidates_removed)},
        {"global_dominance_equivalence_classes", std::to_string(result.global_dominance_equivalence_classes)},
        {"global_dominance_precompute_time_sec", format_compact_double(result.global_dominance_precompute_time_sec)},
        {"conditional_zero_benefit_enabled", result.conditional_zero_benefit_enabled ? "true" : "false"},
        {"conditional_zero_benefit_fixings_attempted", std::to_string(result.conditional_zero_benefit_fixings_attempted)},
        {"conditional_zero_benefit_fixings_applied", std::to_string(result.conditional_zero_benefit_fixings_applied)},
        {"conditional_zero_benefit_time_sec", format_compact_double(result.conditional_zero_benefit_time_sec)},
    };
}

void export_strengthening_summaries(
    const benders::FppStrengtheningOptions& options,
    const io::StandardExperimentResult& result) {
    const auto fields = strengthening_summary_fields(result);
    benders::export_fpp_strengthening_summary(options.coverage_llbi_export_path, fields);
    benders::export_fpp_strengthening_summary(options.path_llbi_export_path, fields);
    benders::export_fpp_strengthening_summary(options.dominance_preprocessing_export_path, fields);
    benders::export_fpp_strengthening_summary(options.conditional_fixing_log_export_path, fields);
}

std::filesystem::path default_experiment_json_path(const std::string& run_id) {
    return firebreak::io::resolve_output_path("results/experiments/" + run_id + ".json");
}

std::filesystem::path default_experiment_csv_path() {
    return firebreak::io::resolve_output_path("results/experiments/fpp_saa_oos_results.csv");
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
    std::cout << "Risk measure: " << result.risk_measure << "\n";
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

int FppSaaOutOfSampleRunner::run(const FppSaaOutOfSampleOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
    }
    if (options.run_id.empty()) {
        throw std::runtime_error("--run-id is required.");
    }
    risk::RiskMeasureConfig effective_risk_config = options.risk_config;
    if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
        effective_risk_config.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective_risk_config);
    const std::string method_label =
        method_label_for_risk(effective_risk_config, options.strengthening_options);
    const std::string fpp_formulation = normalize_fpp_formulation(options.fpp_formulation);
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
    auto opt_instance = builder.build(train_instance, options.alpha, false);
    const auto dominance_preprocess = benders::apply_fpp_global_dominance_preprocessing(
        opt_instance,
        options.strengthening_options.use_global_dominance_preprocessing);
    if (options.strengthening_options.use_global_dominance_preprocessing) {
        opt_instance = dominance_preprocess.reduced_instance;
    }

    solver::WarmStart warm_start;
    const solver::WarmStart* warm_start_ptr = nullptr;
    if (!options.warm_start_solution_path.empty()) {
        const auto warm_start_path = firebreak::io::resolve_input_path(options.warm_start_solution_path.string());
        warm_start = solver::load_warm_start_from_csv(warm_start_path, opt_instance, opt_instance.budget);
        warm_start_ptr = &warm_start;
    }

    solver::ModelResult solve_result;
    if (fpp_formulation == "base") {
        solver::FppSaaCplexModel model;
        solve_result = model.solve(
            opt_instance,
            options.time_limit_seconds,
            options.mip_gap,
            options.threads,
            options.verbose,
            warm_start_ptr,
            nullptr,
            nullptr,
            effective_risk_config,
            &options.strengthening_options);
    } else if (fpp_formulation == "cut") {
        solver::FppCutReachabilityCplexModel model;
        solve_result = model.solve(
            opt_instance,
            options.time_limit_seconds,
            options.mip_gap,
            options.threads,
            options.verbose,
            warm_start_ptr,
            nullptr,
            nullptr,
            effective_risk_config);
        solve_result.coverage_llbi_enabled = options.strengthening_options.use_coverage_llbi;
        solve_result.path_llbi_enabled = options.strengthening_options.use_path_llbi;
        solve_result.global_dominance_enabled =
            options.strengthening_options.use_global_dominance_preprocessing;
        solve_result.conditional_zero_benefit_enabled =
            options.strengthening_options.use_conditional_zero_benefit_fixing;
        if (options.strengthening_options.use_coverage_llbi ||
            options.strengthening_options.use_path_llbi) {
            solve_result.notes.push_back(
                "Coverage/path LLBI are redundant in direct FPP-SAA cut/reachability because the model already contains scenario burn/reachability variables and propagation constraints; no auxiliary eta strengthening variables were added.");
        }
    } else {
        throw std::runtime_error("Unsupported FPP-SAA formulation: " + fpp_formulation);
    }
    if (options.strengthening_options.use_global_dominance_preprocessing) {
        solve_result.global_dominance_enabled = true;
        solve_result.global_dominance_candidates_removed =
            dominance_preprocess.candidates_removed;
        solve_result.global_dominance_equivalence_classes =
            dominance_preprocess.equivalence_classes;
        solve_result.global_dominance_precompute_time_sec =
            dominance_preprocess.precompute_time_sec;
        solve_result.notes.insert(
            solve_result.notes.end(),
            dominance_preprocess.notes.begin(),
            dominance_preprocess.notes.end());
    }

    eval::FppRecourseEvaluator recourse_evaluator(opt_instance);
    const auto recourse_validation =
        recourse_evaluator.evaluate(solve_result.selected_firebreak_indices, false);
    const double validation_reference = std::isfinite(solve_result.expected_loss_component)
        ? solve_result.expected_loss_component
        : solve_result.objective_value;
    const double evaluator_abs_diff =
        std::fabs(recourse_validation.expected_burned_area - validation_reference);
    const double evaluator_rel_diff =
        evaluator_abs_diff / std::max(1.0, std::fabs(validation_reference));
    const std::string validation_status =
        (evaluator_abs_diff > 1.0e-5 && evaluator_rel_diff > 1.0e-6) ? "warn" : "pass";

    io::FirebreakSolutionRecord solution_record;
    solution_record.method = method_label;
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
    result.explored_nodes = solve_result.explored_nodes;
    result.num_variables = solve_result.num_variables;
    result.num_constraints = solve_result.num_constraints;
    result.fpp_mode = fpp_formulation == "cut" ? "fpp_cut" : "fpp_base";
    result.formulation = fpp_formulation;
    result.compact_node_count = opt_instance.node_mapper.size();
    result.eligible_node_count = static_cast<int>(opt_instance.eligible_indices.size());
    for (const auto& scenario : opt_instance.scenarios) {
        result.total_observed_scenario_nodes += static_cast<int>(scenario.observed_node_indices.size());
    }
    result.total_scenario_arcs = static_cast<int>(opt_instance.total_arcs);
    result.evaluator_objective = recourse_validation.expected_burned_area;
    result.evaluator_abs_diff = evaluator_abs_diff;
    result.evaluator_rel_diff = evaluator_rel_diff;
    result.validation_status = validation_status;
    result.selected_firebreaks = solve_result.selected_firebreak_original_nodes;
    result.warm_start_used = solve_result.warm_start_used;
    result.mip_start_accepted = solve_result.mip_start_accepted;
    result.warm_start_source = solve_result.warm_start_source;
    result.warm_start_valid_nodes = solve_result.warm_start_valid_nodes;
    result.warm_start_ignored_nodes = solve_result.warm_start_ignored_nodes;
    result.warm_start_notes = solve_result.warm_start_notes;
    result.train_expected_burned_area = train_eval.expected_burned_area;
    result.train_worst_10pct_burned_area = train_eval.worst_10pct_burned_area;
    result.test_expected_burned_area = test_eval.expected_burned_area;
    result.test_worst_10pct_burned_area = test_eval.worst_10pct_burned_area;
    result.train_empirical_var_burned_area = train_eval.empirical_var_90pct_burned_area;
    result.train_empirical_cvar_burned_area = train_eval.empirical_cvar_90pct_burned_area;
    result.test_empirical_var_burned_area = test_eval.empirical_var_90pct_burned_area;
    result.test_empirical_cvar_burned_area = test_eval.empirical_cvar_90pct_burned_area;
    result.risk_measure = solve_result.risk_measure;
    result.cvar_beta = solve_result.cvar_beta;
    result.cvar_lambda = solve_result.cvar_lambda;
    result.risk_threshold_value = solve_result.risk_threshold_value;
    result.expected_loss_component = solve_result.expected_loss_component;
    result.cvar_loss_component = solve_result.cvar_loss_component;
    result.coverage_llbi_enabled = solve_result.coverage_llbi_enabled;
    result.coverage_llbi_num_zeta_vars = solve_result.coverage_llbi_num_zeta_vars;
    result.coverage_llbi_num_constraints = solve_result.coverage_llbi_num_constraints;
    result.coverage_llbi_precompute_time_sec = solve_result.coverage_llbi_precompute_time_sec;
    result.path_llbi_enabled = solve_result.path_llbi_enabled;
    result.path_llbi_num_b_vars = solve_result.path_llbi_num_b_vars;
    result.path_llbi_num_path_constraints = solve_result.path_llbi_num_path_constraints;
    result.path_llbi_num_paths_used = solve_result.path_llbi_num_paths_used;
    result.path_llbi_precompute_time_sec = solve_result.path_llbi_precompute_time_sec;
    result.global_dominance_enabled = solve_result.global_dominance_enabled;
    result.global_dominance_candidates_removed =
        solve_result.global_dominance_candidates_removed;
    result.global_dominance_equivalence_classes =
        solve_result.global_dominance_equivalence_classes;
    result.global_dominance_precompute_time_sec =
        solve_result.global_dominance_precompute_time_sec;
    result.conditional_zero_benefit_enabled =
        solve_result.conditional_zero_benefit_enabled;
    result.conditional_zero_benefit_fixings_attempted =
        solve_result.conditional_zero_benefit_fixings_attempted;
    result.conditional_zero_benefit_fixings_applied =
        solve_result.conditional_zero_benefit_fixings_applied;
    result.conditional_zero_benefit_time_sec =
        solve_result.conditional_zero_benefit_time_sec;
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
            ? "FppRecourseEvaluator validation passed for final selected firebreaks."
            : "FppRecourseEvaluator validation warning for final selected firebreaks.");
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
    export_strengthening_summaries(options.strengthening_options, result);

    print_summary(result, solution_json_path, solution_csv_path);
    std::cout << "Wrote result JSON: " << firebreak::io::path_to_string(output_json_path) << "\n";
    std::cout << "Appended result CSV: " << firebreak::io::path_to_string(output_csv_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments
