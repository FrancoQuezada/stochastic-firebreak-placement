#include "benders/FppBendersSolver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "benders/FppBendersMaster.hpp"
#include "benders/FppLiftedLowerBound.hpp"
#include "benders/FppScenarioSubproblem.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/CplexEnvironment.hpp"

namespace firebreak::benders {

namespace {

void validate_options(const FppBendersOptions& options) {
    if (options.max_iterations <= 0) {
        throw std::runtime_error("FPP Benders max_iterations must be positive.");
    }
    if (options.tolerance < 0.0) {
        throw std::runtime_error("FPP Benders tolerance must be nonnegative.");
    }
    if (options.time_limit_seconds < 0.0) {
        throw std::runtime_error("FPP Benders time_limit_seconds must be nonnegative.");
    }
    if (options.mip_gap < -1.0) {
        throw std::runtime_error("FPP Benders mip_gap must be nonnegative, or omitted.");
    }
    if (options.threads < 0) {
        throw std::runtime_error("FPP Benders threads must be nonnegative.");
    }
    risk::RiskMeasureConfig effective_risk_config = options.risk_config;
    if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
        effective_risk_config.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective_risk_config);
}

void validate_instance(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("FPP Benders requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("FPP Benders requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("FPP Benders requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0 || opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("FPP Benders budget must be between zero and the eligible-node count.");
    }
}

#ifdef FIREBREAK_WITH_CPLEX
double elapsed_seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::vector<double> expand_y_to_compact_values(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& y_values_by_eligible_position) {
    std::vector<double> compact_values(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < y_values_by_eligible_position.size(); ++pos) {
        compact_values[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            static_cast<double>(y_values_by_eligible_position[pos]);
    }
    return compact_values;
}

std::vector<int> selected_compact_indices(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& y_values_by_eligible_position) {
    std::vector<int> selected;
    for (std::size_t pos = 0; pos < y_values_by_eligible_position.size(); ++pos) {
        if (y_values_by_eligible_position[pos] == 1) {
            selected.push_back(opt.eligible_indices[pos]);
        }
    }
    return selected;
}

std::vector<int> selected_original_nodes(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& y_values_by_eligible_position) {
    std::vector<int> selected;
    for (const int compact_index : selected_compact_indices(opt, y_values_by_eligible_position)) {
        selected.push_back(opt.node_mapper.to_node(compact_index));
    }
    return selected;
}

double relative_gap(double upper_bound, double lower_bound) {
    if (!std::isfinite(upper_bound) || !std::isfinite(lower_bound)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double denom = std::max(1.0, std::fabs(upper_bound));
    return std::fabs(upper_bound - lower_bound) / denom;
}

struct RiskObjectiveEvaluation {
    double objective = 0.0;
    double expected = 0.0;
    double cvar = std::numeric_limits<double>::quiet_NaN();
    double risk_threshold = std::numeric_limits<double>::quiet_NaN();
};

bool uses_cvar_risk(const risk::RiskMeasureConfig& config) {
    return config.type == risk::RiskMeasureType::CVaR ||
           config.type == risk::RiskMeasureType::MeanCVaR;
}

risk::RiskMeasureConfig effective_risk_config_from(const risk::RiskMeasureConfig& config) {
    risk::RiskMeasureConfig effective = config;
    if (effective.type == risk::RiskMeasureType::CVaR) {
        effective.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective);
    return effective;
}

RiskObjectiveEvaluation evaluate_risk_objective(
    const opt::OptimizationInstance& opt,
    const std::vector<double>& scenario_losses,
    const risk::RiskMeasureConfig& config) {
    if (scenario_losses.size() != opt.scenarios.size()) {
        throw std::runtime_error("FPP Benders risk evaluation received the wrong loss vector size.");
    }

    std::vector<risk::WeightedLoss> weighted_losses;
    weighted_losses.reserve(scenario_losses.size());
    RiskObjectiveEvaluation evaluation;
    for (std::size_t s = 0; s < scenario_losses.size(); ++s) {
        const double probability = opt.scenarios[s].probability;
        evaluation.expected += probability * scenario_losses[s];
        weighted_losses.push_back({
            opt.scenarios[s].scenario_id,
            scenario_losses[s],
            probability,
        });
    }

    if (config.type == risk::RiskMeasureType::Expected) {
        evaluation.objective = evaluation.expected;
        return evaluation;
    }

    const auto metrics = risk::compute_weighted_risk_metrics(weighted_losses, config.cvarBeta);
    evaluation.expected = metrics.expected;
    evaluation.cvar = metrics.cvar;
    evaluation.risk_threshold = metrics.var;
    if (config.type == risk::RiskMeasureType::CVaR) {
        evaluation.objective = metrics.cvar;
    } else {
        evaluation.objective =
            (1.0 - config.cvarLambda) * metrics.expected +
            config.cvarLambda * metrics.cvar;
    }
    return evaluation;
}
#endif

}  // namespace

#ifndef FIREBREAK_WITH_CPLEX

solver::ModelResult FppBendersSolver::solve(
    const opt::OptimizationInstance& opt,
    const FppBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);
    throw std::runtime_error(solver::cplex_unavailable_message());
}

#else

solver::ModelResult FppBendersSolver::solve(
    const opt::OptimizationInstance& opt,
    const FppBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);
    const auto risk_config = effective_risk_config_from(options.risk_config);
    const bool risk_enabled = uses_cvar_risk(risk_config);

    solver::ModelResult result;
    result.method = "FPP-SAA-Benders";
    result.risk_measure = risk::to_string(risk_config.type);
    result.cvar_beta = risk_config.cvarBeta;
    result.cvar_lambda = risk_config.cvarLambda;

    const auto solve_start = std::chrono::steady_clock::now();

    FppBendersMaster master;
    master.setParameters(options.time_limit_seconds, options.mip_gap, options.threads, options.verbose);
    master.initialize(opt, risk_config);
    if (options.use_lifted_lower_bounds) {
        const auto llb_result = build_fpp_lifted_lower_bounds(opt);
        for (std::size_t s = 0; s < llb_result.inequalities.size(); ++s) {
            const auto& inequality = llb_result.inequalities[s];
            master.addLiftedLowerBound(static_cast<int>(s), inequality);

            solver::BendersLiftedLowerBoundRecord record;
            record.scenario_id = inequality.scenario_id;
            record.f_empty = inequality.f_empty;
            record.rhs_constant = inequality.rhs_constant;
            record.num_nonzero_coefficients = inequality.nonzero_coefficients;
            record.coefficients_by_compact_index = inequality.coefficients_by_compact_index;
            result.benders_lifted_lower_bounds.push_back(std::move(record));
        }
        result.benders_use_lifted_lower_bounds = true;
        result.benders_lifted_lower_bound_count = master.getLiftedLowerBoundCount();
        result.benders_lifted_lower_bound_precompute_time_sec = llb_result.precompute_time_sec;
        result.benders_lifted_lower_bound_nonzero_coefficients =
            llb_result.total_nonzero_coefficients;
        result.benders_lifted_lower_bound_min_rhs = llb_result.min_rhs;
        result.benders_lifted_lower_bound_max_rhs = llb_result.max_rhs;
        result.benders_lifted_lower_bound_notes = llb_result.notes;
        result.notes.push_back(
            "Optional FPP lifted lower-bound inequalities were added to the Benders master.");
    }

    FppScenarioSubproblem subproblem;
    double incumbent_upper_bound = std::numeric_limits<double>::infinity();
    double incumbent_expected_component = std::numeric_limits<double>::quiet_NaN();
    double incumbent_cvar_component = std::numeric_limits<double>::quiet_NaN();
    double incumbent_risk_threshold = std::numeric_limits<double>::quiet_NaN();
    double last_master_bound = 0.0;
    double final_max_violation = 0.0;
    double largest_observed_violation = 0.0;
    int total_cuts_added = 0;
    int completed_iterations = 0;
    double total_master_solve_time_sec = 0.0;
    double total_subproblem_solve_time_sec = 0.0;
    double max_subproblem_solve_time_sec = 0.0;
    int total_subproblems_solved = 0;
    std::vector<int> incumbent_y;
    std::string termination_status = "IterationLimit";
    std::string termination_reason = "MAX_ITERATIONS_REACHED";
    int solver_status_code = 0;

    for (int iteration = 1; iteration <= options.max_iterations; ++iteration) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - solve_start).count();
        if (options.time_limit_seconds > 0.0 && elapsed >= options.time_limit_seconds) {
            termination_status = "TimeLimit";
            termination_reason = "TIME_LIMIT_REACHED";
            break;
        }
        const auto iteration_start = std::chrono::steady_clock::now();

        if (options.time_limit_seconds > 0.0) {
            master.setParameters(
                std::max(0.001, options.time_limit_seconds - elapsed),
                options.mip_gap,
                options.threads,
                options.verbose);
        }

        const auto master_result = master.solve();
        total_master_solve_time_sec += master_result.runtime_seconds;
        solver_status_code = master_result.solver_status_code;
        if (master_result.y_values.empty()) {
            termination_status = master_result.status.empty() ? "No feasible master solution" : master_result.status;
            termination_reason = "MASTER_INFEASIBLE";
            break;
        }

        completed_iterations = iteration;
        const auto& ybar = master_result.y_values;
        const auto& eta_values = master_result.eta_values;
        const auto compact_y = expand_y_to_compact_values(opt, ybar);
        last_master_bound = master_result.objective_value;

        double weighted_recourse_objective = 0.0;
        double iteration_max_violation = 0.0;
        double violated_cut_violation_sum = 0.0;
        double total_subproblem_objective = 0.0;
        double subproblem_time_sec = 0.0;
        double iteration_max_subproblem_time_sec = 0.0;
        int iteration_cuts_added = 0;
        int subproblems_solved = 0;
        std::vector<double> scenario_recourse_values;
        scenario_recourse_values.reserve(opt.scenarios.size());
        std::vector<solver::BendersAddedCutRecord> iteration_added_cuts;

        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            const auto sub_result = subproblem.solve(opt, static_cast<int>(s), ybar, options.verbose);
            const double q_value = sub_result.objective_value;
            scenario_recourse_values.push_back(q_value);
            total_subproblem_objective += q_value;
            weighted_recourse_objective += opt.scenarios[s].probability * q_value;
            subproblem_time_sec += sub_result.runtime_seconds;
            iteration_max_subproblem_time_sec =
                std::max(iteration_max_subproblem_time_sec, sub_result.runtime_seconds);
            ++subproblems_solved;

            const double eta_value = eta_values[s];
            const double direct_violation = q_value - eta_value;
            const double cut_violation = sub_result.benders_cut.violationAt(eta_value, compact_y);
            const double violation = std::max(direct_violation, cut_violation);
            iteration_max_violation = std::max(iteration_max_violation, violation);

            if (eta_value < q_value - options.tolerance) {
                BendersCut cut = sub_result.benders_cut;
                cut.max_cut_violation = violation;
                master.addCut(static_cast<int>(s), cut);
                violated_cut_violation_sum += violation;

                solver::BendersAddedCutRecord cut_record;
                cut_record.iteration = iteration;
                cut_record.scenario_id = cut.scenario_id;
                cut_record.subproblem_value = cut.subproblem_objective;
                cut_record.violation = violation;
                cut_record.rhs_constant = cut.rhs_constant;
                cut_record.coefficients_by_compact_index = cut.coefficients_by_compact_index;
                iteration_added_cuts.push_back(std::move(cut_record));
                ++iteration_cuts_added;
            }
        }

        const auto risk_evaluation = evaluate_risk_objective(
            opt,
            scenario_recourse_values,
            risk_config);
        const double incumbent_candidate = risk_evaluation.objective;

        if (incumbent_candidate < incumbent_upper_bound - options.tolerance || incumbent_y.empty()) {
            incumbent_upper_bound = incumbent_candidate;
            incumbent_expected_component = risk_evaluation.expected;
            incumbent_cvar_component = risk_evaluation.cvar;
            incumbent_risk_threshold = risk_evaluation.risk_threshold;
            incumbent_y = ybar;
        }

        total_cuts_added += iteration_cuts_added;
        total_subproblem_solve_time_sec += subproblem_time_sec;
        total_subproblems_solved += subproblems_solved;
        max_subproblem_solve_time_sec =
            std::max(max_subproblem_solve_time_sec, iteration_max_subproblem_time_sec);
        final_max_violation = iteration_max_violation;
        largest_observed_violation = std::max(largest_observed_violation, iteration_max_violation);
        result.benders_added_cuts.insert(
            result.benders_added_cuts.end(),
            iteration_added_cuts.begin(),
            iteration_added_cuts.end());

        solver::BendersIterationLog iteration_log;
        iteration_log.iteration = iteration;
        iteration_log.master_objective = master_result.objective_value;
        iteration_log.master_best_bound = master_result.best_bound;
        iteration_log.total_subproblem_objective = total_subproblem_objective;
        iteration_log.weighted_recourse_objective = weighted_recourse_objective;
        iteration_log.max_cut_violation = iteration_max_violation;
        iteration_log.avg_cut_violation =
            iteration_cuts_added == 0
                ? 0.0
                : violated_cut_violation_sum / static_cast<double>(iteration_cuts_added);
        iteration_log.cuts_added = iteration_cuts_added;
        iteration_log.cumulative_cuts = total_cuts_added;
        iteration_log.subproblems_attempted = subproblems_solved;
        iteration_log.subproblems_solved = subproblems_solved;
        iteration_log.master_time_sec = master_result.runtime_seconds;
        iteration_log.subproblem_time_sec = subproblem_time_sec;
        iteration_log.average_subproblem_time_sec =
            subproblems_solved > 0
                ? subproblem_time_sec / static_cast<double>(subproblems_solved)
                : 0.0;
        iteration_log.max_subproblem_time_sec = iteration_max_subproblem_time_sec;
        iteration_log.iteration_time_sec = elapsed_seconds_since(iteration_start);
        iteration_log.selected_firebreaks = selected_original_nodes(opt, ybar);
        iteration_log.selected_firebreak_count =
            static_cast<int>(iteration_log.selected_firebreaks.size());
        result.benders_iteration_log.push_back(std::move(iteration_log));

        if (options.verbose) {
            std::cout << "FPP Benders iteration " << iteration
                      << ": master_lb=" << last_master_bound
                      << " incumbent_ub=" << incumbent_candidate
                      << " expected_recourse=" << weighted_recourse_objective
                      << " max_violation=" << iteration_max_violation
                      << " cuts_added=" << iteration_cuts_added << "\n";
        }

        if (iteration_cuts_added == 0) {
            termination_status = "Optimal";
            termination_reason = "CONVERGED_NO_VIOLATED_CUTS";
            last_master_bound = master_result.objective_value;
            break;
        }
    }

    const auto solve_end = std::chrono::steady_clock::now();
    result.runtime_seconds = std::chrono::duration<double>(solve_end - solve_start).count();
    result.status = termination_status;
    result.solver_status_code = solver_status_code;
    result.objective_value = std::isfinite(incumbent_upper_bound) ? incumbent_upper_bound : 0.0;
    result.best_bound = last_master_bound;
    result.mip_gap = relative_gap(result.objective_value, result.best_bound);
    result.iterations = completed_iterations;
    result.cuts_added = total_cuts_added;
    result.max_cut_violation = final_max_violation;
    result.benders_status = termination_status;
    result.benders_termination_reason = termination_reason;
    result.benders_iterations = completed_iterations;
    result.benders_cuts_added = total_cuts_added;
    result.benders_final_max_cut_violation = final_max_violation;
    result.benders_largest_intermediate_cut_violation = largest_observed_violation;
    result.benders_master_solve_time_sec = total_master_solve_time_sec;
    result.benders_subproblem_time_sec = total_subproblem_solve_time_sec;
    result.benders_subproblems_solved = total_subproblems_solved;
    result.benders_average_subproblem_time_sec =
        total_subproblems_solved > 0
            ? total_subproblem_solve_time_sec / static_cast<double>(total_subproblems_solved)
            : 0.0;
    result.benders_max_subproblem_time_sec = max_subproblem_solve_time_sec;
    if (!options.use_lifted_lower_bounds) {
        result.benders_use_lifted_lower_bounds = false;
        result.benders_lifted_lower_bound_count = 0;
        result.benders_lifted_lower_bound_precompute_time_sec = 0.0;
        result.benders_lifted_lower_bound_nonzero_coefficients = 0;
    }
    result.expected_loss_component = incumbent_expected_component;
    if (risk_enabled) {
        result.cvar_loss_component = incumbent_cvar_component;
        result.risk_threshold_value = incumbent_risk_threshold;
    }

    if (!incumbent_y.empty()) {
        result.selected_firebreak_indices = selected_compact_indices(opt, incumbent_y);
        result.selected_firebreak_original_nodes = selected_original_nodes(opt, incumbent_y);
    }

    std::size_t subproblem_variables = 0;
    std::size_t subproblem_constraints = 0;
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const auto structure = analyze_fpp_scenario_subproblem_structure(opt, static_cast<int>(s));
        subproblem_variables += structure.total_variable_count;
        subproblem_constraints += structure.total_constraint_count;
    }
    result.num_variables = master.getNumVariables() + subproblem_variables;
    result.num_constraints = master.getNumConstraints() + subproblem_constraints;

    result.notes.push_back("Classical iterative FPP-SAA Benders decomposition; no callbacks or lazy cuts.");
    result.notes.push_back("Master solves binary y and per-scenario eta variables to optimality at each iteration.");
    result.notes.push_back("Scenario subproblems solve the FPP LP relaxation with y_copy fixed to the incumbent y.");
    result.notes.push_back("FPP recourse objective is unweighted burned area sum_i x_i for each scenario.");
    result.notes.push_back("Benders cuts use CPLEX equality-row duals directly: eta_s >= Q_s(ybar) + pi*(y-ybar).");
    if (risk_config.type == risk::RiskMeasureType::Expected) {
        result.notes.push_back("Scenario probabilities are applied in the expected-value master objective.");
    } else if (risk_config.type == risk::RiskMeasureType::CVaR) {
        result.notes.push_back("FPP-Benders master objective is pure CVaR of scenario eta variables.");
    } else {
        result.notes.push_back("FPP-Benders master objective is a mean-CVaR blend of scenario eta variables.");
    }
    result.notes.push_back("Benders termination reason: " + termination_reason + ".");
    if (options.use_lifted_lower_bounds) {
        result.notes.push_back(
            "FPP lifted lower-bound count: " +
            std::to_string(result.benders_lifted_lower_bound_count) + ".");
        result.notes.insert(
            result.notes.end(),
            result.benders_lifted_lower_bound_notes.begin(),
            result.benders_lifted_lower_bound_notes.end());
    } else {
        result.notes.push_back("FPP lifted lower-bound inequalities were disabled.");
    }
    result.notes.push_back(
        "Largest observed intermediate Benders violation: " +
        std::to_string(largest_observed_violation) + ".");
    return result;
}

#endif

}  // namespace firebreak::benders
