#include "benders/DpvBranchBendersSolver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
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
#include "benders/DpvLiftedLowerBound.hpp"
#include "benders/DpvPersistentScenarioSubproblemManager.hpp"
#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::benders {

namespace {

void validate_options(const DpvBranchBendersOptions& options) {
    if (options.tolerance < 0.0) {
        throw std::runtime_error("DPV Branch-Benders tolerance must be nonnegative.");
    }
    if (options.time_limit_seconds < 0.0) {
        throw std::runtime_error("DPV Branch-Benders time_limit_seconds must be nonnegative.");
    }
    if (options.mip_gap < -1.0) {
        throw std::runtime_error("DPV Branch-Benders mip_gap must be nonnegative, or omitted.");
    }
    if (options.threads < 0) {
        throw std::runtime_error("DPV Branch-Benders threads must be nonnegative.");
    }
    if (options.use_root_user_cuts && options.root_user_cut_max_rounds <= 0) {
        throw std::runtime_error("DPV Branch-Benders root_user_cut_max_rounds must be positive when root user cuts are enabled.");
    }
    if (std::isfinite(options.root_user_cut_tolerance) &&
        options.root_user_cut_tolerance < 0.0) {
        throw std::runtime_error("DPV Branch-Benders root_user_cut_tolerance must be nonnegative.");
    }
}

void validate_instance(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("DPV Branch-Benders requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("DPV Branch-Benders requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("DPV Branch-Benders requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0 || opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("DPV Branch-Benders budget must be between zero and the eligible-node count.");
    }
    std::size_t counted_pairs = 0;
    for (const auto& scenario : opt.scenarios) {
        counted_pairs += scenario.dpv.product_pairs.size();
    }
    if (counted_pairs != opt.total_dpv_pairs) {
        throw std::runtime_error("DPV Branch-Benders total_dpv_pairs does not match scenario product-pair counts.");
    }
}

#ifdef FIREBREAK_WITH_CPLEX

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

struct BranchBendersVariableAccess {
    IloBoolVarArray y;
    IloNumVarArray eta;
    std::vector<int> y_position_by_node;
};

struct BranchBendersCallbackStats {
    int callback_calls = 0;
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
    mutable std::mutex mutex;
};

struct BranchBendersRootUserCutStats {
    bool enabled = false;
    int max_rounds = 1;
    double tolerance = 0.0;
    int rounds_executed = 0;
    int callback_calls = 0;
    int cuts_added = 0;
    int scenarios_solved = 0;
    double max_violation = 0.0;
    double total_time_sec = 0.0;
    double subproblem_time_sec = 0.0;
    double max_subproblem_time_sec = 0.0;
    std::string skipped_reason;
    bool only_at_root_confirmed = true;
    std::vector<solver::BranchBendersRootUserCutRoundLog> round_log;
    mutable std::mutex mutex;
};

double effective_root_user_cut_tolerance(const DpvBranchBendersOptions& options) {
    if (std::isfinite(options.root_user_cut_tolerance)) {
        return options.root_user_cut_tolerance;
    }
    return options.tolerance;
}

class DpvBranchBendersCandidateCallback : public IloCplex::Callback::Function {
public:
    DpvBranchBendersCandidateCallback(
        const opt::OptimizationInstance& opt,
        DpvPersistentScenarioSubproblemManager& subproblem_manager,
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
        }

        if (!context.inCandidate() || !context.isCandidatePoint()) {
            record_callback_time(callback_start);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(stats_.mutex);
            ++stats_.incumbent_callback_calls;
        }

        std::vector<int> ybar(access_.y.getSize(), 0);
        for (IloInt pos = 0; pos < access_.y.getSize(); ++pos) {
            ybar[static_cast<std::size_t>(pos)] =
                context.getCandidatePoint(access_.y[pos]) > 0.5 ? 1 : 0;
        }

        std::vector<double> eta_values(access_.eta.getSize(), 0.0);
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
            const double cut_violation = sub_result.benders_cut.violationAt(eta_value, compact_y);
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
                            "DPV Branch-Benders cut references a compact node without a master y variable.");
                    }
                    const int y_pos = access_.y_position_by_node[static_cast<std::size_t>(compact_index)];
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
    DpvPersistentScenarioSubproblemManager& subproblem_manager_;
    BranchBendersVariableAccess access_;
    double tolerance_ = 1.0e-6;
    bool verbose_ = false;
    BranchBendersCallbackStats& stats_;
};

class DpvBranchBendersRootUserCutCallback : public IloCplex::Callback::Function {
public:
    DpvBranchBendersRootUserCutCallback(
        const opt::OptimizationInstance& opt,
        DpvPersistentScenarioSubproblemManager& subproblem_manager,
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
            const double violation = sub_result.benders_cut.violationAt(eta_value, compact_y);
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
                            "DPV Branch-Benders root user cut references a compact node without a master y variable.");
                    }
                    const int y_pos = access_.y_position_by_node[static_cast<std::size_t>(compact_index)];
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
        log.avg_violation = cuts_added > 0 ? sum_violation / static_cast<double>(cuts_added) : 0.0;
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
    DpvPersistentScenarioSubproblemManager& subproblem_manager_;
    BranchBendersVariableAccess access_;
    int max_rounds_ = 1;
    double tolerance_ = 1.0e-6;
    bool verbose_ = false;
    BranchBendersRootUserCutStats& stats_;
};

class DpvBranchBendersCombinedCallback : public IloCplex::Callback::Function {
public:
    DpvBranchBendersCombinedCallback(
        const opt::OptimizationInstance& opt,
        DpvPersistentScenarioSubproblemManager& subproblem_manager,
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
    DpvBranchBendersCandidateCallback candidate_callback_;
    DpvBranchBendersRootUserCutCallback root_callback_;
};

void apply_cplex_parameters(
    IloCplex& cplex,
    IloEnv env,
    const DpvBranchBendersOptions& options) {
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

DpvBranchBendersMasterStructure analyze_dpv_branch_benders_master_structure(
    const opt::OptimizationInstance& opt) {
    validate_instance(opt);
    DpvBranchBendersMasterStructure structure;
    structure.y_variable_count = opt.eligible_indices.size();
    structure.eta_variable_count = opt.scenarios.size();
    structure.total_variable_count = structure.y_variable_count + structure.eta_variable_count;
    structure.budget_constraint_count = 1;
    structure.base_constraint_count = 1;
    structure.has_scenario_recourse_variables = false;
    return structure;
}

#ifndef FIREBREAK_WITH_CPLEX

solver::ModelResult DpvBranchBendersSolver::solve(
    const opt::OptimizationInstance& opt,
    const DpvBranchBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);
    throw std::runtime_error(solver::cplex_unavailable_message());
}

#else

solver::ModelResult DpvBranchBendersSolver::solve(
    const opt::OptimizationInstance& opt,
    const DpvBranchBendersOptions& options) const {
    validate_options(options);
    validate_instance(opt);

    solver::ModelResult result;
    result.method = "DPV-SAA-Branch-Benders";
    result.branch_benders_enabled = true;
    const double root_user_cut_tolerance = effective_root_user_cut_tolerance(options);
    result.branch_benders_use_root_user_cuts = options.use_root_user_cuts;
    result.branch_benders_root_user_cut_max_rounds = options.root_user_cut_max_rounds;
    result.branch_benders_root_user_cut_tolerance = root_user_cut_tolerance;
    if (!options.use_root_user_cuts) {
        result.branch_benders_root_user_cut_skipped_reason = "Root user cuts disabled.";
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

        IloExpr objective(env);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            objective += opt.scenarios[s].probability * eta[static_cast<IloInt>(s)];
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
            const auto llb_result = build_dpv_lifted_lower_bounds(opt);
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
            result.benders_lifted_lower_bound_notes = llb_result.notes;
        }

        IloCplex cplex(model);
        apply_cplex_parameters(cplex, env, options);
        DpvPersistentScenarioSubproblemManager subproblem_manager(opt, options.verbose);

        if (options.warm_start && options.warm_start->enabled) {
            attach_warm_start_metadata(result, *options.warm_start);
            const auto y_start_binary = warm_start_to_y_values(opt, *options.warm_start);
            IloNumVarArray start_vars(env);
            IloNumArray start_vals(env);
            for (IloInt pos = 0; pos < y.getSize(); ++pos) {
                start_vars.add(y[pos]);
                start_vals.add(static_cast<double>(y_start_binary[static_cast<std::size_t>(pos)]));
            }
            cplex.addMIPStart(start_vars, start_vals);
            start_vals.end();
            start_vars.end();
            result.warm_start_used = true;
        }

        BranchBendersVariableAccess access;
        access.y = y;
        access.eta = eta;
        access.y_position_by_node = y_position_by_node;
        BranchBendersCallbackStats callback_stats;
        BranchBendersRootUserCutStats root_user_stats;
        root_user_stats.enabled = options.use_root_user_cuts;
        root_user_stats.max_rounds = options.root_user_cut_max_rounds;
        root_user_stats.tolerance = root_user_cut_tolerance;

        std::unique_ptr<DpvBranchBendersCandidateCallback> candidate_callback;
        std::unique_ptr<DpvBranchBendersCombinedCallback> combined_callback;
        if (options.use_root_user_cuts) {
            combined_callback = std::make_unique<DpvBranchBendersCombinedCallback>(
                opt,
                subproblem_manager,
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
            candidate_callback = std::make_unique<DpvBranchBendersCandidateCallback>(
                opt,
                subproblem_manager,
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
        double verified_objective = 0.0;
        double final_max_violation = 0.0;
        double verification_subproblem_time = 0.0;
        double verification_max_subproblem_time = 0.0;
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            const auto sub_result =
                subproblem_manager.solveScenario(static_cast<int>(s), y_values);
            verified_objective += opt.scenarios[s].probability * sub_result.objective_value;
            verification_subproblem_time += sub_result.runtime_seconds;
            verification_max_subproblem_time =
                std::max(verification_max_subproblem_time, sub_result.runtime_seconds);
            const double eta_value = eta_values[s];
            const double direct_violation = sub_result.objective_value - eta_value;
            const double cut_violation = sub_result.benders_cut.violationAt(eta_value, compact_y);
            final_max_violation = std::max(final_max_violation, std::max(direct_violation, cut_violation));
        }
        if (final_max_violation < options.tolerance) {
            final_max_violation = std::max(0.0, final_max_violation);
        }

        result.objective_value = verified_objective;
        result.selected_firebreak_indices = selected_compact_indices(opt, y_values);
        result.selected_firebreak_original_nodes = selected_original_nodes(opt, y_values);
        result.iterations = static_cast<int>(cplex.getNiterations());
        result.cuts_added = callback_stats.lazy_cuts_added;
        result.max_cut_violation = final_max_violation;
        result.branch_benders_callback_calls = callback_stats.callback_calls;
        result.branch_benders_candidate_callback_calls = callback_stats.callback_calls;
        result.branch_benders_incumbent_callback_calls =
            callback_stats.incumbent_callback_calls;
        result.branch_benders_candidate_incumbents_checked =
            callback_stats.candidate_incumbents_checked;
        result.branch_benders_subproblems_attempted =
            callback_stats.subproblems_attempted +
            static_cast<int>(opt.scenarios.size()) +
            root_user_stats.scenarios_solved;
        result.branch_benders_subproblems_solved =
            callback_stats.subproblems_solved +
            static_cast<int>(opt.scenarios.size()) +
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
        result.branch_benders_use_root_user_cuts = options.use_root_user_cuts;
        result.branch_benders_root_user_cut_max_rounds = options.root_user_cut_max_rounds;
        result.branch_benders_root_user_cut_tolerance = root_user_cut_tolerance;
        result.branch_benders_root_user_cut_rounds_executed =
            root_user_stats.rounds_executed;
        result.branch_benders_root_user_cut_callback_calls =
            root_user_stats.callback_calls;
        result.branch_benders_root_user_cuts_added =
            root_user_stats.cuts_added;
        result.branch_benders_root_user_cut_scenarios_solved =
            root_user_stats.scenarios_solved;
        result.branch_benders_root_user_cut_max_violation =
            root_user_stats.max_violation;
        result.branch_benders_root_user_cut_total_time_sec =
            root_user_stats.total_time_sec;
        result.branch_benders_root_user_cut_subproblem_time_sec =
            root_user_stats.subproblem_time_sec;
        result.branch_benders_root_user_cut_skipped_reason =
            options.use_root_user_cuts ? root_user_stats.skipped_reason : "Root user cuts disabled.";
        result.branch_benders_root_user_cut_only_at_root_confirmed =
            root_user_stats.only_at_root_confirmed;
        result.branch_benders_root_user_cut_round_log =
            root_user_stats.round_log;

        const auto master_structure = analyze_dpv_branch_benders_master_structure(opt);
        result.num_variables = master_structure.total_variable_count;
        result.num_constraints =
            master_structure.base_constraint_count +
            static_cast<std::size_t>(callback_stats.lazy_cuts_added) +
            static_cast<std::size_t>(root_user_stats.cuts_added) +
            static_cast<std::size_t>(lifted_lower_bound_count);

        const auto subproblem_diagnostics = subproblem_manager.diagnostics();
        result.notes.push_back("Callback-based DPV-SAA Branch-and-Benders-cut solver.");
        result.notes.push_back("Lazy Benders optimality cuts are generated only at integer candidate incumbents.");
        result.notes.push_back(
            "Persistent DPV scenario subproblems are enabled: " +
            std::to_string(subproblem_diagnostics.subproblem_model_build_count) +
            " LP models were built once and y_copy equality bounds were updated before each solve.");
        if (options.use_root_user_cuts) {
            result.notes.push_back("Optional fractional Benders user cuts were separated at the root node only.");
        } else {
            result.notes.push_back("Fractional root user cuts were disabled.");
        }
        result.notes.push_back("Benders cuts preserve the Phase 13/14 CPLEX dual sign convention.");
        if (options.use_lifted_lower_bounds) {
            result.notes.push_back("Optional lifted lower-bound inequalities were added to the callback master.");
        } else {
            result.notes.push_back("Lifted lower-bound inequalities were disabled.");
        }

        env.end();
        return result;
    } catch (const IloException& exc) {
        std::string message = "CPLEX exception in DPV Branch-Benders solver: ";
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
