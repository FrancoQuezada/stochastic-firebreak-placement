#include "benders/FppBranchBendersSolver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/FppCombinatorialBenders.hpp"
#include "benders/FppLiftedLowerBound.hpp"
#include "benders/FppPersistentScenarioSubproblemManager.hpp"
#include "benders/FppProjectedLlbi.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/CplexEnvironment.hpp"
#include "solver/FppWeightedLossUtils.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::benders {

namespace {

void validate_options(const FppBranchBendersOptions& options) {
    if (options.tolerance < 0.0) {
        throw std::runtime_error("FPP Branch-Benders tolerance must be nonnegative.");
    }
    if (options.time_limit_seconds < 0.0) {
        throw std::runtime_error("FPP Branch-Benders time_limit_seconds must be nonnegative.");
    }
    if (options.mip_gap < -1.0) {
        throw std::runtime_error("FPP Branch-Benders mip_gap must be nonnegative, or omitted.");
    }
    if (options.threads < 0) {
        throw std::runtime_error("FPP Branch-Benders threads must be nonnegative.");
    }
    if (options.combinatorial_options.enabled) {
        validate_fpp_combinatorial_benders_options(options.combinatorial_options);
    }
    if (options.root_user_cut_max_rounds <= 0) {
        throw std::runtime_error("FPP Branch-Benders root_user_cut_max_rounds must be positive.");
    }
    if (!std::isnan(options.root_user_cut_tolerance) &&
        options.root_user_cut_tolerance < 0.0) {
        throw std::runtime_error("FPP Branch-Benders root_user_cut_tolerance must be nonnegative.");
    }
    FppProjectedLlbiOptions projected_options;
    projected_options.use_projected_coverage_llbi_exp =
        options.strengthening_options.use_projected_coverage_llbi_exp;
    projected_options.use_projected_path_llbi_exp =
        options.strengthening_options.use_projected_path_llbi_exp;
    projected_options.use_projected_coverage_llbi_poly =
        options.strengthening_options.use_projected_coverage_llbi_poly;
    projected_options.use_projected_path_llbi_poly =
        options.strengthening_options.use_projected_path_llbi_poly;
    projected_options.root_rounds =
        options.strengthening_options.projected_llbi_root_rounds;
    projected_options.max_cuts_per_round =
        options.strengthening_options.projected_llbi_max_cuts_per_round;
    projected_options.violation_tolerance =
        options.strengthening_options.projected_llbi_violation_tolerance;
    projected_options.cut_density_limit =
        options.strengthening_options.projected_llbi_cut_density_limit;
    projected_options.poly_max_cuts =
        options.strengthening_options.projected_poly_max_cuts;
    projected_options.path_max_paths_per_node =
        options.strengthening_options.path_llbi_max_paths_per_node;
    projected_options.export_cuts_path =
        options.strengthening_options.projected_llbi_export_cuts_path;
    validate_fpp_projected_llbi_options(projected_options);
    const int extended_families =
        (options.strengthening_options.use_coverage_llbi ? 1 : 0) +
        (options.strengthening_options.use_path_llbi ? 1 : 0);
    const int projected_families =
        (options.strengthening_options.use_projected_coverage_llbi_exp ? 1 : 0) +
        (options.strengthening_options.use_projected_path_llbi_exp ? 1 : 0) +
        (options.strengthening_options.use_projected_coverage_llbi_poly ? 1 : 0) +
        (options.strengthening_options.use_projected_path_llbi_poly ? 1 : 0);
    if (projected_families > 1 || (projected_families > 0 && extended_families > 0)) {
        throw std::runtime_error(
            "Projected LLBI variants are mutually exclusive with each other and with extended CoverageLLBI/PathLLBI in the FPP Branch-Benders master.");
    }
    risk::RiskMeasureConfig effective_risk_config = options.risk_config;
    if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
        effective_risk_config.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective_risk_config);
}

void validate_instance(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("FPP Branch-Benders requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("FPP Branch-Benders requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("FPP Branch-Benders requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0 || opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("FPP Branch-Benders budget must be between zero and the eligible-node count.");
    }
    if (!opt.compact_cell_weights.empty()) {
        (void)solver::direct_fpp_compact_weights(opt);
    }
}

#ifdef FIREBREAK_WITH_CPLEX

bool has_nonunit_compact_weights(const opt::OptimizationInstance& opt) {
    if (opt.compact_cell_weights.empty()) {
        return false;
    }
    const auto& weights = solver::direct_fpp_compact_weights(opt);
    for (const double weight : weights) {
        if (std::fabs(weight - 1.0) > 1.0e-9) {
            return true;
        }
    }
    return false;
}

bool uses_unconverted_weighted_strengthening(const FppBranchBendersOptions& options) {
    if (options.combinatorial_options.enabled) {
        validate_fpp_phase6c2c_weighted_combinatorial_mode(
            options.combinatorial_options,
            options.use_root_user_cuts,
            options.use_lifted_lower_bounds,
            options.strengthening_options);
        return false;
    }
    return false;
}

#endif

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

#ifdef FIREBREAK_WITH_CPLEX

double effective_root_user_cut_tolerance(const FppBranchBendersOptions& options) {
    if (std::isnan(options.root_user_cut_tolerance)) {
        return options.tolerance;
    }
    return options.root_user_cut_tolerance;
}

FppProjectedLlbiOptions projected_llbi_options_from_strengthening(
    const FppStrengtheningOptions& strengthening_options) {
    FppProjectedLlbiOptions options;
    options.use_projected_coverage_llbi_exp =
        strengthening_options.use_projected_coverage_llbi_exp;
    options.use_projected_path_llbi_exp =
        strengthening_options.use_projected_path_llbi_exp;
    options.use_projected_coverage_llbi_poly =
        strengthening_options.use_projected_coverage_llbi_poly;
    options.use_projected_path_llbi_poly =
        strengthening_options.use_projected_path_llbi_poly;
    options.root_rounds = strengthening_options.projected_llbi_root_rounds;
    options.max_cuts_per_round =
        strengthening_options.projected_llbi_max_cuts_per_round;
    options.violation_tolerance =
        strengthening_options.projected_llbi_violation_tolerance;
    options.cut_density_limit =
        strengthening_options.projected_llbi_cut_density_limit;
    options.poly_max_cuts = strengthening_options.projected_poly_max_cuts;
    options.path_max_paths_per_node =
        strengthening_options.path_llbi_max_paths_per_node;
    options.export_cuts_path =
        strengthening_options.projected_llbi_export_cuts_path;
    return options;
}

struct RiskObjectiveEvaluation {
    double objective = 0.0;
    double expected = 0.0;
    double cvar = std::numeric_limits<double>::quiet_NaN();
    double risk_threshold = std::numeric_limits<double>::quiet_NaN();
};

RiskObjectiveEvaluation evaluate_risk_objective(
    const opt::OptimizationInstance& opt,
    const std::vector<double>& scenario_losses,
    const risk::RiskMeasureConfig& config) {
    if (scenario_losses.size() != opt.scenarios.size()) {
        throw std::runtime_error("FPP Branch-Benders risk evaluation received the wrong loss vector size.");
    }

    RiskObjectiveEvaluation evaluation;
    std::vector<risk::WeightedLoss> weighted_losses;
    weighted_losses.reserve(scenario_losses.size());
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

std::vector<double> expand_y_to_compact_values(
    const opt::OptimizationInstance& opt,
    const std::vector<double>& y_values_by_eligible_position) {
    std::vector<double> compact_values(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < y_values_by_eligible_position.size(); ++pos) {
        compact_values[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            y_values_by_eligible_position[pos];
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

std::string cut_signature(const BendersCut& cut) {
    auto coefficients = cut.coefficients_by_compact_index;
    std::sort(
        coefficients.begin(),
        coefficients.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

    std::ostringstream out;
    out << cut.scenario_id << "|" << std::setprecision(17) << cut.rhs_constant;
    for (const auto& [compact_index, coefficient] : coefficients) {
        if (std::fabs(coefficient) <= 1.0e-12) {
            continue;
        }
        out << "|" << compact_index << ":" << std::setprecision(17) << coefficient;
    }
    return out.str();
}

std::string combinatorial_cut_signature(
    const BendersCut& cut,
    const std::string& weight_map_hash,
    const std::vector<int>& eligible_indices) {
    std::ostringstream out;
    out << "fpp-combinatorial-baseline|" << cut_signature(cut) << "|weight_hash="
        << weight_map_hash << "|candidates=";
    for (const int compact_node : eligible_indices) {
        out << compact_node << ";";
    }
    return out.str();
}

std::string fpp_fractional_combinatorial_validity_mode(bool weighted) {
    return weighted
        ? "weighted-fractional-path-activation-user-cut-convex-hull-valid"
        : "unit-fractional-path-activation-user-cut-convex-hull-valid";
}

struct BranchBendersVariableAccess {
    IloBoolVarArray y;
    IloNumVarArray eta;
    std::vector<int> y_position_by_node;
};

struct BranchBendersCallbackStats {
    int callback_calls = 0;
    int candidate_callback_calls = 0;
    int incumbent_callback_calls = 0;
    int candidate_incumbents_checked = 0;
    int subproblems_attempted = 0;
    int subproblems_solved = 0;
    int lazy_cuts_added = 0;
    int violated_cuts = 0;
    int nonviolated_cuts = 0;
    int skipped_cuts = 0;
    int duplicate_cuts = 0;
    double largest_incumbent_cut_violation = 0.0;
    double callback_time_sec = 0.0;
    double subproblem_time_sec = 0.0;
    double max_subproblem_time_sec = 0.0;
    double cut_construction_time_sec = 0.0;
    double lazy_cut_insertion_time_sec = 0.0;
    std::set<std::string> cut_signatures;
    std::vector<solver::BranchBendersIncumbentLog> incumbent_log;
    FppCombinatorialBendersStats combinatorial_stats;
    mutable std::mutex mutex;
};

struct BranchBendersRootUserCutStats {
    bool enabled = false;
    int max_rounds = 1;
    double tolerance = 1.0e-6;
    int callback_calls = 0;
    int rounds_executed = 0;
    int cuts_added = 0;
    int scenarios_solved = 0;
    double max_violation = 0.0;
    double total_time_sec = 0.0;
    double subproblem_time_sec = 0.0;
    double max_subproblem_time_sec = 0.0;
    bool only_at_root_confirmed = true;
    std::string skipped_reason;
    std::vector<solver::BranchBendersRootUserCutRoundLog> round_log;
    mutable std::mutex mutex;
};

double add_coverage_llbi_constraints(
    IloEnv& env,
    IloModel& model,
    const FppCoverageLlbiData& data,
    const IloBoolVarArray& y,
    const IloNumVarArray& eta,
    const std::vector<int>& y_position_by_node) {
    const auto start = std::chrono::steady_clock::now();
    if (!data.enabled) {
        return 0.0;
    }
    for (const auto& scenario_record : data.scenarios) {
        IloExpr lower_bound_rhs(env);
        lower_bound_rhs += scenario_record.empty_burned_area;
        for (const auto& node_record : scenario_record.nodes) {
            IloNumVar zeta(env, 0.0, 1.0, ILOFLOAT);
            std::ostringstream zeta_name;
            zeta_name << "coverage_zeta_s" << scenario_record.scenario_id
                      << "_" << node_record.compact_node;
            zeta.setName(zeta_name.str().c_str());

            IloExpr cover(env);
            for (const int candidate : node_record.covering_candidate_compact_nodes) {
                if (candidate < 0 ||
                    candidate >= static_cast<int>(y_position_by_node.size()) ||
                    y_position_by_node[static_cast<std::size_t>(candidate)] < 0) {
                    continue;
                }
                cover += y[static_cast<IloInt>(
                    y_position_by_node[static_cast<std::size_t>(candidate)])];
            }
            cover -= zeta;
            model.add(cover >= 0.0);
            cover.end();
            lower_bound_rhs -= node_record.cell_weight * zeta;
        }
        if (!scenario_record.nodes.empty()) {
            IloExpr lhs(env);
            lhs += eta[static_cast<IloInt>(scenario_record.scenario_index)];
            lhs -= lower_bound_rhs;
            model.add(lhs >= 0.0);
            lhs.end();
        }
        lower_bound_rhs.end();
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

double add_path_llbi_constraints(
    IloEnv& env,
    IloModel& model,
    const FppPathLlbiData& data,
    const IloBoolVarArray& y,
    const IloNumVarArray& eta,
    const std::vector<int>& y_position_by_node) {
    const auto start = std::chrono::steady_clock::now();
    if (!data.enabled) {
        return 0.0;
    }
    for (const auto& scenario_record : data.scenarios) {
        IloExpr eta_lower_bound(env);
        for (const auto& node_record : scenario_record.nodes) {
            IloNumVar burn_lb(env, 0.0, 1.0, ILOFLOAT);
            std::ostringstream b_name;
            b_name << "path_b_s" << scenario_record.scenario_id
                   << "_" << node_record.compact_node;
            burn_lb.setName(b_name.str().c_str());
            eta_lower_bound += node_record.cell_weight * burn_lb;
            for (const auto& path : node_record.paths) {
                IloExpr expr(env);
                expr += burn_lb;
                for (const int candidate : path.blocking_candidate_compact_nodes) {
                    if (candidate < 0 ||
                        candidate >= static_cast<int>(y_position_by_node.size()) ||
                        y_position_by_node[static_cast<std::size_t>(candidate)] < 0) {
                        continue;
                    }
                    expr += y[static_cast<IloInt>(
                        y_position_by_node[static_cast<std::size_t>(candidate)])];
                }
                model.add(expr >= 1.0);
                expr.end();
            }
        }
        if (!scenario_record.nodes.empty()) {
            IloExpr lhs(env);
            lhs += eta[static_cast<IloInt>(scenario_record.scenario_index)];
            lhs -= eta_lower_bound;
            model.add(lhs >= 0.0);
            lhs.end();
        }
        eta_lower_bound.end();
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void add_benders_cut_to_model(
    IloEnv& env,
    IloModel& model,
    const BendersCut& benders_cut,
    const BranchBendersVariableAccess& access,
    int scenario_position,
    const std::string& context) {
    IloExpr expr(env);
    expr += access.eta[static_cast<IloInt>(scenario_position)];
    for (const auto& [compact_index, coefficient] :
         benders_cut.coefficients_by_compact_index) {
        if (std::fabs(coefficient) <= 1.0e-12) {
            continue;
        }
        if (compact_index < 0 ||
            compact_index >= static_cast<int>(access.y_position_by_node.size()) ||
            access.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
            expr.end();
            throw std::runtime_error(context + " references a compact node without a master y variable.");
        }
        const int y_pos =
            access.y_position_by_node[static_cast<std::size_t>(compact_index)];
        expr -= coefficient * access.y[static_cast<IloInt>(y_pos)];
    }
    model.add(expr >= benders_cut.rhs_constant);
    expr.end();
}

IloRange make_benders_cut_range(
    IloEnv& env,
    const BendersCut& benders_cut,
    const BranchBendersVariableAccess& access,
    int scenario_position,
    const std::string& context) {
    IloExpr expr(env);
    expr += access.eta[static_cast<IloInt>(scenario_position)];
    for (const auto& [compact_index, coefficient] :
         benders_cut.coefficients_by_compact_index) {
        if (std::fabs(coefficient) <= 1.0e-12) {
            continue;
        }
        if (compact_index < 0 ||
            compact_index >= static_cast<int>(access.y_position_by_node.size()) ||
            access.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
            expr.end();
            throw std::runtime_error(context + " references a compact node without a master y variable.");
        }
        const int y_pos =
            access.y_position_by_node[static_cast<std::size_t>(compact_index)];
        expr -= coefficient * access.y[static_cast<IloInt>(y_pos)];
    }
    IloRange range(expr >= benders_cut.rhs_constant);
    expr.end();
    return range;
}

void accumulate_combinatorial_summary(
    FppCombinatorialBendersStats& stats,
    const FppCombinatorialSeparationSummary& summary,
    bool fractional,
    int cuts_added) {
    if (stats.realized_sample_size == 0 ||
        (summary.realized_sample_size > 0 &&
         summary.realized_sample_size < stats.realized_sample_size)) {
        stats.realized_sample_size = summary.realized_sample_size;
    }
    stats.sampling_exact_fallback =
        stats.sampling_exact_fallback || summary.sampling_exact_fallback;
    stats.scenario_policy_exact =
        stats.scenario_policy_exact && summary.scenario_policy_exact;
    stats.scenario_policy_heuristic =
        stats.scenario_policy_heuristic || summary.scenario_policy_heuristic;
    stats.full_verification_before_acceptance =
        stats.full_verification_before_acceptance &&
        summary.full_verification_before_acceptance;
    stats.sampling_time_sec += summary.sampling_time_sec;
    stats.ordering_time_sec += summary.ordering_time_sec;
    stats.scenarios_checked += summary.scenarios_checked;
    stats.separation_time_sec += summary.separation_time_sec;
    stats.propagation_time_sec += summary.propagation_time_sec;
    stats.cut_build_time_sec += summary.cut_build_time_sec;
    stats.weighted_recourse_evaluations += summary.weighted_recourse_evaluations;
    stats.tight_cuts += summary.tight_cuts;
    stats.max_tightness_error =
        std::max(stats.max_tightness_error, summary.max_tightness_error);
    stats.max_violation = std::max(stats.max_violation, summary.max_violation);
    stats.lifting_attempts += summary.lifting_attempts;
    stats.lifting_successes += summary.lifting_successes;
    stats.lifting_failures += summary.lifting_failures;
    stats.candidates_considered_for_lifting += summary.candidates_considered_for_lifting;
    stats.coefficients_changed_by_lifting += summary.coefficients_changed_by_lifting;
    stats.propagation_evaluations_for_lifting +=
        summary.propagation_evaluations_for_lifting;
    stats.baseline_cut_nonzeros += summary.baseline_cut_nonzeros;
    stats.lifted_cut_nonzeros += summary.lifted_cut_nonzeros;
    stats.lifted_cuts_dominating_baseline +=
        summary.lifted_cuts_dominating_baseline;
    stats.max_coefficient_change =
        std::max(stats.max_coefficient_change, summary.max_coefficient_change);
    stats.max_baseline_tightness_error =
        std::max(stats.max_baseline_tightness_error, summary.max_baseline_tightness_error);
    stats.max_lifted_tightness_error =
        std::max(stats.max_lifted_tightness_error, summary.max_lifted_tightness_error);
    stats.lifting_time_sec += summary.lifting_time_sec;
    stats.num_violated_cuts += summary.violated_cuts;
    stats.lift_fallback_count += summary.lift_fallback_count;
    if (summary.lift_fallback_count > 0) {
        stats.fractional_lift_disabled_due_to_validity = true;
    }
    if (fractional) {
        stats.fractional_cuts_added += cuts_added;
        ++stats.fractional_separation_calls;
        stats.fractional_scenarios_evaluated += summary.scenarios_checked;
        stats.fractional_cuts_generated += summary.violated_cuts;
        stats.fractional_max_violation =
            std::max(stats.fractional_max_violation, summary.max_violation);
        stats.fractional_max_tightness_error =
            std::max(stats.fractional_max_tightness_error, summary.max_tightness_error);
        stats.fractional_separation_time_sec += summary.separation_time_sec;
    } else {
        stats.integer_cuts_added += cuts_added;
        stats.candidate_initial_sample_scenarios_evaluated +=
            summary.initial_sample_scenarios_evaluated;
        stats.candidate_fallback_scenarios_evaluated +=
            summary.fallback_scenarios_evaluated;
        stats.candidate_full_sweeps += summary.candidate_full_sweeps;
        stats.candidates_rejected_in_initial_sample +=
            summary.candidates_rejected_in_initial_sample;
        stats.candidates_rejected_in_fallback +=
            summary.candidates_rejected_in_fallback;
        stats.candidates_fully_verified += summary.candidates_fully_verified;
        stats.sampled_violations += summary.sampled_violations;
        stats.fallback_violations += summary.fallback_violations;
        stats.scenarios_skipped_after_candidate_rejection +=
            summary.scenarios_skipped_after_candidate_rejection;
    }
    for (const auto& cut : summary.cuts) {
        stats.total_paths_per_cut += static_cast<double>(cut.activation_paths);
        stats.total_nonzeros_per_cut += static_cast<double>(cut.nonzeros);
        ++stats.cuts_for_averages;
    }
}

class FppBranchBendersCandidateCallback : public IloCplex::Callback::Function {
public:
    FppBranchBendersCandidateCallback(
        const opt::OptimizationInstance& opt,
        FppPersistentScenarioSubproblemManager& subproblem_manager,
        BranchBendersVariableAccess access,
        double tolerance,
        bool verbose,
        BranchBendersCallbackStats& stats)
        : opt_(opt),
          subproblem_manager_(subproblem_manager),
          access_(std::move(access)),
          tolerance_(tolerance),
          verbose_(verbose),
          stats_(stats) {}

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE {
        const auto callback_start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.callback_calls;
            ++stats_.candidate_callback_calls;
        }

        if (!context.inCandidate() || !context.isCandidatePoint()) {
            record_callback_time(callback_start);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.incumbent_callback_calls;
        }

        std::vector<int> ybar(static_cast<std::size_t>(access_.y.getSize()), 0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            ybar[static_cast<std::size_t>(pos)] =
                context.getCandidatePoint(access_.y[pos]) > 0.5 ? 1 : 0;
        }

        std::vector<double> eta_values(static_cast<std::size_t>(access_.eta.getSize()), 0.0);
        for (IloInt s = 0; s < access_.eta.getSize(); ++s) {
            eta_values[static_cast<std::size_t>(s)] = context.getCandidatePoint(access_.eta[s]);
        }

        const auto compact_y = expand_y_to_compact_values(opt_, ybar);
        IloEnv env = context.getEnv();
        IloRangeArray lazy_cuts(env);

        int cuts_added = 0;
        int violated_cuts = 0;
        int nonviolated_cuts = 0;
        int skipped_cuts = 0;
        int duplicate_cuts = 0;
        int subproblems_attempted = 0;
        int subproblems_solved = 0;
        double max_violation = 0.0;
        double subproblem_time = 0.0;
        double max_subproblem_time = 0.0;
        double cut_construction_time = 0.0;
        double lazy_cut_insertion_time = 0.0;

        for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
            ++subproblems_attempted;
            const auto sub_result =
                subproblem_manager_.solveScenario(static_cast<int>(s), ybar);
            ++subproblems_solved;
            subproblem_time += sub_result.runtime_seconds;
            max_subproblem_time = std::max(max_subproblem_time, sub_result.runtime_seconds);

            const double eta_value = eta_values[s];
            const double direct_violation = sub_result.objective_value - eta_value;
            const double cut_violation =
                sub_result.benders_cut.violationAt(eta_value, compact_y);
            const double violation = std::max(direct_violation, cut_violation);
            max_violation = std::max(max_violation, violation);

            if (violation > tolerance_) {
                ++violated_cuts;
                const auto signature = cut_signature(sub_result.benders_cut);
                {
                    std::lock_guard<std::mutex> lock(stats_.mutex);
                    const auto [_, inserted] = stats_.cut_signatures.insert(signature);
                    if (!inserted) {
                        ++duplicate_cuts;
                    }
                }
                const auto cut_construction_start = std::chrono::steady_clock::now();
                IloExpr expr(env);
                expr += access_.eta[static_cast<IloInt>(s)];
                for (const auto& [compact_index, coefficient] :
                     sub_result.benders_cut.coefficients_by_compact_index) {
                    if (std::fabs(coefficient) <= 1.0e-12) {
                        continue;
                    }
                    if (compact_index < 0 ||
                        compact_index >= static_cast<int>(access_.y_position_by_node.size()) ||
                        access_.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
                        expr.end();
                        lazy_cuts.end();
                        throw std::runtime_error(
                            "FPP Branch-Benders cut references a compact node without a master y variable.");
                    }
                    const int y_pos =
                        access_.y_position_by_node[static_cast<std::size_t>(compact_index)];
                    expr -= coefficient * access_.y[static_cast<IloInt>(y_pos)];
                }
                IloRange cut(expr >= sub_result.benders_cut.rhs_constant);
                cut_construction_time += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - cut_construction_start).count();
                const auto lazy_insert_start = std::chrono::steady_clock::now();
                lazy_cuts.add(cut);
                lazy_cut_insertion_time += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - lazy_insert_start).count();
                expr.end();
                ++cuts_added;
            } else {
                ++nonviolated_cuts;
            }
        }

        if (lazy_cuts.getSize() > 0) {
            const auto lazy_insert_start = std::chrono::steady_clock::now();
            context.rejectCandidate(lazy_cuts);
            lazy_cut_insertion_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - lazy_insert_start).count();
        }
        lazy_cuts.end();

        solver::BranchBendersIncumbentLog log;
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.candidate_incumbents_checked;
            stats_.subproblems_attempted += subproblems_attempted;
            stats_.subproblems_solved += subproblems_solved;
            stats_.lazy_cuts_added += cuts_added;
            stats_.violated_cuts += violated_cuts;
            stats_.nonviolated_cuts += nonviolated_cuts;
            stats_.skipped_cuts += skipped_cuts;
            stats_.duplicate_cuts += duplicate_cuts;
            stats_.subproblem_time_sec += subproblem_time;
            stats_.max_subproblem_time_sec =
                std::max(stats_.max_subproblem_time_sec, max_subproblem_time);
            stats_.cut_construction_time_sec += cut_construction_time;
            stats_.lazy_cut_insertion_time_sec += lazy_cut_insertion_time;
            stats_.largest_incumbent_cut_violation =
                std::max(stats_.largest_incumbent_cut_violation, max_violation);

            log.incumbent_index = stats_.candidate_incumbents_checked;
            log.incumbent_objective = context.getCandidateObjective();
            log.selected_firebreak_count =
                static_cast<int>(selected_original_nodes(opt_, ybar).size());
            log.cuts_added = cuts_added;
            log.max_cut_violation = max_violation;
            log.subproblems_attempted = subproblems_attempted;
            log.subproblems_solved = subproblems_solved;
            log.subproblem_time_sec = subproblem_time;
            log.average_subproblem_time_sec =
                subproblems_solved > 0
                    ? subproblem_time / static_cast<double>(subproblems_solved)
                    : 0.0;
            log.max_subproblem_time_sec = max_subproblem_time;
            log.cut_construction_time_sec = cut_construction_time;
            log.lazy_cut_insertion_time_sec = lazy_cut_insertion_time;
            log.violated_cuts = violated_cuts;
            log.nonviolated_cuts = nonviolated_cuts;
            log.skipped_cuts = skipped_cuts;
            log.duplicate_cuts = duplicate_cuts;
            stats_.incumbent_log.push_back(log);
        }

        record_callback_time(callback_start);
    }

private:
    void record_callback_time(std::chrono::steady_clock::time_point start) {
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::lock_guard<std::mutex> lock(stats_.mutex);
        stats_.callback_time_sec += elapsed;
    }

    const opt::OptimizationInstance& opt_;
    FppPersistentScenarioSubproblemManager& subproblem_manager_;
    BranchBendersVariableAccess access_;
    double tolerance_ = 1.0e-6;
    bool verbose_ = false;
    BranchBendersCallbackStats& stats_;
};

class FppBranchBendersRootUserCutCallback : public IloCplex::Callback::Function {
public:
    FppBranchBendersRootUserCutCallback(
        const opt::OptimizationInstance& opt,
        FppPersistentScenarioSubproblemManager& subproblem_manager,
        BranchBendersVariableAccess access,
        int max_rounds,
        double tolerance,
        bool verbose,
        BranchBendersRootUserCutStats& stats)
        : opt_(opt),
          subproblem_manager_(subproblem_manager),
          access_(std::move(access)),
          max_rounds_(max_rounds),
          tolerance_(tolerance),
          verbose_(verbose),
          stats_(stats) {}

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE {
        const auto callback_start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.callback_calls;
        }

        if (!context.inRelaxation()) {
            record_total_time(callback_start);
            return;
        }

        CPXLONG depth = -1;
        try {
            depth = context.getLongInfo(IloCplex::Callback::Context::Info::NodeDepth);
        } catch (...) {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            stats_.only_at_root_confirmed = false;
            if (stats_.skipped_reason.empty()) {
                stats_.skipped_reason = "NodeDepth unavailable; root user cuts skipped.";
            }
            record_total_time_unlocked(callback_start);
            return;
        }

        if (depth != 0) {
            record_total_time(callback_start);
            return;
        }

        int round_index = 0;
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            if (stats_.rounds_executed >= max_rounds_) {
                if (stats_.skipped_reason.empty()) {
                    stats_.skipped_reason = "Root user cut max rounds reached.";
                }
                record_total_time_unlocked(callback_start);
                return;
            }
            ++stats_.rounds_executed;
            round_index = stats_.rounds_executed;
        }

        std::vector<double> ybar(static_cast<std::size_t>(access_.y.getSize()), 0.0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            const double value = context.getRelaxationPoint(access_.y[pos]);
            ybar[static_cast<std::size_t>(pos)] = std::max(0.0, std::min(1.0, value));
        }

        std::vector<double> eta_values(static_cast<std::size_t>(access_.eta.getSize()), 0.0);
        for (IloInt s = 0; s < access_.eta.getSize(); ++s) {
            eta_values[static_cast<std::size_t>(s)] = context.getRelaxationPoint(access_.eta[s]);
        }

        const auto compact_y = expand_y_to_compact_values(opt_, ybar);

        int cuts_added = 0;
        int scenarios_solved = 0;
        double max_violation = 0.0;
        double sum_violation = 0.0;
        double subproblem_time = 0.0;
        double max_subproblem_time = 0.0;

        for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
            const auto sub_result =
                subproblem_manager_.solveScenarioFractional(static_cast<int>(s), ybar);
            ++scenarios_solved;
            subproblem_time += sub_result.runtime_seconds;
            max_subproblem_time = std::max(max_subproblem_time, sub_result.runtime_seconds);

            const double eta_value = eta_values[s];
            const double violation =
                sub_result.benders_cut.violationAt(eta_value, compact_y);
            max_violation = std::max(max_violation, violation);

            if (violation > tolerance_) {
                sum_violation += violation;
                IloEnv env = context.getEnv();
                IloExpr expr(env);
                expr += access_.eta[static_cast<IloInt>(s)];
                for (const auto& [compact_index, coefficient] :
                     sub_result.benders_cut.coefficients_by_compact_index) {
                    if (std::fabs(coefficient) <= 1.0e-12) {
                        continue;
                    }
                    if (compact_index < 0 ||
                        compact_index >= static_cast<int>(access_.y_position_by_node.size()) ||
                        access_.y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
                        expr.end();
                        throw std::runtime_error(
                            "FPP Branch-Benders root user cut references a compact node without a master y variable.");
                    }
                    const int y_pos =
                        access_.y_position_by_node[static_cast<std::size_t>(compact_index)];
                    expr -= coefficient * access_.y[static_cast<IloInt>(y_pos)];
                }
                IloRange cut(expr >= sub_result.benders_cut.rhs_constant);
                context.addUserCut(cut, IloCplex::UseCutPurge, IloFalse);
                cut.end();
                expr.end();
                ++cuts_added;
            }
        }

        solver::BranchBendersRootUserCutRoundLog log;
        log.round_index = round_index;
        log.scenarios_solved = scenarios_solved;
        log.cuts_added = cuts_added;
        log.max_violation = max_violation;
        log.avg_violation =
            cuts_added > 0 ? sum_violation / static_cast<double>(cuts_added) : 0.0;
        log.time_sec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - callback_start).count();

        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            stats_.cuts_added += cuts_added;
            stats_.scenarios_solved += scenarios_solved;
            stats_.max_violation = std::max(stats_.max_violation, max_violation);
            stats_.subproblem_time_sec += subproblem_time;
            stats_.max_subproblem_time_sec =
                std::max(stats_.max_subproblem_time_sec, max_subproblem_time);
            stats_.round_log.push_back(log);
            record_total_time_unlocked(callback_start);
        }
    }

private:
    void record_total_time(std::chrono::steady_clock::time_point start) {
        std::lock_guard<std::mutex> lock(stats_.mutex);
        record_total_time_unlocked(start);
    }

    void record_total_time_unlocked(std::chrono::steady_clock::time_point start) {
        stats_.total_time_sec += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
    }

    const opt::OptimizationInstance& opt_;
    FppPersistentScenarioSubproblemManager& subproblem_manager_;
    BranchBendersVariableAccess access_;
    int max_rounds_ = 1;
    double tolerance_ = 1.0e-6;
    bool verbose_ = false;
    BranchBendersRootUserCutStats& stats_;
};

class FppBranchBendersCombinedCallback : public IloCplex::Callback::Function {
public:
    FppBranchBendersCombinedCallback(
        const opt::OptimizationInstance& opt,
        FppPersistentScenarioSubproblemManager& subproblem_manager,
        BranchBendersVariableAccess access,
        double candidate_tolerance,
        int root_max_rounds,
        double root_tolerance,
        bool verbose,
        BranchBendersCallbackStats& candidate_stats,
        BranchBendersRootUserCutStats& root_stats)
        : candidate_callback_(
              opt,
              subproblem_manager,
              access,
              candidate_tolerance,
              verbose,
              candidate_stats),
          root_callback_(
              opt,
              subproblem_manager,
              std::move(access),
              root_max_rounds,
              root_tolerance,
              verbose,
              root_stats) {}

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE {
        if (context.inCandidate()) {
            candidate_callback_.invoke(context);
            return;
        }
        if (context.inRelaxation()) {
            root_callback_.invoke(context);
        }
    }

private:
    FppBranchBendersCandidateCallback candidate_callback_;
    FppBranchBendersRootUserCutCallback root_callback_;
};

class FppCombinatorialBranchBendersCallback : public IloCplex::Callback::Function {
public:
    FppCombinatorialBranchBendersCallback(
        const opt::OptimizationInstance& opt,
        const FppCombinatorialBendersSeparator& separator,
        FppCombinatorialBendersOptions combinatorial_options,
        BranchBendersVariableAccess access,
        double tolerance,
        BranchBendersCallbackStats& stats)
        : opt_(opt),
          separator_(separator),
          combinatorial_options_(combinatorial_options),
          access_(std::move(access)),
          tolerance_(tolerance),
          stats_(stats) {}

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE {
        const auto callback_start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.callback_calls;
        }
        if (context.inCandidate()) {
            separate_candidate(context, callback_start);
            return;
        }
        if (context.inRelaxation()) {
            separate_relaxation(context, callback_start);
            return;
        }
        record_callback_time(callback_start);
    }

private:
    void separate_candidate(
        const IloCplex::Callback::Context& context,
        std::chrono::steady_clock::time_point callback_start) {
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.candidate_callback_calls;
        }
        if (!context.isCandidatePoint()) {
            record_callback_time(callback_start);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.incumbent_callback_calls;
        }

        std::vector<int> ybar_int(static_cast<std::size_t>(access_.y.getSize()), 0);
        std::vector<double> ybar(static_cast<std::size_t>(access_.y.getSize()), 0.0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            const int value = context.getCandidatePoint(access_.y[pos]) > 0.5 ? 1 : 0;
            ybar_int[static_cast<std::size_t>(pos)] = value;
            ybar[static_cast<std::size_t>(pos)] = static_cast<double>(value);
        }
        std::vector<double> eta_values(static_cast<std::size_t>(access_.eta.getSize()), 0.0);
        for (IloInt s = 0; s < access_.eta.getSize(); ++s) {
            eta_values[static_cast<std::size_t>(s)] = context.getCandidatePoint(access_.eta[s]);
        }

        const auto summary = separator_.separateViolatedCuts(
            ybar,
            eta_values,
            false,
            combinatorial_options_.lift_mode,
            combinatorial_options_.scenario_order,
            combinatorial_options_.cut_sampling_ratio,
            tolerance_);

        IloEnv env = context.getEnv();
        IloRangeArray lazy_cuts(env);
        int cuts_added = 0;
        int duplicate_cuts = 0;
        double cut_construction_time = 0.0;
        double lazy_cut_insertion_time = 0.0;
        for (const auto& separated : summary.cuts) {
            const auto signature = combinatorial_cut_signature(
                separated.cut,
                separator_.weightMapHash(),
                opt_.eligible_indices);
            {
                std::lock_guard<std::mutex> lock(stats_.mutex);
                const auto [_, inserted] = stats_.cut_signatures.insert(signature);
                if (!inserted) {
                    ++duplicate_cuts;
                    ++stats_.combinatorial_stats.duplicate_cuts;
                }
            }
            const auto cut_start = std::chrono::steady_clock::now();
            IloRange cut = make_benders_cut_range(
                env,
                separated.cut,
                access_,
                scenario_position_for_cut(separated.cut),
                "FPP combinatorial Branch-Benders lazy cut");
            cut_construction_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cut_start).count();
            const auto insert_start = std::chrono::steady_clock::now();
            lazy_cuts.add(cut);
            lazy_cut_insertion_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - insert_start).count();
            ++cuts_added;
        }
        if (lazy_cuts.getSize() > 0) {
            const auto reject_start = std::chrono::steady_clock::now();
            context.rejectCandidate(lazy_cuts);
            lazy_cut_insertion_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - reject_start).count();
        }
        lazy_cuts.end();

        solver::BranchBendersIncumbentLog log;
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.candidate_incumbents_checked;
            stats_.lazy_cuts_added += cuts_added;
            stats_.violated_cuts += summary.violated_cuts;
            stats_.nonviolated_cuts += summary.nonviolated_cuts;
            stats_.skipped_cuts += summary.scenarios_skipped;
            stats_.duplicate_cuts += duplicate_cuts;
            stats_.cut_construction_time_sec += cut_construction_time;
            stats_.lazy_cut_insertion_time_sec += lazy_cut_insertion_time;
            stats_.largest_incumbent_cut_violation =
                std::max(stats_.largest_incumbent_cut_violation, summary.max_violation);
            accumulate_combinatorial_summary(
                stats_.combinatorial_stats,
                summary,
                false,
                cuts_added);

            log.incumbent_index = stats_.candidate_incumbents_checked;
            log.incumbent_objective = context.getCandidateObjective();
            log.selected_firebreak_count =
                static_cast<int>(selected_original_nodes(opt_, ybar_int).size());
            log.cuts_added = cuts_added;
            log.max_cut_violation = summary.max_violation;
            log.subproblems_attempted = 0;
            log.subproblems_solved = 0;
            log.subproblem_time_sec = 0.0;
            log.average_subproblem_time_sec = 0.0;
            log.max_subproblem_time_sec = 0.0;
            log.cut_construction_time_sec = cut_construction_time;
            log.lazy_cut_insertion_time_sec = lazy_cut_insertion_time;
            log.violated_cuts = summary.violated_cuts;
            log.nonviolated_cuts = summary.nonviolated_cuts;
            log.skipped_cuts = summary.scenarios_skipped;
            log.duplicate_cuts = duplicate_cuts;
            stats_.incumbent_log.push_back(log);
        }
        record_callback_time(callback_start);
    }

    void separate_relaxation(
        const IloCplex::Callback::Context& context,
        std::chrono::steady_clock::time_point callback_start) {
        if (!combinatorial_options_.separate_fractional) {
            record_callback_time(callback_start);
            return;
        }
        std::vector<double> ybar(static_cast<std::size_t>(access_.y.getSize()), 0.0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            const double value = context.getRelaxationPoint(access_.y[pos]);
            ybar[static_cast<std::size_t>(pos)] = std::max(0.0, std::min(1.0, value));
        }
        std::vector<double> eta_values(static_cast<std::size_t>(access_.eta.getSize()), 0.0);
        for (IloInt s = 0; s < access_.eta.getSize(); ++s) {
            eta_values[static_cast<std::size_t>(s)] = context.getRelaxationPoint(access_.eta[s]);
        }
        const auto summary = separator_.separateViolatedCuts(
            ybar,
            eta_values,
            true,
            combinatorial_options_.lift_mode,
            combinatorial_options_.scenario_order,
            combinatorial_options_.cut_sampling_ratio,
            tolerance_);

        int cuts_added = 0;
        int duplicate_cuts = 0;
        double cut_construction_time = 0.0;
        for (const auto& separated : summary.cuts) {
            const auto signature = combinatorial_cut_signature(
                separated.cut,
                separator_.weightMapHash(),
                opt_.eligible_indices);
            {
                std::lock_guard<std::mutex> lock(stats_.mutex);
                const auto [_, inserted] = stats_.cut_signatures.insert(signature);
                if (!inserted) {
                    ++duplicate_cuts;
                    ++stats_.combinatorial_stats.duplicate_cuts;
                    ++stats_.combinatorial_stats.fractional_duplicate_cuts;
                    continue;
                }
            }
            const auto cut_start = std::chrono::steady_clock::now();
            IloEnv env = context.getEnv();
            IloRange cut = make_benders_cut_range(
                env,
                separated.cut,
                access_,
                scenario_position_for_cut(separated.cut),
                "FPP combinatorial Branch-Benders user cut");
            cut_construction_time += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cut_start).count();
            context.addUserCut(cut, IloCplex::UseCutPurge, IloFalse);
            cut.end();
            ++cuts_added;
        }
        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            stats_.cut_construction_time_sec += cut_construction_time;
            stats_.violated_cuts += summary.violated_cuts;
            stats_.nonviolated_cuts += summary.nonviolated_cuts;
            stats_.skipped_cuts += summary.scenarios_skipped;
            stats_.duplicate_cuts += duplicate_cuts;
            accumulate_combinatorial_summary(
                stats_.combinatorial_stats,
                summary,
                true,
                cuts_added);
        }
        record_callback_time(callback_start);
    }

    int scenario_position_for_cut(const BendersCut& cut) const {
        for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
            if (opt_.scenarios[s].scenario_id == cut.scenario_id) {
                return static_cast<int>(s);
            }
        }
        throw std::runtime_error(
            "FPP combinatorial Benders cut references an unknown scenario id.");
    }

    void record_callback_time(std::chrono::steady_clock::time_point start) {
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::lock_guard<std::mutex> lock(stats_.mutex);
        stats_.callback_time_sec += elapsed;
    }

    const opt::OptimizationInstance& opt_;
    const FppCombinatorialBendersSeparator& separator_;
    FppCombinatorialBendersOptions combinatorial_options_;
    BranchBendersVariableAccess access_;
    double tolerance_ = 1.0e-6;
    BranchBendersCallbackStats& stats_;
};

void apply_cplex_parameters(
    IloCplex& cplex,
    IloEnv env,
    const FppBranchBendersOptions& options) {
    if (!options.verbose) {
        cplex.setOut(env.getNullStream());
        cplex.setWarning(env.getNullStream());
    }
    if (options.time_limit_seconds > 0.0) {
        cplex.setParam(IloCplex::Param::TimeLimit, options.time_limit_seconds);
    }
    if (options.mip_gap >= 0.0) {
        cplex.setParam(IloCplex::Param::MIP::Tolerances::MIPGap, options.mip_gap);
    }
    if (options.threads > 0) {
        cplex.setParam(IloCplex::Param::Threads, options.threads);
    }
}

#endif

}  // namespace

FppBranchBendersMasterStructure analyze_fpp_branch_benders_master_structure(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config) {
    validate_instance(opt);
    const auto effective_risk_config = effective_risk_config_from(risk_config);
    const bool risk_enabled = uses_cvar_risk(effective_risk_config);
    FppBranchBendersMasterStructure structure;
    structure.y_variable_count = opt.eligible_indices.size();
    structure.eta_variable_count = opt.scenarios.size();
    if (risk_enabled) {
        structure.risk_threshold_variable_count = 1;
        structure.cvar_excess_variable_count = opt.scenarios.size();
        structure.risk_constraint_count = opt.scenarios.size();
    }
    structure.total_variable_count =
        structure.y_variable_count +
        structure.eta_variable_count +
        structure.risk_threshold_variable_count +
        structure.cvar_excess_variable_count;
    structure.budget_constraint_count = 1;
    structure.base_constraint_count = 1;
    structure.has_scenario_recourse_variables = false;
    return structure;
}

#ifndef FIREBREAK_WITH_CPLEX

solver::ModelResult FppBranchBendersSolver::solve(
    const opt::OptimizationInstance& opt,
    const FppBranchBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);
    throw std::runtime_error(solver::cplex_unavailable_message());
}

#else

solver::ModelResult FppBranchBendersSolver::solve(
    const opt::OptimizationInstance& opt,
    const FppBranchBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);
    if (has_nonunit_compact_weights(opt) &&
        uses_unconverted_weighted_strengthening(options)) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP Branch-Benders Phase 6C2C supports LP lazy cuts, root user cuts, standard downstream-union LLBI, extended CoverageLLBI, extended PathLLBI, projected CoverageLLBI, projected PathLLBI, structural global dominance, conditional zero-benefit diagnostics, and combinatorial Benders with lift_mode=none|heuristic|posterior, eta-asc|eta-desc ordering, exact sampling-first fallback, optional binary initial cuts, and optional fractional path user cuts.");
    }
    const auto risk_config = effective_risk_config_from(options.risk_config);
    const bool risk_enabled = uses_cvar_risk(risk_config);

    solver::ModelResult result;
    result.method = options.combinatorial_options.enabled
        ? "FPP-Branch-Benders-Combinatorial"
        : "FPP-SAA-Branch-Benders";
    result.risk_measure = risk::to_string(risk_config.type);
    result.cvar_beta = risk_config.cvarBeta;
    result.cvar_lambda = risk_config.cvarLambda;
    result.objective_metric = solver::weighted_objective_metric_label(risk_config);
    result.branch_benders_enabled = true;
    const double root_user_cut_tolerance = effective_root_user_cut_tolerance(options);
    result.branch_benders_use_root_user_cuts = options.use_root_user_cuts;
    result.branch_benders_root_user_cut_max_rounds = options.root_user_cut_max_rounds;
    result.branch_benders_root_user_cut_tolerance = root_user_cut_tolerance;
    if (!options.use_root_user_cuts) {
        result.branch_benders_root_user_cut_skipped_reason = "Root user cuts disabled.";
    }
    result.branch_benders_root_user_cut_only_at_root_confirmed = true;
    result.combinatorial_benders_enabled = options.combinatorial_options.enabled;
    result.combinatorial_benders_lift_mode =
        to_string(options.combinatorial_options.lift_mode);
    result.combinatorial_benders_scenario_order =
        to_string(options.combinatorial_options.scenario_order);
    result.combinatorial_benders_cut_sampling_ratio =
        options.combinatorial_options.cut_sampling_ratio;
    result.combinatorial_benders_fractional_separation_enabled =
        options.combinatorial_options.separate_fractional;
        result.combinatorial_benders_initial_cuts_enabled =
            options.combinatorial_options.initial_cuts;
    if (options.combinatorial_options.enabled) {
        result.combinatorial_benders_mode =
            fpp_phase6c2a_combinatorial_mode(
                options.combinatorial_options.lift_mode);
    }

        const auto projected_options =
            projected_llbi_options_from_strengthening(options.strengthening_options);
        FppProjectedLlbiStats projected_stats;
        const auto projected_mode = active_projected_llbi_mode(projected_options);
        if (projected_mode != FppProjectedLlbiMode::None) {
            const auto projected_family = active_projected_llbi_family(projected_options);
            projected_stats.projected_coverage_llbi_enabled =
                projected_family == FppProjectedLlbiFamily::Coverage;
            projected_stats.projected_path_llbi_enabled =
                projected_family == FppProjectedLlbiFamily::Path;
            projected_stats.projected_llbi_family = to_string(projected_family);
            projected_stats.projected_llbi_strategy = to_string(projected_mode);
            projected_stats.projected_llbi_mode = projected_stats.projected_llbi_strategy;
            projected_stats.projected_llbi_root_rounds =
                projected_mode == FppProjectedLlbiMode::Exp ? projected_options.root_rounds : 0;
            if (projected_mode == FppProjectedLlbiMode::Poly) {
                projected_stats.projected_poly_enumeration_limit = projected_options.poly_max_cuts;
            }
        }

    const auto solve_start = std::chrono::steady_clock::now();
    IloEnv env;
    try {
        IloModel model(env);
        std::vector<int> y_position_by_node(static_cast<std::size_t>(opt.node_mapper.size()), -1);

        IloBoolVarArray y(env, static_cast<IloInt>(opt.eligible_indices.size()));
        for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
            const int compact_index = opt.eligible_indices[pos];
            y_position_by_node[static_cast<std::size_t>(compact_index)] = static_cast<int>(pos);
            std::ostringstream name;
            name << "y_" << compact_index;
            y[static_cast<IloInt>(pos)].setName(name.str().c_str());
        }

        IloNumVarArray eta(
            env,
            static_cast<IloInt>(opt.scenarios.size()),
            0.0,
            IloInfinity,
            ILOFLOAT);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            std::ostringstream name;
            name << "eta_s" << opt.scenarios[s].scenario_id;
            eta[static_cast<IloInt>(s)].setName(name.str().c_str());
        }

        IloNumVar risk_threshold;
        IloNumVarArray cvar_excess(env);
        int risk_constraint_count = 0;
        if (risk_enabled) {
            risk_threshold = IloNumVar(env, -IloInfinity, IloInfinity, ILOFLOAT);
            risk_threshold.setName("risk_threshold");
            cvar_excess = IloNumVarArray(
                env,
                static_cast<IloInt>(opt.scenarios.size()),
                0.0,
                IloInfinity,
                ILOFLOAT);
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                std::ostringstream name;
                name << "cvar_excess_s" << opt.scenarios[s].scenario_id;
                cvar_excess[static_cast<IloInt>(s)].setName(name.str().c_str());

                IloExpr excess_lhs(env);
                excess_lhs += cvar_excess[static_cast<IloInt>(s)];
                excess_lhs -= eta[static_cast<IloInt>(s)];
                excess_lhs += risk_threshold;
                IloRange excess_range = (excess_lhs >= 0.0);
                std::ostringstream constraint_name;
                constraint_name << "cvar_excess_link_s" << opt.scenarios[s].scenario_id;
                excess_range.setName(constraint_name.str().c_str());
                model.add(excess_range);
                excess_lhs.end();
                ++risk_constraint_count;
            }
        }

        IloExpr objective(env);
        const double cvar_tail_scale = 1.0 / (1.0 - risk_config.cvarBeta);
        if (risk_config.type == risk::RiskMeasureType::Expected) {
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                objective += opt.scenarios[s].probability * eta[static_cast<IloInt>(s)];
            }
        } else {
            const bool include_expected_term =
                risk_config.type == risk::RiskMeasureType::MeanCVaR;
            const double expected_weight = include_expected_term
                ? (1.0 - risk_config.cvarLambda)
                : 0.0;
            if (include_expected_term && expected_weight != 0.0) {
                for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                    objective +=
                        expected_weight *
                        opt.scenarios[s].probability *
                        eta[static_cast<IloInt>(s)];
                }
            }

            IloExpr cvar_tail(env);
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                cvar_tail +=
                    opt.scenarios[s].probability *
                    cvar_excess[static_cast<IloInt>(s)];
            }
            objective +=
                risk_config.cvarLambda * risk_threshold +
                risk_config.cvarLambda * cvar_tail_scale * cvar_tail;
            cvar_tail.end();
        }
        model.add(IloMinimize(env, objective));
        objective.end();

        IloExpr budget(env);
        for (IloInt pos = 0; pos < y.getSize(); ++pos) {
            budget += y[pos];
        }
        model.add(budget <= opt.budget);
        budget.end();

        int lifted_lower_bound_count = 0;
        if (options.use_lifted_lower_bounds) {
            const auto llb_result = build_fpp_lifted_lower_bounds(opt);
            for (std::size_t s = 0; s < llb_result.inequalities.size(); ++s) {
                const auto& inequality = llb_result.inequalities[s];
                IloExpr expr(env);
                expr += eta[static_cast<IloInt>(s)];
                for (const auto& [compact_index, coefficient] :
                     inequality.coefficients_by_compact_index) {
                    if (std::fabs(coefficient) <= 1.0e-12) {
                        continue;
                    }
                    const int y_pos = y_position_by_node[static_cast<std::size_t>(compact_index)];
                    if (y_pos < 0) {
                        expr.end();
                        throw std::runtime_error(
                            "FPP callback LLBI references a compact node without a master y variable.");
                    }
                    expr -= coefficient * y[static_cast<IloInt>(y_pos)];
                }
                model.add(expr >= inequality.rhs_constant);
                expr.end();
                ++lifted_lower_bound_count;

                solver::BendersLiftedLowerBoundRecord record;
                record.scenario_id = inequality.scenario_id;
                record.f_empty = inequality.f_empty;
                record.rhs_constant = inequality.rhs_constant;
                record.num_nonzero_coefficients = inequality.nonzero_coefficients;
                record.coefficients_by_compact_index = inequality.coefficients_by_compact_index;
                result.benders_lifted_lower_bounds.push_back(std::move(record));
            }
            result.benders_use_lifted_lower_bounds = true;
            result.benders_lifted_lower_bound_count = lifted_lower_bound_count;
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
            result.benders_lifted_lower_bound_constraints_added = lifted_lower_bound_count;
            result.benders_lifted_lower_bound_cache_hit = llb_result.cache_hit;
            result.benders_lifted_lower_bound_validity_mode = llb_result.validity_mode;
            result.benders_lifted_lower_bound_notes = llb_result.notes;
        }

        const auto coverage_llbi = build_fpp_coverage_llbi_data(
            opt,
            options.strengthening_options.use_coverage_llbi);
        const auto path_llbi = build_fpp_path_llbi_data(
            opt,
            options.strengthening_options.use_path_llbi,
            options.strengthening_options.path_llbi_max_paths_per_node);
        const double coverage_llbi_build_time_sec = add_coverage_llbi_constraints(
            env,
            model,
            coverage_llbi,
            y,
            eta,
            y_position_by_node);
        const double path_llbi_build_time_sec = add_path_llbi_constraints(
            env,
            model,
            path_llbi,
            y,
            eta,
            y_position_by_node);
        result.coverage_llbi_enabled = coverage_llbi.enabled;
        result.coverage_llbi_num_zeta_vars = coverage_llbi.num_zeta_vars;
        result.coverage_llbi_num_constraints = coverage_llbi.num_constraints;
        result.coverage_llbi_precompute_time_sec = coverage_llbi.precompute_time_sec;
        result.coverage_llbi_weighted = coverage_llbi.weighted;
        result.coverage_llbi_weight_map_hash = coverage_llbi.weight_map_hash;
        result.coverage_llbi_scenarios_precomputed = coverage_llbi.scenarios_precomputed;
        result.coverage_llbi_baseline_cells = coverage_llbi.baseline_cells;
        result.coverage_llbi_auxiliary_variables = coverage_llbi.auxiliary_variables;
        result.coverage_llbi_linking_constraints = coverage_llbi.linking_constraints;
        result.coverage_llbi_loss_constraints = coverage_llbi.loss_constraints;
        result.coverage_llbi_nonempty_coverage_sets = coverage_llbi.nonempty_coverage_sets;
        result.coverage_llbi_total_incidence_terms = coverage_llbi.total_incidence_terms;
        result.coverage_llbi_build_time_sec = coverage_llbi_build_time_sec;
        result.coverage_llbi_validity_mode = coverage_llbi.validity_mode;
        result.path_llbi_enabled = path_llbi.enabled;
        result.path_llbi_num_b_vars = path_llbi.num_b_vars;
        result.path_llbi_num_path_constraints = path_llbi.num_path_constraints;
        result.path_llbi_num_paths_used = path_llbi.num_paths_used;
        result.path_llbi_weighted = path_llbi.weighted;
        result.path_llbi_weight_map_hash = path_llbi.weight_map_hash;
        result.path_llbi_scenarios_precomputed = path_llbi.scenarios_precomputed;
        result.path_llbi_baseline_nodes = path_llbi.baseline_nodes;
        result.path_llbi_auxiliary_variables = path_llbi.auxiliary_variables;
        result.path_llbi_path_constraints = path_llbi.path_constraints;
        result.path_llbi_loss_constraints = path_llbi.loss_constraints;
        result.path_llbi_total_paths = path_llbi.total_paths;
        result.path_llbi_total_candidate_incidence_terms =
            path_llbi.total_candidate_incidence_terms;
        result.path_llbi_nodes_without_paths = path_llbi.nodes_without_paths;
        result.path_llbi_path_enumeration_complete =
            path_llbi.path_enumeration_complete;
        result.path_llbi_paths_truncated = path_llbi.paths_truncated;
        result.path_llbi_precompute_time_sec = path_llbi.precompute_time_sec;
        result.path_llbi_build_time_sec = path_llbi_build_time_sec;
        result.path_llbi_validity_mode = path_llbi.validity_mode;
        result.projected_coverage_llbi_enabled =
            projected_stats.projected_coverage_llbi_enabled;
        result.projected_path_llbi_enabled =
            projected_stats.projected_path_llbi_enabled;
        result.projected_llbi_family = projected_stats.projected_llbi_family;
        result.projected_llbi_strategy = projected_stats.projected_llbi_strategy;
        result.projected_llbi_mode = projected_stats.projected_llbi_mode;
        result.projected_llbi_root_rounds = projected_stats.projected_llbi_root_rounds;
        result.projected_poly_enumeration_limit =
            projected_stats.projected_poly_enumeration_limit;
        result.projected_exp_enumeration_limit =
            projected_stats.projected_exp_enumeration_limit;
        result.projected_coverage_llbi_weighted =
            projected_stats.projected_coverage_llbi_weighted;
        result.projected_coverage_llbi_mode =
            projected_stats.projected_coverage_llbi_mode;
        result.projected_coverage_llbi_weight_map_hash =
            projected_stats.projected_coverage_llbi_weight_map_hash;
        result.projected_coverage_llbi_scenarios_precomputed =
            projected_stats.projected_coverage_llbi_scenarios_precomputed;
        result.projected_coverage_llbi_baseline_cells =
            projected_stats.projected_coverage_llbi_baseline_cells;
        result.projected_coverage_llbi_nonempty_coverage_sets =
            projected_stats.projected_coverage_llbi_nonempty_coverage_sets;
        result.projected_coverage_llbi_total_incidence_terms =
            projected_stats.projected_coverage_llbi_total_incidence_terms;
        result.projected_coverage_llbi_precompute_time_sec =
            projected_stats.projected_coverage_llbi_precompute_time_sec;
        result.projected_coverage_llbi_validity_mode =
            projected_stats.projected_coverage_llbi_validity_mode;
        result.projected_path_llbi_weighted =
            projected_stats.projected_path_llbi_weighted;
        result.projected_path_llbi_mode =
            projected_stats.projected_path_llbi_mode;
        result.projected_path_llbi_weight_map_hash =
            projected_stats.projected_path_llbi_weight_map_hash;
        result.projected_path_llbi_scenarios_precomputed =
            projected_stats.projected_path_llbi_scenarios_precomputed;
        result.projected_path_llbi_destination_nodes =
            projected_stats.projected_path_llbi_destination_nodes;
        result.projected_path_llbi_total_paths =
            projected_stats.projected_path_llbi_total_paths;
        result.projected_path_llbi_total_incidence_terms =
            projected_stats.projected_path_llbi_total_incidence_terms;
        result.projected_path_llbi_nodes_without_paths =
            projected_stats.projected_path_llbi_nodes_without_paths;
        result.projected_path_llbi_enumeration_complete =
            projected_stats.projected_path_llbi_enumeration_complete;
        result.projected_path_llbi_paths_truncated =
            projected_stats.projected_path_llbi_paths_truncated;
        result.projected_path_llbi_precompute_time_sec =
            projected_stats.projected_path_llbi_precompute_time_sec;
        result.projected_path_llbi_validity_mode =
            projected_stats.projected_path_llbi_validity_mode;
        result.conditional_zero_benefit_enabled =
            options.strengthening_options.use_conditional_zero_benefit_fixing;
        if (options.strengthening_options.use_conditional_zero_benefit_fixing) {
            result.conditional_zero_benefit_structural_weight_safe = true;
            result.notes.push_back(
                "Conditional zero-benefit local fixing requested, but CPLEX generic callbacks in this solver do not safely expose node-local y upper-bound tightening; diagnostics are reported with zero applied local fixings.");
        }

        BranchBendersVariableAccess access;
        access.y = y;
        access.eta = eta;
        access.y_position_by_node = y_position_by_node;

        std::vector<FppProjectedLlbiSeparatedCut> projected_export_cuts;
        std::set<std::string> projected_cut_signatures;
        if (projected_mode == FppProjectedLlbiMode::Poly) {
            const auto projected_cuts =
                build_fpp_projected_llbi_poly_cuts(opt, projected_options, &projected_stats);
            for (const auto& cut : projected_cuts) {
                int scenario_position = -1;
                for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                    if (opt.scenarios[s].scenario_id == cut.scenario_id) {
                        scenario_position = static_cast<int>(s);
                        break;
                    }
                }
                if (scenario_position < 0) {
                    throw std::runtime_error(
                        "Projected LLBI poly cut references an unknown scenario id.");
                }
                const auto signature = cut_signature(cut);
                const auto [_, inserted] = projected_cut_signatures.insert(signature);
                if (!inserted) {
                    if (projected_stats.projected_coverage_llbi_enabled) {
                        ++projected_stats.projected_coverage_llbi_duplicate_cuts;
                    }
                    if (projected_stats.projected_path_llbi_enabled) {
                        ++projected_stats.projected_path_llbi_duplicate_cuts;
                    }
                    continue;
                }
                add_benders_cut_to_model(
                    env,
                    model,
                    cut,
                    access,
                    scenario_position,
                    "FPP projected LLBI poly cut");
                ++projected_stats.projected_llbi_cuts_added;
                if (projected_stats.projected_coverage_llbi_enabled) {
                    ++projected_stats.projected_llbi_coverage_cuts_added;
                }
                if (projected_stats.projected_path_llbi_enabled) {
                    ++projected_stats.projected_llbi_path_cuts_added;
                }
                const int nonzeros =
                    static_cast<int>(cut.coefficients_by_compact_index.size());
                projected_stats.projected_llbi_total_nonzeros += nonzeros;
                projected_stats.projected_llbi_max_nonzeros_per_cut =
                    std::max(projected_stats.projected_llbi_max_nonzeros_per_cut, nonzeros);
                FppProjectedLlbiSeparatedCut export_cut;
                export_cut.cut = cut;
                export_cut.scenario_index = scenario_position;
                export_cut.nonzeros = nonzeros;
                projected_export_cuts.push_back(std::move(export_cut));
            }
            if (projected_stats.projected_llbi_cuts_added > 0) {
                projected_stats.projected_llbi_avg_nonzeros_per_cut =
                    static_cast<double>(projected_stats.projected_llbi_total_nonzeros) /
                    static_cast<double>(projected_stats.projected_llbi_cuts_added);
            }
            if (projected_stats.projected_coverage_llbi_enabled) {
                projected_stats.projected_coverage_llbi_cuts_added =
                    projected_stats.projected_llbi_coverage_cuts_added;
            }
            if (projected_stats.projected_path_llbi_enabled) {
                projected_stats.projected_path_llbi_cuts_added =
                    projected_stats.projected_llbi_path_cuts_added;
            }
        }

        std::unique_ptr<FppCombinatorialBendersSeparator> combinatorial_separator;
        std::vector<int> combinatorial_initial_y;
        std::set<std::string> combinatorial_initial_cut_signatures;
        int combinatorial_initial_solutions_evaluated = 0;
        int combinatorial_initial_cuts_generated = 0;
        int combinatorial_initial_cuts_added = 0;
        int combinatorial_initial_duplicate_cuts = 0;
        double combinatorial_initial_cut_time_sec = 0.0;
        if (options.combinatorial_options.enabled) {
            combinatorial_separator =
                std::make_unique<FppCombinatorialBendersSeparator>(opt);
            if (options.combinatorial_options.initial_cuts) {
                const auto initial_start = std::chrono::steady_clock::now();
                combinatorial_initial_y = combinatorial_separator->greedyInitialSolution();
                ++combinatorial_initial_solutions_evaluated;
                const auto initial_cuts =
                    combinatorial_separator->initialCutsFromSolution(
                        combinatorial_initial_y,
                        options.combinatorial_options.lift_mode);
                combinatorial_initial_cuts_generated =
                    static_cast<int>(initial_cuts.size());
                for (std::size_t s = 0; s < initial_cuts.size(); ++s) {
                    const auto signature = combinatorial_cut_signature(
                        initial_cuts[s].cut,
                        combinatorial_separator->weightMapHash(),
                        opt.eligible_indices);
                    const auto [_, inserted] =
                        combinatorial_initial_cut_signatures.insert(signature);
                    if (!inserted) {
                        ++combinatorial_initial_duplicate_cuts;
                        continue;
                    }
                    add_benders_cut_to_model(
                        env,
                        model,
                        initial_cuts[s].cut,
                        access,
                        static_cast<int>(s),
                        "FPP combinatorial initial Benders cut");
                    ++combinatorial_initial_cuts_added;
                }
                combinatorial_initial_cut_time_sec =
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - initial_start).count();
            }
        }

        IloCplex cplex(model);
        apply_cplex_parameters(cplex, env, options);

        if (projected_mode == FppProjectedLlbiMode::Exp) {
            const auto projected_start = std::chrono::steady_clock::now();
            IloConversion y_relaxation(env, y, ILOFLOAT);
            model.add(y_relaxation);
            bool added_in_last_round = false;
            for (int round = 0; round < projected_options.root_rounds; ++round) {
                ++projected_stats.projected_exp_separation_rounds;
                const auto lp_start = std::chrono::steady_clock::now();
                const bool lp_solved = cplex.solve();
                projected_stats.projected_llbi_solve_time_sec +=
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - lp_start).count();
                if (!lp_solved) {
                    model.remove(y_relaxation);
                    y_relaxation.end();
                    throw std::runtime_error(
                        "Projected LLBI root LP separation failed before the MIP solve.");
                }
                if (!std::isfinite(projected_stats.projected_llbi_root_bound_initial)) {
                    projected_stats.projected_llbi_root_bound_initial = cplex.getObjValue();
                }
                projected_stats.projected_llbi_root_bound_final = cplex.getObjValue();

                std::vector<double> ybar(static_cast<std::size_t>(y.getSize()), 0.0);
                for (IloInt pos = 0; pos < y.getSize(); ++pos) {
                    const double value = cplex.getValue(y[pos]);
                    ybar[static_cast<std::size_t>(pos)] = std::max(0.0, std::min(1.0, value));
                }
                std::vector<double> eta_values(static_cast<std::size_t>(eta.getSize()), 0.0);
                for (IloInt s = 0; s < eta.getSize(); ++s) {
                    eta_values[static_cast<std::size_t>(s)] = cplex.getValue(eta[s]);
                }

                const auto separated = separate_fpp_projected_llbi_cuts(
                    opt,
                    projected_options,
                    ybar,
                    eta_values);
                projected_stats.projected_llbi_separation_time_sec +=
                    separated.separation_time_sec;
                if (projected_stats.projected_coverage_llbi_enabled) {
                    projected_stats.projected_coverage_llbi_weighted =
                        separated.projected_coverage_llbi_weighted;
                    projected_stats.projected_coverage_llbi_mode =
                        separated.projected_coverage_llbi_mode;
                    projected_stats.projected_coverage_llbi_weight_map_hash =
                        separated.projected_coverage_llbi_weight_map_hash;
                    projected_stats.projected_coverage_llbi_scenarios_precomputed =
                        separated.projected_coverage_llbi_scenarios_precomputed;
                    projected_stats.projected_coverage_llbi_baseline_cells =
                        separated.projected_coverage_llbi_baseline_cells;
                    projected_stats.projected_coverage_llbi_nonempty_coverage_sets =
                        separated.projected_coverage_llbi_nonempty_coverage_sets;
                    projected_stats.projected_coverage_llbi_total_incidence_terms =
                        separated.projected_coverage_llbi_total_incidence_terms;
                    projected_stats.projected_coverage_llbi_precompute_time_sec +=
                        separated.projected_coverage_llbi_precompute_time_sec;
                    projected_stats.projected_coverage_llbi_validity_mode =
                        separated.projected_coverage_llbi_validity_mode;
                    ++projected_stats.projected_coverage_llbi_separation_calls;
                    projected_stats.projected_coverage_llbi_separation_time_sec +=
                        separated.separation_time_sec;
                    projected_stats.projected_coverage_llbi_cuts_generated +=
                        separated.violated_cuts_found;
                    projected_stats.projected_coverage_llbi_max_violation =
                        std::max(
                            projected_stats.projected_coverage_llbi_max_violation,
                            separated.max_violation);
                }
                if (projected_stats.projected_path_llbi_enabled) {
                    projected_stats.projected_path_llbi_weighted =
                        separated.projected_path_llbi_weighted;
                    projected_stats.projected_path_llbi_mode =
                        separated.projected_path_llbi_mode;
                    projected_stats.projected_path_llbi_weight_map_hash =
                        separated.projected_path_llbi_weight_map_hash;
                    projected_stats.projected_path_llbi_scenarios_precomputed =
                        separated.projected_path_llbi_scenarios_precomputed;
                    projected_stats.projected_path_llbi_destination_nodes =
                        separated.projected_path_llbi_destination_nodes;
                    projected_stats.projected_path_llbi_total_paths =
                        separated.projected_path_llbi_total_paths;
                    projected_stats.projected_path_llbi_total_incidence_terms =
                        separated.projected_path_llbi_total_incidence_terms;
                    projected_stats.projected_path_llbi_nodes_without_paths =
                        separated.projected_path_llbi_nodes_without_paths;
                    projected_stats.projected_path_llbi_enumeration_complete =
                        separated.projected_path_llbi_enumeration_complete;
                    projected_stats.projected_path_llbi_paths_truncated =
                        separated.projected_path_llbi_paths_truncated;
                    projected_stats.projected_path_llbi_precompute_time_sec +=
                        separated.projected_path_llbi_precompute_time_sec;
                    projected_stats.projected_path_llbi_validity_mode =
                        separated.projected_path_llbi_validity_mode;
                    ++projected_stats.projected_path_llbi_separation_calls;
                    projected_stats.projected_path_llbi_separation_time_sec +=
                        separated.separation_time_sec;
                    projected_stats.projected_path_llbi_cuts_generated +=
                        separated.violated_cuts_found;
                    projected_stats.projected_path_llbi_max_violation =
                        std::max(
                            projected_stats.projected_path_llbi_max_violation,
                            separated.max_violation);
                }
                projected_stats.projected_llbi_violated_cuts_found +=
                    separated.violated_cuts_found;
                if (!std::isnan(separated.min_violation)) {
                    if (std::isnan(projected_stats.projected_llbi_min_violation)) {
                        projected_stats.projected_llbi_min_violation =
                            separated.min_violation;
                    } else {
                        projected_stats.projected_llbi_min_violation = std::min(
                            projected_stats.projected_llbi_min_violation,
                            separated.min_violation);
                    }
                }
                projected_stats.projected_llbi_max_violation = std::max(
                    projected_stats.projected_llbi_max_violation,
                    separated.max_violation);

                if (separated.cuts.empty()) {
                    added_in_last_round = false;
                    break;
                }

                for (const auto& separated_cut : separated.cuts) {
                    const auto signature = cut_signature(separated_cut.cut);
                    const auto [_, inserted] = projected_cut_signatures.insert(signature);
                    if (!inserted) {
                        if (projected_stats.projected_coverage_llbi_enabled) {
                            ++projected_stats.projected_coverage_llbi_duplicate_cuts;
                        }
                        if (projected_stats.projected_path_llbi_enabled) {
                            ++projected_stats.projected_path_llbi_duplicate_cuts;
                        }
                        continue;
                    }
                    add_benders_cut_to_model(
                        env,
                        model,
                        separated_cut.cut,
                        access,
                        separated_cut.scenario_index,
                        "FPP projected LLBI exp cut");
                    ++projected_stats.projected_llbi_cuts_added;
                    if (projected_stats.projected_coverage_llbi_enabled) {
                        ++projected_stats.projected_llbi_coverage_cuts_added;
                        ++projected_stats.projected_coverage_llbi_cuts_added;
                    }
                    if (projected_stats.projected_path_llbi_enabled) {
                        ++projected_stats.projected_llbi_path_cuts_added;
                        ++projected_stats.projected_path_llbi_cuts_added;
                    }
                    projected_stats.projected_llbi_total_nonzeros += separated_cut.nonzeros;
                    projected_stats.projected_llbi_max_nonzeros_per_cut = std::max(
                        projected_stats.projected_llbi_max_nonzeros_per_cut,
                        separated_cut.nonzeros);
                    projected_stats.projected_llbi_avg_violation += separated_cut.violation;
                    projected_export_cuts.push_back(separated_cut);
                }
                added_in_last_round = true;
            }
            projected_stats.projected_exp_separated_cuts_added =
                projected_stats.projected_llbi_cuts_added;
            if (added_in_last_round) {
                const auto lp_start = std::chrono::steady_clock::now();
                const bool lp_solved = cplex.solve();
                projected_stats.projected_llbi_solve_time_sec +=
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - lp_start).count();
                if (lp_solved) {
                    projected_stats.projected_llbi_root_bound_final = cplex.getObjValue();
                }
            }
            model.remove(y_relaxation);
            y_relaxation.end();
            projected_stats.projected_llbi_total_time_sec =
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - projected_start).count();
            if (projected_stats.projected_llbi_cuts_added > 0) {
                projected_stats.projected_llbi_avg_nonzeros_per_cut =
                    static_cast<double>(projected_stats.projected_llbi_total_nonzeros) /
                    static_cast<double>(projected_stats.projected_llbi_cuts_added);
                projected_stats.projected_llbi_avg_violation /=
                    static_cast<double>(projected_stats.projected_llbi_cuts_added);
            }
            if (std::isfinite(projected_stats.projected_llbi_root_bound_initial) &&
                std::isfinite(projected_stats.projected_llbi_root_bound_final)) {
                projected_stats.projected_llbi_root_bound_improvement_abs =
                    projected_stats.projected_llbi_root_bound_final -
                    projected_stats.projected_llbi_root_bound_initial;
                const double denom =
                    std::max(1.0, std::fabs(projected_stats.projected_llbi_root_bound_initial));
                projected_stats.projected_llbi_root_bound_improvement_pct =
                    100.0 *
                    projected_stats.projected_llbi_root_bound_improvement_abs /
                    denom;
            }
        }

        if (projected_mode != FppProjectedLlbiMode::None) {
            export_fpp_projected_llbi_cuts(
                projected_options.export_cuts_path,
                projected_export_cuts);
        }

        if (options.combinatorial_options.enabled &&
            options.combinatorial_options.initial_cuts &&
            !combinatorial_initial_y.empty()) {
            IloNumVarArray start_vars(env);
            IloNumArray start_values(env);
            for (IloInt pos = 0; pos < y.getSize(); ++pos) {
                start_vars.add(y[pos]);
                start_values.add(combinatorial_initial_y[static_cast<std::size_t>(pos)]);
            }
            cplex.addMIPStart(start_vars, start_values);
            start_vars.end();
            start_values.end();
            result.warm_start_used = true;
            result.mip_start_accepted = true;
            result.warm_start_source = "fpp-combinatorial-greedy-initial-solution";
        }

        std::unique_ptr<FppPersistentScenarioSubproblemManager> subproblem_manager;
        if (!options.combinatorial_options.enabled) {
            subproblem_manager =
                std::make_unique<FppPersistentScenarioSubproblemManager>(opt, options.verbose);
        }
        BranchBendersCallbackStats callback_stats;
        callback_stats.combinatorial_stats.enabled = options.combinatorial_options.enabled;
        callback_stats.combinatorial_stats.lift_mode =
            to_string(options.combinatorial_options.lift_mode);
        callback_stats.combinatorial_stats.scenario_order =
            to_string(options.combinatorial_options.scenario_order);
        callback_stats.combinatorial_stats.cut_sampling_ratio =
            options.combinatorial_options.cut_sampling_ratio;
        callback_stats.combinatorial_stats.realized_sample_size =
            options.combinatorial_options.enabled
                ? fpp_combinatorial_realized_sample_size(
                      opt.scenarios.size(),
                      options.combinatorial_options.cut_sampling_ratio)
                : 0;
        callback_stats.combinatorial_stats.sampling_exact_fallback =
            options.combinatorial_options.enabled &&
            options.combinatorial_options.cut_sampling_ratio < 1.0 - 1.0e-12;
        callback_stats.combinatorial_stats.scenario_policy_exact = true;
        callback_stats.combinatorial_stats.scenario_policy_heuristic = false;
        callback_stats.combinatorial_stats.full_verification_before_acceptance = true;
        callback_stats.combinatorial_stats.fractional_separation_enabled =
            options.combinatorial_options.separate_fractional;
        callback_stats.combinatorial_stats.initial_cuts_enabled =
            options.combinatorial_options.initial_cuts;
        callback_stats.combinatorial_stats.initial_solutions_evaluated =
            combinatorial_initial_solutions_evaluated;
        callback_stats.combinatorial_stats.initial_cuts_generated =
            combinatorial_initial_cuts_generated;
        callback_stats.combinatorial_stats.initial_cuts_added =
            combinatorial_initial_cuts_added;
        callback_stats.combinatorial_stats.initial_duplicate_cuts =
            combinatorial_initial_duplicate_cuts;
        callback_stats.combinatorial_stats.initial_cut_time_sec =
            combinatorial_initial_cut_time_sec;
        callback_stats.cut_signatures.insert(
            combinatorial_initial_cut_signatures.begin(),
            combinatorial_initial_cut_signatures.end());
        if (combinatorial_separator) {
            callback_stats.combinatorial_stats.weighted =
                combinatorial_separator->weighted();
            callback_stats.combinatorial_stats.weight_map_hash =
                combinatorial_separator->weightMapHash();
            callback_stats.combinatorial_stats.mode =
                fpp_phase6c2a_combinatorial_mode(
                    options.combinatorial_options.lift_mode);
            callback_stats.combinatorial_stats.validity_mode =
                combinatorial_separator->validityMode();
            callback_stats.combinatorial_stats.lifting_weighted =
                combinatorial_separator->weighted();
            callback_stats.combinatorial_stats.lifting_mode =
                to_string(options.combinatorial_options.lift_mode);
            callback_stats.combinatorial_stats.lifting_weight_map_hash =
                combinatorial_separator->weightMapHash();
            callback_stats.combinatorial_stats.lifting_validity_mode =
                fpp_phase6c2a_lifting_validity_mode(
                    options.combinatorial_options.lift_mode,
                    combinatorial_separator->weighted());
            callback_stats.combinatorial_stats.fractional_validity_mode =
                options.combinatorial_options.separate_fractional
                    ? fpp_fractional_combinatorial_validity_mode(
                          combinatorial_separator->weighted())
                    : "disabled";
            callback_stats.combinatorial_stats.root_cuts_enabled = false;
            callback_stats.combinatorial_stats.root_skipped_reason =
                "No dedicated combinatorial root-only cut mechanism is implemented; LP-dual root user cuts remain separate.";
        }
        BranchBendersRootUserCutStats root_user_stats;
        root_user_stats.enabled = options.use_root_user_cuts;
        root_user_stats.max_rounds = options.root_user_cut_max_rounds;
        root_user_stats.tolerance = root_user_cut_tolerance;

        std::unique_ptr<FppBranchBendersCandidateCallback> candidate_callback;
        std::unique_ptr<FppBranchBendersCombinedCallback> combined_callback;
        std::unique_ptr<FppCombinatorialBranchBendersCallback> combinatorial_callback;
        if (options.combinatorial_options.enabled) {
            if (!combinatorial_separator) {
                throw std::runtime_error("FPP combinatorial separator was not initialized.");
            }
            combinatorial_callback = std::make_unique<FppCombinatorialBranchBendersCallback>(
                opt,
                *combinatorial_separator,
                options.combinatorial_options,
                access,
                options.tolerance,
                callback_stats);
            CPXLONG context_mask =
                IloCplex::Callback::Context::Id::Candidate;
            if (options.combinatorial_options.separate_fractional) {
                context_mask = context_mask | IloCplex::Callback::Context::Id::Relaxation;
            }
            cplex.use(combinatorial_callback.get(), context_mask);
            if (options.use_root_user_cuts) {
                root_user_stats.skipped_reason =
                    "LP root user cuts disabled because combinatorial fractional separation is controlled by combinatorial_benders_fractional_separation_enabled.";
            }
        } else if (options.use_root_user_cuts) {
            combined_callback = std::make_unique<FppBranchBendersCombinedCallback>(
                opt,
                *subproblem_manager,
                access,
                options.tolerance,
                options.root_user_cut_max_rounds,
                root_user_cut_tolerance,
                options.verbose,
                callback_stats,
                root_user_stats);
            cplex.use(
                combined_callback.get(),
                IloCplex::Callback::Context::Id::Candidate |
                    IloCplex::Callback::Context::Id::Relaxation);
        } else {
            candidate_callback = std::make_unique<FppBranchBendersCandidateCallback>(
                opt,
                *subproblem_manager,
                access,
                options.tolerance,
                options.verbose,
                callback_stats);
            cplex.use(candidate_callback.get(), IloCplex::Callback::Context::Id::Candidate);
        }

        const bool solved = cplex.solve();
        const auto solve_end = std::chrono::steady_clock::now();
        result.runtime_seconds = std::chrono::duration<double>(solve_end - solve_start).count();

        std::ostringstream status;
        status << cplex.getStatus();
        result.status = solved ? status.str() : "No feasible Branch-Benders solution";
        result.solver_status_code = static_cast<int>(cplex.getCplexStatus());
        if (!solved) {
            env.end();
            return result;
        }

        result.best_bound = cplex.getBestObjValue();
        result.mip_gap = cplex.getMIPRelativeGap();
        result.explored_nodes = static_cast<long long>(cplex.getNnodes());

        std::vector<int> y_values;
        y_values.reserve(static_cast<std::size_t>(y.getSize()));
        for (IloInt pos = 0; pos < y.getSize(); ++pos) {
            y_values.push_back(cplex.getValue(y[pos]) > 0.5 ? 1 : 0);
        }
        std::vector<double> eta_values;
        eta_values.reserve(static_cast<std::size_t>(eta.getSize()));
        for (IloInt s = 0; s < eta.getSize(); ++s) {
            eta_values.push_back(cplex.getValue(eta[s]));
        }

        const auto compact_y = expand_y_to_compact_values(opt, y_values);
        double final_max_violation = 0.0;
        double verification_subproblem_time = 0.0;
        double verification_max_subproblem_time = 0.0;
        std::vector<double> scenario_recourse_values;
        scenario_recourse_values.reserve(opt.scenarios.size());
        if (options.combinatorial_options.enabled) {
            if (!combinatorial_separator) {
                throw std::runtime_error("FPP combinatorial separator was not initialized.");
            }
            scenario_recourse_values =
                combinatorial_separator->evaluateScenarioLosses(y_values);
            std::vector<double> y_values_double;
            y_values_double.reserve(y_values.size());
            for (const int value : y_values) {
                y_values_double.push_back(static_cast<double>(value));
            }
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                const double eta_value = eta_values[s];
                const double direct_violation = scenario_recourse_values[s] - eta_value;
                const auto separated = combinatorial_separator->separateScenario(
                    static_cast<int>(s),
                    y_values_double,
                    eta_value,
                    false,
                    options.combinatorial_options.lift_mode,
                    options.tolerance);
                final_max_violation = std::max(
                    final_max_violation,
                    std::max(direct_violation, separated.violation));
            }
        } else {
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                const auto sub_result =
                    subproblem_manager->solveScenario(static_cast<int>(s), y_values);
                scenario_recourse_values.push_back(sub_result.objective_value);
                verification_subproblem_time += sub_result.runtime_seconds;
                verification_max_subproblem_time =
                    std::max(verification_max_subproblem_time, sub_result.runtime_seconds);
                const double eta_value = eta_values[s];
                const double direct_violation = sub_result.objective_value - eta_value;
                const double cut_violation =
                    sub_result.benders_cut.violationAt(eta_value, compact_y);
                final_max_violation =
                    std::max(final_max_violation, std::max(direct_violation, cut_violation));
            }
        }
        if (final_max_violation < options.tolerance) {
            final_max_violation = std::max(0.0, final_max_violation);
        }
        const auto risk_evaluation = evaluate_risk_objective(
            opt,
            scenario_recourse_values,
            risk_config);

        result.objective_value = risk_evaluation.objective;
        result.solver_weighted_objective = result.objective_value;
        result.expected_loss_component = risk_evaluation.expected;
        if (risk_enabled) {
            result.cvar_loss_component = risk_evaluation.cvar;
            result.risk_threshold_value = cplex.getValue(risk_threshold);
        }
        result.selected_firebreak_indices = selected_compact_indices(opt, y_values);
        result.selected_firebreak_original_nodes = selected_original_nodes(opt, y_values);
        result.iterations = static_cast<int>(cplex.getNiterations());
        result.cuts_added =
            callback_stats.lazy_cuts_added +
            callback_stats.combinatorial_stats.fractional_cuts_added +
            callback_stats.combinatorial_stats.initial_cuts_added +
            projected_stats.projected_llbi_cuts_added;
        result.max_cut_violation = final_max_violation;
        result.branch_benders_callback_calls = callback_stats.callback_calls;
        result.branch_benders_candidate_callback_calls =
            callback_stats.candidate_callback_calls;
        result.branch_benders_incumbent_callback_calls =
            callback_stats.incumbent_callback_calls;
        result.branch_benders_candidate_incumbents_checked =
            callback_stats.candidate_incumbents_checked;
        result.branch_benders_subproblems_attempted =
            callback_stats.subproblems_attempted +
            (options.combinatorial_options.enabled ? 0 : static_cast<int>(opt.scenarios.size())) +
            root_user_stats.scenarios_solved;
        result.branch_benders_subproblems_solved =
            callback_stats.subproblems_solved +
            (options.combinatorial_options.enabled ? 0 : static_cast<int>(opt.scenarios.size())) +
            root_user_stats.scenarios_solved;
        result.branch_benders_lazy_cuts_added = callback_stats.lazy_cuts_added;
        result.branch_benders_max_cut_violation = final_max_violation;
        result.branch_benders_largest_incumbent_cut_violation =
            callback_stats.largest_incumbent_cut_violation;
        result.branch_benders_callback_time_sec = callback_stats.callback_time_sec;
        result.branch_benders_subproblem_time_sec =
            callback_stats.subproblem_time_sec +
            verification_subproblem_time +
            root_user_stats.subproblem_time_sec;
        result.branch_benders_average_subproblem_time_sec =
            result.branch_benders_subproblems_solved > 0
                ? result.branch_benders_subproblem_time_sec /
                    static_cast<double>(result.branch_benders_subproblems_solved)
                : 0.0;
        result.branch_benders_max_subproblem_time_sec = std::max(
            std::max(callback_stats.max_subproblem_time_sec, verification_max_subproblem_time),
            root_user_stats.max_subproblem_time_sec);
        result.branch_benders_cut_construction_time_sec =
            callback_stats.cut_construction_time_sec;
        result.branch_benders_lazy_cut_insertion_time_sec =
            callback_stats.lazy_cut_insertion_time_sec;
        result.branch_benders_violated_cuts = callback_stats.violated_cuts;
        result.branch_benders_nonviolated_cuts = callback_stats.nonviolated_cuts;
        result.branch_benders_skipped_cuts = callback_stats.skipped_cuts;
        result.branch_benders_duplicate_cuts = callback_stats.duplicate_cuts;
        result.branch_benders_incumbent_log = callback_stats.incumbent_log;
        result.combinatorial_benders_enabled =
            callback_stats.combinatorial_stats.enabled;
        result.combinatorial_benders_lift_mode =
            callback_stats.combinatorial_stats.lift_mode;
        result.combinatorial_benders_scenario_order =
            callback_stats.combinatorial_stats.scenario_order;
        result.combinatorial_benders_cut_sampling_ratio =
            callback_stats.combinatorial_stats.cut_sampling_ratio;
        result.combinatorial_benders_fractional_separation_enabled =
            callback_stats.combinatorial_stats.fractional_separation_enabled;
        result.combinatorial_benders_initial_cuts_enabled =
            callback_stats.combinatorial_stats.initial_cuts_enabled;
        result.combinatorial_benders_integer_cuts_added =
            callback_stats.combinatorial_stats.integer_cuts_added;
        result.combinatorial_benders_fractional_cuts_added =
            callback_stats.combinatorial_stats.fractional_cuts_added;
        result.combinatorial_benders_initial_cuts_added =
            callback_stats.combinatorial_stats.initial_cuts_added;
        result.combinatorial_benders_scenarios_checked =
            callback_stats.combinatorial_stats.scenarios_checked;
        result.combinatorial_benders_separation_time_sec =
            callback_stats.combinatorial_stats.separation_time_sec;
        result.combinatorial_benders_avg_paths_per_cut =
            callback_stats.combinatorial_stats.average_paths_per_cut();
        result.combinatorial_benders_avg_cut_nonzeros =
            callback_stats.combinatorial_stats.average_cut_nonzeros();
        result.combinatorial_benders_num_violated_cuts =
            callback_stats.combinatorial_stats.num_violated_cuts;
        result.combinatorial_benders_weighted =
            callback_stats.combinatorial_stats.weighted;
        result.combinatorial_benders_mode =
            callback_stats.combinatorial_stats.mode;
        result.combinatorial_benders_weight_map_hash =
            callback_stats.combinatorial_stats.weight_map_hash;
        result.combinatorial_benders_weighted_recourse_evaluations =
            callback_stats.combinatorial_stats.weighted_recourse_evaluations;
        result.combinatorial_benders_duplicate_cuts =
            callback_stats.combinatorial_stats.duplicate_cuts;
        result.combinatorial_benders_cuts_tight_at_incumbent =
            callback_stats.combinatorial_stats.tight_cuts;
        result.combinatorial_benders_lifting_enabled =
            options.combinatorial_options.enabled &&
            options.combinatorial_options.lift_mode !=
                FppCombinatorialBendersLiftMode::None;
        result.combinatorial_benders_scenario_sampling_enabled =
            options.combinatorial_options.enabled &&
            options.combinatorial_options.cut_sampling_ratio < 1.0 - 1.0e-12;
        result.combinatorial_benders_max_tightness_error =
            callback_stats.combinatorial_stats.max_tightness_error;
        result.combinatorial_benders_max_violation =
            callback_stats.combinatorial_stats.max_violation;
        result.combinatorial_benders_propagation_time_sec =
            callback_stats.combinatorial_stats.propagation_time_sec;
        result.combinatorial_benders_cut_build_time_sec =
            callback_stats.combinatorial_stats.cut_build_time_sec;
        result.combinatorial_benders_validity_mode =
            callback_stats.combinatorial_stats.validity_mode;
        result.combinatorial_weighted = result.combinatorial_benders_weighted;
        result.combinatorial_mode = result.combinatorial_benders_mode;
        result.combinatorial_weight_map_hash =
            result.combinatorial_benders_weight_map_hash;
        result.combinatorial_scenario_order =
            result.combinatorial_benders_scenario_order;
        result.combinatorial_cut_sampling_ratio =
            result.combinatorial_benders_cut_sampling_ratio;
        result.combinatorial_candidate_callbacks =
            result.branch_benders_candidate_callback_calls;
        result.combinatorial_scenarios_evaluated =
            result.combinatorial_benders_scenarios_checked;
        result.combinatorial_weighted_recourse_evaluations =
            result.combinatorial_benders_weighted_recourse_evaluations;
        result.combinatorial_cuts_generated =
            result.combinatorial_benders_num_violated_cuts;
        result.combinatorial_cuts_added =
            result.combinatorial_benders_integer_cuts_added +
            result.combinatorial_benders_fractional_cuts_added +
            result.combinatorial_benders_initial_cuts_added;
        result.combinatorial_duplicate_cuts =
            result.combinatorial_benders_duplicate_cuts;
        result.combinatorial_cuts_tight_at_incumbent =
            result.combinatorial_benders_cuts_tight_at_incumbent;
        result.combinatorial_max_tightness_error =
            result.combinatorial_benders_max_tightness_error;
        result.combinatorial_max_violation =
            result.combinatorial_benders_max_violation;
        result.combinatorial_propagation_time_sec =
            result.combinatorial_benders_propagation_time_sec;
        result.combinatorial_cut_build_time_sec =
            result.combinatorial_benders_cut_build_time_sec;
        result.combinatorial_callback_time_sec =
            result.branch_benders_callback_time_sec;
        result.combinatorial_validity_mode =
            result.combinatorial_benders_validity_mode;
        result.combinatorial_lifting_enabled =
            result.combinatorial_benders_lifting_enabled;
        result.combinatorial_fractional_cuts_enabled =
            result.combinatorial_benders_fractional_separation_enabled;
        result.combinatorial_initial_cuts_enabled =
            result.combinatorial_benders_initial_cuts_enabled;
        result.combinatorial_scenario_sampling_enabled =
            result.combinatorial_benders_scenario_sampling_enabled;
        result.combinatorial_lifting_weighted =
            callback_stats.combinatorial_stats.lifting_weighted;
        result.combinatorial_lifting_mode =
            callback_stats.combinatorial_stats.lifting_mode;
        result.combinatorial_lifting_weight_map_hash =
            callback_stats.combinatorial_stats.lifting_weight_map_hash;
        result.combinatorial_lifting_attempts =
            callback_stats.combinatorial_stats.lifting_attempts;
        result.combinatorial_lifting_successes =
            callback_stats.combinatorial_stats.lifting_successes;
        result.combinatorial_lifting_failures =
            callback_stats.combinatorial_stats.lifting_failures;
        result.combinatorial_candidates_considered_for_lifting =
            callback_stats.combinatorial_stats.candidates_considered_for_lifting;
        result.combinatorial_coefficients_changed =
            callback_stats.combinatorial_stats.coefficients_changed_by_lifting;
        result.combinatorial_propagation_evaluations_for_lifting =
            callback_stats.combinatorial_stats.propagation_evaluations_for_lifting;
        result.combinatorial_baseline_cut_nonzeros =
            callback_stats.combinatorial_stats.baseline_cut_nonzeros;
        result.combinatorial_lifted_cut_nonzeros =
            callback_stats.combinatorial_stats.lifted_cut_nonzeros;
        result.combinatorial_max_coefficient_change =
            callback_stats.combinatorial_stats.max_coefficient_change;
        result.combinatorial_max_baseline_tightness_error =
            callback_stats.combinatorial_stats.max_baseline_tightness_error;
        result.combinatorial_max_lifted_tightness_error =
            callback_stats.combinatorial_stats.max_lifted_tightness_error;
        result.combinatorial_lifted_cuts_dominating_baseline =
            callback_stats.combinatorial_stats.lifted_cuts_dominating_baseline;
        result.combinatorial_lifting_time_sec =
            callback_stats.combinatorial_stats.lifting_time_sec;
        result.combinatorial_lifting_validity_mode =
            callback_stats.combinatorial_stats.lifting_validity_mode;
        result.combinatorial_initial_solutions_evaluated =
            callback_stats.combinatorial_stats.initial_solutions_evaluated;
        result.combinatorial_initial_cuts_generated =
            callback_stats.combinatorial_stats.initial_cuts_generated;
        result.combinatorial_initial_duplicate_cuts =
            callback_stats.combinatorial_stats.initial_duplicate_cuts;
        result.combinatorial_initial_cut_time_sec =
            callback_stats.combinatorial_stats.initial_cut_time_sec;
        result.combinatorial_root_cuts_enabled =
            callback_stats.combinatorial_stats.root_cuts_enabled;
        result.combinatorial_root_rounds =
            callback_stats.combinatorial_stats.root_rounds;
        result.combinatorial_root_integer_points_evaluated =
            callback_stats.combinatorial_stats.root_integer_points_evaluated;
        result.combinatorial_root_fractional_points_evaluated =
            callback_stats.combinatorial_stats.root_fractional_points_evaluated;
        result.combinatorial_root_cuts_generated =
            callback_stats.combinatorial_stats.root_cuts_generated;
        result.combinatorial_root_cuts_added =
            callback_stats.combinatorial_stats.root_cuts_added;
        result.combinatorial_root_duplicate_cuts =
            callback_stats.combinatorial_stats.root_duplicate_cuts;
        result.combinatorial_root_cut_time_sec =
            callback_stats.combinatorial_stats.root_cut_time_sec;
        result.combinatorial_root_skipped_reason =
            callback_stats.combinatorial_stats.root_skipped_reason;
        result.combinatorial_fractional_validity_mode =
            callback_stats.combinatorial_stats.fractional_validity_mode;
        result.combinatorial_fractional_separation_calls =
            callback_stats.combinatorial_stats.fractional_separation_calls;
        result.combinatorial_fractional_scenarios_evaluated =
            callback_stats.combinatorial_stats.fractional_scenarios_evaluated;
        result.combinatorial_fractional_cuts_generated =
            callback_stats.combinatorial_stats.fractional_cuts_generated;
        result.combinatorial_fractional_duplicate_cuts =
            callback_stats.combinatorial_stats.fractional_duplicate_cuts;
        result.combinatorial_fractional_max_violation =
            callback_stats.combinatorial_stats.fractional_max_violation;
        result.combinatorial_fractional_max_tightness_error =
            callback_stats.combinatorial_stats.fractional_max_tightness_error;
        result.combinatorial_fractional_separation_time_sec =
            callback_stats.combinatorial_stats.fractional_separation_time_sec;
        result.combinatorial_realized_sample_size =
            callback_stats.combinatorial_stats.realized_sample_size;
        result.combinatorial_sampling_exact_fallback =
            callback_stats.combinatorial_stats.sampling_exact_fallback;
        result.combinatorial_scenario_policy_exact =
            callback_stats.combinatorial_stats.scenario_policy_exact;
        result.combinatorial_scenario_policy_heuristic =
            callback_stats.combinatorial_stats.scenario_policy_heuristic;
        result.combinatorial_full_verification_before_acceptance =
            callback_stats.combinatorial_stats.full_verification_before_acceptance;
        result.combinatorial_candidate_initial_sample_scenarios_evaluated =
            callback_stats.combinatorial_stats
                .candidate_initial_sample_scenarios_evaluated;
        result.combinatorial_candidate_fallback_scenarios_evaluated =
            callback_stats.combinatorial_stats
                .candidate_fallback_scenarios_evaluated;
        result.combinatorial_candidate_full_sweeps =
            callback_stats.combinatorial_stats.candidate_full_sweeps;
        result.combinatorial_candidates_rejected_in_initial_sample =
            callback_stats.combinatorial_stats.candidates_rejected_in_initial_sample;
        result.combinatorial_candidates_rejected_in_fallback =
            callback_stats.combinatorial_stats.candidates_rejected_in_fallback;
        result.combinatorial_candidates_fully_verified =
            callback_stats.combinatorial_stats.candidates_fully_verified;
        result.combinatorial_sampled_violations =
            callback_stats.combinatorial_stats.sampled_violations;
        result.combinatorial_fallback_violations =
            callback_stats.combinatorial_stats.fallback_violations;
        result.combinatorial_scenarios_skipped_after_candidate_rejection =
            callback_stats.combinatorial_stats
                .scenarios_skipped_after_candidate_rejection;
        result.combinatorial_sampling_time_sec =
            callback_stats.combinatorial_stats.sampling_time_sec;
        result.combinatorial_ordering_time_sec =
            callback_stats.combinatorial_stats.ordering_time_sec;
        result.projected_coverage_llbi_enabled =
            projected_stats.projected_coverage_llbi_enabled;
        result.projected_path_llbi_enabled =
            projected_stats.projected_path_llbi_enabled;
        result.projected_llbi_family =
            projected_stats.projected_llbi_family;
        result.projected_llbi_strategy =
            projected_stats.projected_llbi_strategy;
        result.projected_llbi_mode = projected_stats.projected_llbi_mode;
        result.projected_llbi_root_rounds =
            projected_stats.projected_llbi_root_rounds;
        result.projected_llbi_cuts_added =
            projected_stats.projected_llbi_cuts_added;
        result.projected_llbi_coverage_cuts_added =
            projected_stats.projected_llbi_coverage_cuts_added;
        result.projected_llbi_path_cuts_added =
            projected_stats.projected_llbi_path_cuts_added;
        result.projected_llbi_violated_cuts_found =
            projected_stats.projected_llbi_violated_cuts_found;
        result.projected_llbi_separation_time_sec =
            projected_stats.projected_llbi_separation_time_sec;
        result.projected_llbi_solve_time_sec =
            projected_stats.projected_llbi_solve_time_sec;
        result.projected_llbi_total_time_sec =
            projected_stats.projected_llbi_total_time_sec;
        result.projected_llbi_total_nonzeros =
            projected_stats.projected_llbi_total_nonzeros;
        result.projected_llbi_avg_nonzeros_per_cut =
            projected_stats.projected_llbi_avg_nonzeros_per_cut;
        result.projected_llbi_max_nonzeros_per_cut =
            projected_stats.projected_llbi_max_nonzeros_per_cut;
        result.projected_llbi_min_violation =
            projected_stats.projected_llbi_min_violation;
        result.projected_llbi_max_violation =
            projected_stats.projected_llbi_max_violation;
        result.projected_llbi_avg_violation =
            projected_stats.projected_llbi_avg_violation;
        result.projected_llbi_root_bound_initial =
            projected_stats.projected_llbi_root_bound_initial;
        result.projected_llbi_root_bound_final =
            projected_stats.projected_llbi_root_bound_final;
        result.projected_llbi_root_bound_improvement_abs =
            projected_stats.projected_llbi_root_bound_improvement_abs;
        result.projected_llbi_root_bound_improvement_pct =
            projected_stats.projected_llbi_root_bound_improvement_pct;
        result.projected_poly_candidate_cuts_generated =
            projected_stats.projected_poly_candidate_cuts_generated;
        result.projected_poly_candidate_cuts_added =
            projected_stats.projected_poly_candidate_cuts_added;
        result.projected_poly_enumeration_truncated =
            projected_stats.projected_poly_enumeration_truncated;
        result.projected_poly_enumeration_limit =
            projected_stats.projected_poly_enumeration_limit;
        result.projected_exp_separated_cuts_added =
            projected_stats.projected_exp_separated_cuts_added;
        result.projected_exp_separation_rounds =
            projected_stats.projected_exp_separation_rounds;
        result.projected_exp_candidate_cuts_generated =
            projected_stats.projected_exp_candidate_cuts_generated;
        result.projected_exp_candidate_cuts_added =
            projected_stats.projected_exp_candidate_cuts_added;
        result.projected_exp_enumeration_truncated =
            projected_stats.projected_exp_enumeration_truncated;
        result.projected_exp_enumeration_limit =
            projected_stats.projected_exp_enumeration_limit;
        result.projected_coverage_llbi_weighted =
            projected_stats.projected_coverage_llbi_weighted;
        result.projected_coverage_llbi_mode =
            projected_stats.projected_coverage_llbi_mode;
        result.projected_coverage_llbi_weight_map_hash =
            projected_stats.projected_coverage_llbi_weight_map_hash;
        result.projected_coverage_llbi_scenarios_precomputed =
            projected_stats.projected_coverage_llbi_scenarios_precomputed;
        result.projected_coverage_llbi_baseline_cells =
            projected_stats.projected_coverage_llbi_baseline_cells;
        result.projected_coverage_llbi_nonempty_coverage_sets =
            projected_stats.projected_coverage_llbi_nonempty_coverage_sets;
        result.projected_coverage_llbi_total_incidence_terms =
            projected_stats.projected_coverage_llbi_total_incidence_terms;
        result.projected_coverage_llbi_separation_calls =
            projected_stats.projected_coverage_llbi_separation_calls;
        result.projected_coverage_llbi_cuts_generated =
            projected_stats.projected_coverage_llbi_cuts_generated;
        result.projected_coverage_llbi_cuts_added =
            projected_stats.projected_coverage_llbi_cuts_added;
        result.projected_coverage_llbi_duplicate_cuts =
            projected_stats.projected_coverage_llbi_duplicate_cuts;
        result.projected_coverage_llbi_max_violation =
            projected_stats.projected_coverage_llbi_max_violation;
        result.projected_coverage_llbi_precompute_time_sec =
            projected_stats.projected_coverage_llbi_precompute_time_sec;
        result.projected_coverage_llbi_separation_time_sec =
            projected_stats.projected_coverage_llbi_separation_time_sec;
        result.projected_coverage_llbi_validity_mode =
            projected_stats.projected_coverage_llbi_validity_mode;
        result.projected_path_llbi_weighted =
            projected_stats.projected_path_llbi_weighted;
        result.projected_path_llbi_mode =
            projected_stats.projected_path_llbi_mode;
        result.projected_path_llbi_weight_map_hash =
            projected_stats.projected_path_llbi_weight_map_hash;
        result.projected_path_llbi_scenarios_precomputed =
            projected_stats.projected_path_llbi_scenarios_precomputed;
        result.projected_path_llbi_destination_nodes =
            projected_stats.projected_path_llbi_destination_nodes;
        result.projected_path_llbi_total_paths =
            projected_stats.projected_path_llbi_total_paths;
        result.projected_path_llbi_total_incidence_terms =
            projected_stats.projected_path_llbi_total_incidence_terms;
        result.projected_path_llbi_nodes_without_paths =
            projected_stats.projected_path_llbi_nodes_without_paths;
        result.projected_path_llbi_enumeration_complete =
            projected_stats.projected_path_llbi_enumeration_complete;
        result.projected_path_llbi_paths_truncated =
            projected_stats.projected_path_llbi_paths_truncated;
        result.projected_path_llbi_separation_calls =
            projected_stats.projected_path_llbi_separation_calls;
        result.projected_path_llbi_cuts_generated =
            projected_stats.projected_path_llbi_cuts_generated;
        result.projected_path_llbi_cuts_added =
            projected_stats.projected_path_llbi_cuts_added;
        result.projected_path_llbi_duplicate_cuts =
            projected_stats.projected_path_llbi_duplicate_cuts;
        result.projected_path_llbi_max_violation =
            projected_stats.projected_path_llbi_max_violation;
        result.projected_path_llbi_precompute_time_sec =
            projected_stats.projected_path_llbi_precompute_time_sec;
        result.projected_path_llbi_separation_time_sec =
            projected_stats.projected_path_llbi_separation_time_sec;
        result.projected_path_llbi_validity_mode =
            projected_stats.projected_path_llbi_validity_mode;
        result.branch_benders_use_root_user_cuts = options.use_root_user_cuts;
        result.branch_benders_root_user_cut_max_rounds = options.root_user_cut_max_rounds;
        result.branch_benders_root_user_cut_tolerance = root_user_cut_tolerance;
        result.branch_benders_root_user_cut_rounds_executed =
            root_user_stats.rounds_executed;
        result.branch_benders_root_user_cut_callback_calls =
            root_user_stats.callback_calls;
        result.branch_benders_root_user_cuts_added = root_user_stats.cuts_added;
        result.branch_benders_root_user_cut_scenarios_solved =
            root_user_stats.scenarios_solved;
        result.branch_benders_root_user_cut_max_violation = root_user_stats.max_violation;
        result.branch_benders_root_user_cut_total_time_sec = root_user_stats.total_time_sec;
        result.branch_benders_root_user_cut_subproblem_time_sec =
            root_user_stats.subproblem_time_sec;
        result.branch_benders_root_user_cut_skipped_reason =
            options.use_root_user_cuts ? root_user_stats.skipped_reason : "Root user cuts disabled.";
        result.branch_benders_root_user_cut_only_at_root_confirmed =
            root_user_stats.only_at_root_confirmed;
        result.branch_benders_root_user_cut_round_log = root_user_stats.round_log;
        solver::attach_direct_fpp_weight_metadata(result, opt);
        result.solver_weighted_objective = result.objective_value;
        result.notes.push_back(
            "FPP Branch-Benders eta variables, lazy cuts, root user cuts, bounds, and incumbent objective are expressed in weighted burned-node loss units.");

        const auto master_structure =
            analyze_fpp_branch_benders_master_structure(opt, risk_config);
        result.num_variables =
            master_structure.total_variable_count +
            static_cast<std::size_t>(coverage_llbi.num_zeta_vars) +
            static_cast<std::size_t>(path_llbi.num_b_vars);
        result.num_constraints =
            master_structure.base_constraint_count +
            static_cast<std::size_t>(risk_constraint_count) +
            static_cast<std::size_t>(callback_stats.lazy_cuts_added) +
            static_cast<std::size_t>(callback_stats.combinatorial_stats.initial_cuts_added) +
            static_cast<std::size_t>(callback_stats.combinatorial_stats.fractional_cuts_added) +
            static_cast<std::size_t>(root_user_stats.cuts_added) +
            static_cast<std::size_t>(lifted_lower_bound_count) +
            static_cast<std::size_t>(coverage_llbi.num_constraints) +
            static_cast<std::size_t>(path_llbi.num_path_constraints) +
            static_cast<std::size_t>(projected_stats.projected_llbi_cuts_added) +
            (path_llbi.enabled ? path_llbi.scenarios.size() : 0);

        result.notes.push_back("Callback-based FPP-SAA Branch-and-Benders-cut solver.");
        if (options.combinatorial_options.enabled) {
            result.notes.push_back(
                "Combinatorial FPP Branch-and-Benders separation is enabled; scenario cuts are generated by graph search instead of LP subproblem solves.");
            result.notes.push_back(
                "Fractional combinatorial user cuts use non-lifted path cuts when heuristic lifting is requested, because that is the conservative globally valid fractional policy.");
        } else {
            const auto subproblem_diagnostics = subproblem_manager->diagnostics();
            result.notes.push_back("Lazy Benders optimality cuts are generated only at integer candidate incumbents.");
            result.notes.push_back("FPP recourse objective is burned area from scenario LP subproblems.");
            result.notes.push_back(
                "Persistent FPP scenario subproblems are enabled: " +
                std::to_string(subproblem_diagnostics.subproblem_model_build_count) +
                " LP models were built once and y_copy equality bounds were updated before each solve.");
        }
        if (risk_config.type == risk::RiskMeasureType::Expected) {
            result.notes.push_back("FPP Branch-Benders master objective is expected burned area.");
        } else if (risk_config.type == risk::RiskMeasureType::CVaR) {
            result.notes.push_back("FPP Branch-Benders master objective is pure CVaR of scenario eta variables.");
        } else {
            result.notes.push_back("FPP Branch-Benders master objective is a mean-CVaR blend of scenario eta variables.");
        }
        if (options.combinatorial_options.enabled &&
            options.combinatorial_options.separate_fractional) {
            result.notes.push_back(
                "Optional FPP fractional combinatorial Benders user cuts were separated in CPLEX relaxation callbacks.");
        } else if (options.use_root_user_cuts) {
            result.notes.push_back("Optional FPP fractional Benders user cuts were separated at the root node only.");
        } else {
            result.notes.push_back("FPP fractional root user cuts were disabled.");
        }
        if (options.use_lifted_lower_bounds) {
            result.notes.push_back("Optional FPP lifted lower-bound inequalities were added to the callback master.");
        } else {
            result.notes.push_back("FPP lifted lower-bound inequalities were disabled.");
        }
        if (projected_mode != FppProjectedLlbiMode::None) {
            result.notes.push_back(
                "Projected FPP LLBI cuts were added in the original y/eta master space without zeta or b auxiliary variables.");
        }
        result.notes.push_back("Callback FPP Branch-and-Benders is separate from explicit-loop FPP-Benders.");

        env.end();
        return result;
    } catch (const IloException& exc) {
        std::string message = "CPLEX exception in FPP Branch-Benders solver: ";
        message += exc.getMessage();
        env.end();
        throw std::runtime_error(message);
    } catch (...) {
        env.end();
        throw;
    }
}

#endif

}  // namespace firebreak::benders
