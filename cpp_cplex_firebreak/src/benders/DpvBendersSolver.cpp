#include "benders/DpvBendersSolver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "benders/DpvBendersMaster.hpp"
#include "benders/DpvLiftedLowerBound.hpp"
#include "benders/DpvScenarioSubproblem.hpp"
#include "opt/WeightedDpvScoring.hpp"
#include "solver/CplexEnvironment.hpp"

namespace firebreak::benders {

namespace {

void validate_options(const DpvBendersOptions& options) {
    if (options.max_iterations <= 0) {
        throw std::runtime_error("DPV Benders max_iterations must be positive.");
    }
    if (options.tolerance < 0.0) {
        throw std::runtime_error("DPV Benders tolerance must be nonnegative.");
    }
    if (options.time_limit_seconds < 0.0) {
        throw std::runtime_error("DPV Benders time_limit_seconds must be nonnegative.");
    }
    if (options.mip_gap < -1.0) {
        throw std::runtime_error("DPV Benders mip_gap must be nonnegative, or omitted.");
    }
    if (options.threads < 0) {
        throw std::runtime_error("DPV Benders threads must be nonnegative.");
    }
}

void validate_instance(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("DPV Benders requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("DPV Benders requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("DPV Benders requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0 || opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("DPV Benders budget must be between zero and the eligible-node count.");
    }
    std::size_t counted_pairs = 0;
    for (const auto& scenario : opt.scenarios) {
        counted_pairs += scenario.dpv.product_pairs.size();
    }
    if (counted_pairs != opt.total_dpv_pairs) {
        throw std::runtime_error("DPV Benders total_dpv_pairs does not match scenario product-pair counts.");
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

std::vector<int> warm_start_to_y_values(
    const opt::OptimizationInstance& opt,
    const solver::WarmStart& warm_start) {
    std::vector<int> y_values(opt.eligible_indices.size(), 0);
    std::vector<int> y_position_by_node(static_cast<std::size_t>(opt.node_mapper.size()), -1);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        y_position_by_node[static_cast<std::size_t>(opt.eligible_indices[pos])] = static_cast<int>(pos);
    }
    for (const int compact_index : warm_start.compact_indices) {
        if (compact_index >= 0 && compact_index < static_cast<int>(y_position_by_node.size())) {
            const int y_pos = y_position_by_node[static_cast<std::size_t>(compact_index)];
            if (y_pos >= 0) {
                y_values[static_cast<std::size_t>(y_pos)] = 1;
            }
        }
    }
    return y_values;
}

void attach_warm_start_metadata(solver::ModelResult& result, const solver::WarmStart& warm_start) {
    result.warm_start_source = warm_start.source_path;
    result.warm_start_valid_nodes = warm_start.original_node_ids;
    result.warm_start_ignored_nodes = warm_start.ignored_original_node_ids;
    result.warm_start_ignored_nodes.insert(
        result.warm_start_ignored_nodes.end(),
        warm_start.trimmed_original_node_ids.begin(),
        warm_start.trimmed_original_node_ids.end());
    result.warm_start_notes = warm_start.notes;
    if (!warm_start.status.empty()) {
        result.warm_start_notes.push_back("Warm start status: " + warm_start.status + ".");
    }
}

double relative_gap(double upper_bound, double lower_bound) {
    if (!std::isfinite(upper_bound) || !std::isfinite(lower_bound)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double denom = std::max(1.0, std::fabs(upper_bound));
    return std::fabs(upper_bound - lower_bound) / denom;
}

void attach_dpv_benders_metadata(
    solver::ModelResult& result,
    const opt::OptimizationInstance& opt,
    const DpvBendersOptions& options) {
    const auto weights = opt::canonical_compact_dpv_weights_or_unit(opt);
    result.dpv_model_weighted = true;
    result.dpv_model_type = "explicit_loop_dpv_benders";
    result.dpv_variant = "solution_dependent_product_pair_loss";
    result.dpv_structural_definition =
        "product pairs (source, successor, closed descendant); destination descendant weights; multiplicity preserved";
    result.dpv_ignition_policy =
        opt::weighted_dpv_ignition_policy_name(options.dpv_ignition_policy);
    result.dpv_weight_profile = opt::weighted_dpv_weight_profile(opt, weights);
    result.dpv_weight_map_hash = opt::weighted_dpv_weight_map_hash(opt, weights);
    result.dpv_scenario_aggregation = "scenario_probability_weighted_sum";
    result.dpv_normalization = "none";
    result.dpv_risk_measure = "expected";
    result.dpv_surrogate_objective = result.objective_value;
    result.dpv_surrogate_best_bound = result.best_bound;
    result.dpv_surrogate_gap = result.mip_gap;
    result.dpv_benders_iterations = result.benders_iterations;
    result.dpv_benders_subproblems_solved = result.benders_subproblems_solved;
    result.dpv_benders_cuts_generated = result.benders_cuts_added;
    result.dpv_benders_cuts_added = result.benders_cuts_added;
    result.dpv_benders_duplicate_cuts = 0;
    result.dpv_benders_max_cut_violation = result.benders_final_max_cut_violation;
    result.dpv_benders_subproblem_time_sec = result.benders_subproblem_time_sec;
    result.dpv_benders_cut_time_sec = 0.0;
    result.dpv_llbi_enabled = options.use_lifted_lower_bounds;
    result.dpv_llbi_weighted = options.use_lifted_lower_bounds;
    result.dpv_llbi_type =
        options.use_lifted_lower_bounds ? "weighted_optimistic_downstream_product_pair_llbi" : "";
    result.dpv_llbi_constraints_added = result.benders_lifted_lower_bound_count;
    result.dpv_llbi_precompute_time_sec =
        result.benders_lifted_lower_bound_precompute_time_sec;
    result.dpv_llbi_validity_mode = result.benders_lifted_lower_bound_validity_mode;
    result.objective_metric = "weighted_solution_dependent_DPV_product_pair_loss_benders";
    result.solver_weighted_objective = result.objective_value;
}
#endif

}  // namespace

#ifndef FIREBREAK_WITH_CPLEX

	solver::ModelResult DpvBendersSolver::solve(
	    const opt::OptimizationInstance& opt,
	    const DpvBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);
    throw std::runtime_error(solver::cplex_unavailable_message());
}

#else

solver::ModelResult DpvBendersSolver::solve(
    const opt::OptimizationInstance& opt,
    const DpvBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);

    solver::ModelResult result;
    result.method = "DPV-SAA-Benders";

    const auto solve_start = std::chrono::steady_clock::now();

    DpvBendersMaster master;
    master.setParameters(options.time_limit_seconds, options.mip_gap, options.threads, options.verbose);
    master.initialize(opt);

    if (options.use_lifted_lower_bounds) {
        const auto llb_result = build_dpv_lifted_lower_bounds(opt);
        for (std::size_t s = 0; s < llb_result.inequalities.size(); ++s) {
            master.addLiftedLowerBound(static_cast<int>(s), llb_result.inequalities[s]);

            solver::BendersLiftedLowerBoundRecord record;
            record.scenario_id = llb_result.inequalities[s].scenario_id;
            record.f_empty = llb_result.inequalities[s].f_empty;
            record.rhs_constant = llb_result.inequalities[s].rhs_constant;
            record.num_nonzero_coefficients = llb_result.inequalities[s].nonzero_coefficients;
            record.coefficients_by_compact_index =
                llb_result.inequalities[s].coefficients_by_compact_index;
            result.benders_lifted_lower_bounds.push_back(std::move(record));
        }
        result.benders_use_lifted_lower_bounds = true;
        result.benders_lifted_lower_bound_count = master.getLiftedLowerBoundCount();
        result.benders_lifted_lower_bound_precompute_time_sec = llb_result.precompute_time_sec;
        result.benders_lifted_lower_bound_nonzero_coefficients =
            llb_result.total_nonzero_coefficients;
        result.benders_lifted_lower_bound_min_rhs = llb_result.min_rhs;
        result.benders_lifted_lower_bound_max_rhs = llb_result.max_rhs;
        result.benders_lifted_lower_bound_weighted = llb_result.weighted;
        result.benders_lifted_lower_bound_weight_map_hash = llb_result.weight_map_hash;
        result.benders_lifted_lower_bound_scenarios_precomputed =
            llb_result.scenarios_precomputed;
        result.benders_lifted_lower_bound_singletons_evaluated =
            llb_result.singletons_evaluated;
        result.benders_lifted_lower_bound_no_firebreak_loss_min =
            llb_result.no_firebreak_loss_min;
        result.benders_lifted_lower_bound_no_firebreak_loss_max =
            llb_result.no_firebreak_loss_max;
        result.benders_lifted_lower_bound_singleton_benefit_min =
            llb_result.singleton_benefit_min;
        result.benders_lifted_lower_bound_singleton_benefit_max =
            llb_result.singleton_benefit_max;
        result.benders_lifted_lower_bound_constraints_added =
            result.benders_lifted_lower_bound_count;
        result.benders_lifted_lower_bound_validity_mode = llb_result.validity_mode;
        result.benders_lifted_lower_bound_notes = llb_result.notes;
        result.notes.push_back(
            "Optional DPV lifted lower-bound inequalities were added to the Benders master.");
        result.notes.push_back(
            "LLBI coefficients use optimistic downstream singleton losses, not true singleton recourse values.");
    }

    if (options.warm_start && options.warm_start->enabled) {
        attach_warm_start_metadata(result, *options.warm_start);
        if (!options.warm_start->compact_indices.empty()) {
            master.setWarmStart(warm_start_to_y_values(opt, *options.warm_start));
            result.warm_start_used = true;
            result.notes.push_back("CPLEX y-only MIP start was added to the Benders master.");
        } else {
            result.notes.push_back("Warm-start file was provided but no valid eligible nodes were available.");
        }
    }

    DpvScenarioSubproblem subproblem;
    double incumbent_upper_bound = std::numeric_limits<double>::infinity();
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

	        double incumbent_candidate = 0.0;
	        double iteration_max_violation = 0.0;
	        double violated_cut_violation_sum = 0.0;
	        double total_subproblem_objective = 0.0;
	        double subproblem_time_sec = 0.0;
	        double iteration_max_subproblem_time_sec = 0.0;
	        int iteration_cuts_added = 0;
	        int subproblems_solved = 0;
	        std::vector<solver::BendersAddedCutRecord> iteration_added_cuts;

	        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
	            const auto sub_result = subproblem.solve(opt, static_cast<int>(s), ybar, options.verbose);
	            const double q_value = sub_result.objective_value;
	            total_subproblem_objective += q_value;
	            incumbent_candidate += opt.scenarios[s].probability * q_value;
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

        if (incumbent_candidate < incumbent_upper_bound - options.tolerance || incumbent_y.empty()) {
            incumbent_upper_bound = incumbent_candidate;
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
	        iteration_log.weighted_recourse_objective = incumbent_candidate;
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
	            std::cout << "Benders iteration " << iteration
                      << ": master_lb=" << last_master_bound
                      << " incumbent_ub=" << incumbent_candidate
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

	    if (!incumbent_y.empty()) {
        result.selected_firebreak_indices = selected_compact_indices(opt, incumbent_y);
        result.selected_firebreak_original_nodes = selected_original_nodes(opt, incumbent_y);
    }

    std::size_t subproblem_variables = 0;
    std::size_t subproblem_constraints = 0;
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const auto structure = analyze_dpv_scenario_subproblem_structure(opt, static_cast<int>(s));
        subproblem_variables += structure.total_variable_count;
        subproblem_constraints += structure.total_constraint_count;
    }
    result.num_variables = master.getNumVariables() + subproblem_variables;
    result.num_constraints = master.getNumConstraints() + subproblem_constraints;

    result.notes.push_back("Classical iterative DPV-SAA Benders decomposition; no callbacks or lazy cuts.");
    result.notes.push_back("Master solves binary y and per-scenario eta variables to optimality at each iteration.");
    result.notes.push_back("Scenario subproblems solve the LP relaxation with y_copy fixed to the incumbent y.");
	    result.notes.push_back("Benders cuts use CPLEX equality-row duals directly: eta_s >= Q_s(ybar) + pi*(y-ybar).");
	    attach_dpv_benders_metadata(result, opt, options);
	    result.notes.push_back("DPV objective uses weight(descendant) product-pair weights and scenario probabilities in the master.");
	    if (options.use_lifted_lower_bounds) {
	        result.notes.push_back("Lifted lower-bound inequalities added: " +
	                               std::to_string(result.benders_lifted_lower_bound_count) + ".");
	        result.notes.insert(
	            result.notes.end(),
	            result.benders_lifted_lower_bound_notes.begin(),
	            result.benders_lifted_lower_bound_notes.end());
	    }
	    result.notes.push_back("Benders termination reason: " + termination_reason + ".");
	    result.notes.push_back("Largest observed intermediate Benders violation: " + std::to_string(largest_observed_violation) + ".");
	    return result;
	}

#endif

}  // namespace firebreak::benders
