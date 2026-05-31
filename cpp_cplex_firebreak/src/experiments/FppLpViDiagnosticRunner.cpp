#include "experiments/FppLpViDiagnosticRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cuts/DominatorCuts.hpp"
#include "cuts/SeparatorCutSeparator.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "io/ScenarioSplitUtils.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/CplexEnvironment.hpp"
#include "solver/FppCutReachabilityCplexModel.hpp"
#include "solver/FppSaaCplexModel.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::experiments {

namespace {

constexpr double kTolerance = 1.0e-7;

[[maybe_unused]] std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

[[maybe_unused]] std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

std::string csv_escape(const std::string& value) {
    bool needs_quotes = false;
    for (const char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }
    std::string out = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

std::string fmt(double value) {
    if (!std::isfinite(value)) {
        return "";
    }
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

[[maybe_unused]] std::vector<std::pair<int, int>> unique_arcs(const opt::OptimizationScenario& scenario) {
    std::set<std::pair<int, int>> seen;
    for (const auto& arc : scenario.arcs) {
        seen.insert({arc.u_index, arc.v_index});
    }
    return {seen.begin(), seen.end()};
}

[[maybe_unused]] double scenario_probability_at(const opt::OptimizationInstance& opt, std::size_t scenario_index) {
    const double probability = opt.scenarios[scenario_index].probability;
    if (std::isfinite(probability)) {
        return probability;
    }
    if (scenario_index < opt.scenario_probabilities.size() &&
        std::isfinite(opt.scenario_probabilities[scenario_index])) {
        return opt.scenario_probabilities[scenario_index];
    }
    throw std::runtime_error("LP diagnostic found a nonfinite scenario probability.");
}

[[maybe_unused]] std::vector<std::vector<int>> build_scenario_nodes_by_position(const opt::OptimizationInstance& opt) {
    std::vector<std::vector<int>> nodes_by_scenario;
    nodes_by_scenario.reserve(opt.scenarios.size());
    for (const auto& scenario : opt.scenarios) {
        std::vector<int> nodes = scenario.observed_node_indices;
        std::sort(nodes.begin(), nodes.end());
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        nodes_by_scenario.push_back(std::move(nodes));
    }
    return nodes_by_scenario;
}

[[maybe_unused]] std::vector<std::vector<int>> build_node_position_by_scenario(
    const opt::OptimizationInstance& opt,
    const std::vector<std::vector<int>>& nodes_by_scenario) {
    const int node_count = opt.node_mapper.size();
    std::vector<std::vector<int>> position_by_scenario;
    position_by_scenario.reserve(nodes_by_scenario.size());
    for (const auto& nodes : nodes_by_scenario) {
        std::vector<int> positions(static_cast<std::size_t>(node_count), -1);
        for (std::size_t pos = 0; pos < nodes.size(); ++pos) {
            positions[static_cast<std::size_t>(nodes[pos])] = static_cast<int>(pos);
        }
        position_by_scenario.push_back(std::move(positions));
    }
    return position_by_scenario;
}

struct RawLpResult {
    double alpha = 0.0;
    int case_id = 0;
    unsigned int seed = 0;
    int train_count = 0;
    std::string formulation;
    std::string variant;
    double lp_objective_initial = 0.0;
    double lp_objective_final = 0.0;
    double solve_time_initial = 0.0;
    double solve_time_total = 0.0;
    int lp_resolves = 0;
    int dominator_cuts_added = 0;
    int dominator_aggregate_cuts_added = 0;
    int dominator_individual_cuts_added = 0;
    double dominator_preprocessing_time_sec = 0.0;
    int separator_cuts_added = 0;
    int separator_rounds = 0;
    int separator_min_cut_calls = 0;
    double separator_time_sec = 0.0;
    double max_separator_violation = 0.0;
    std::string termination_reason;
    int num_variables = 0;
    int constraints_before_cuts = 0;
    int constraints_after_cuts = 0;
    int budget = 0;
    int compact_node_count = 0;
    int eligible_node_count = 0;
    int total_observed_scenario_nodes = 0;
    int total_scenario_arcs = 0;
    std::string notes;
};

struct RoundLpResult {
    double alpha = 0.0;
    int case_id = 0;
    std::string formulation;
    std::string variant;
    int round = 0;
    double lp_objective_before_round = 0.0;
    int cuts_added = 0;
    int min_cut_calls = 0;
    double max_violation = 0.0;
    double separation_time = 0.0;
    double lp_objective_after_round = 0.0;
    double objective_improvement_round = 0.0;
};

[[maybe_unused]] void write_raw_header(std::ofstream& out) {
    out << "alpha,case_id,seed,train_count,formulation,lp_variant,"
        << "lp_objective_initial,lp_objective_final,lp_improvement_abs,lp_improvement_pct,"
        << "solve_time_initial,solve_time_total,lp_resolves,"
        << "dominator_cuts_added,dominator_aggregate_cuts_added,dominator_individual_cuts_added,"
        << "dominator_preprocessing_time_sec,separator_cuts_added,separator_rounds,"
        << "separator_min_cut_calls,separator_time_sec,max_separator_violation,"
        << "termination_reason,num_variables,constraints_before_cuts,constraints_after_cuts,"
        << "budget,compact_node_count,eligible_node_count,total_observed_scenario_nodes,"
        << "total_scenario_arcs,notes\n";
}

[[maybe_unused]] void write_raw_row(std::ofstream& out, const RawLpResult& row) {
    const double improvement = row.lp_objective_final - row.lp_objective_initial;
    const double improvement_pct = improvement / std::max(1.0e-9, std::fabs(row.lp_objective_initial));
    out << fmt(row.alpha) << ","
        << row.case_id << ","
        << row.seed << ","
        << row.train_count << ","
        << row.formulation << ","
        << row.variant << ","
        << fmt(row.lp_objective_initial) << ","
        << fmt(row.lp_objective_final) << ","
        << fmt(improvement) << ","
        << fmt(improvement_pct) << ","
        << fmt(row.solve_time_initial) << ","
        << fmt(row.solve_time_total) << ","
        << row.lp_resolves << ","
        << row.dominator_cuts_added << ","
        << row.dominator_aggregate_cuts_added << ","
        << row.dominator_individual_cuts_added << ","
        << fmt(row.dominator_preprocessing_time_sec) << ","
        << row.separator_cuts_added << ","
        << row.separator_rounds << ","
        << row.separator_min_cut_calls << ","
        << fmt(row.separator_time_sec) << ","
        << fmt(row.max_separator_violation) << ","
        << csv_escape(row.termination_reason) << ","
        << row.num_variables << ","
        << row.constraints_before_cuts << ","
        << row.constraints_after_cuts << ","
        << row.budget << ","
        << row.compact_node_count << ","
        << row.eligible_node_count << ","
        << row.total_observed_scenario_nodes << ","
        << row.total_scenario_arcs << ","
        << csv_escape(row.notes) << "\n";
}

[[maybe_unused]] void write_round_header(std::ofstream& out) {
    out << "alpha,case_id,formulation,variant,round,lp_objective_before_round,"
        << "cuts_added,min_cut_calls,max_violation,separation_time,"
        << "lp_objective_after_round,objective_improvement_round\n";
}

[[maybe_unused]] void write_round_row(std::ofstream& out, const RoundLpResult& row) {
    out << fmt(row.alpha) << ","
        << row.case_id << ","
        << row.formulation << ","
        << row.variant << ","
        << row.round << ","
        << fmt(row.lp_objective_before_round) << ","
        << row.cuts_added << ","
        << row.min_cut_calls << ","
        << fmt(row.max_violation) << ","
        << fmt(row.separation_time) << ","
        << fmt(row.lp_objective_after_round) << ","
        << fmt(row.objective_improvement_round) << "\n";
}

[[maybe_unused]] int total_observed_scenario_nodes(const opt::OptimizationInstance& opt) {
    int total = 0;
    for (const auto& scenario : opt.scenarios) {
        total += static_cast<int>(scenario.observed_node_indices.size());
    }
    return total;
}

[[maybe_unused]] cuts::DominatorCutOptions dominator_options_from_diagnostic(const FppLpViDiagnosticOptions& options) {
    cuts::DominatorCutOptions out;
    out.enabled = true;
    out.max_aggregate_dominator_cuts_per_scenario =
        options.max_aggregate_dominator_cuts_per_scenario;
    out.max_individual_dominator_cuts_per_scenario =
        options.max_individual_dominator_cuts_per_scenario;
    return out;
}

[[maybe_unused]] cuts::SeparatorCutOptions separator_options_from_diagnostic(const FppLpViDiagnosticOptions& options) {
    cuts::SeparatorCutOptions out;
    out.enabled = true;
    out.sep_at_root = true;
    out.sep_frequency_nodes = 1;
    out.sep_max_scenarios_per_call = options.offline_sep_max_scenarios_per_round;
    out.sep_max_nodes_per_scenario = options.offline_sep_max_nodes_per_scenario;
    out.sep_max_cuts_per_call = options.offline_sep_max_cuts_per_round;
    out.sep_min_violation = options.offline_sep_min_violation;
    out.sep_max_cut_cardinality = options.offline_sep_max_cut_cardinality;
    return out;
}

#ifdef FIREBREAK_WITH_CPLEX

struct LpModelData {
    IloNumVarArray y;
    std::vector<IloNumVarArray> x;
    std::vector<IloNumVarArray> q;
    std::vector<int> y_position_by_node;
    std::vector<std::vector<int>> x_position_by_scenario;
    int num_variables = 0;
    int base_constraints = 0;

    explicit LpModelData(IloEnv& env) : y(env) {}
};

void add_y_variables(
    IloEnv& env,
    const opt::OptimizationInstance& opt,
    LpModelData& data) {
    const int node_count = opt.node_mapper.size();
    data.y_position_by_node.assign(static_cast<std::size_t>(node_count), -1);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        const int node = opt.eligible_indices[pos];
        data.y_position_by_node[static_cast<std::size_t>(node)] = static_cast<int>(pos);
        std::ostringstream name;
        name << "y_" << node;
        data.y.add(IloNumVar(env, 0.0, 1.0, ILOFLOAT, name.str().c_str()));
    }
}

LpModelData build_base_lp_model(
    IloEnv& env,
    IloModel& model,
    const opt::OptimizationInstance& opt) {
    const int node_count = opt.node_mapper.size();
    LpModelData data(env);
    add_y_variables(env, opt, data);

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        IloNumVarArray x_s(env);
        for (int node = 0; node < node_count; ++node) {
            std::ostringstream name;
            name << "x_s" << opt.scenarios[s].scenario_id << "_" << node;
            x_s.add(IloNumVar(env, 0.0, 1.0, ILOFLOAT, name.str().c_str()));
        }
        data.x.push_back(x_s);
        std::vector<int> positions(static_cast<std::size_t>(node_count), -1);
        for (int node = 0; node < node_count; ++node) {
            positions[static_cast<std::size_t>(node)] = node;
        }
        data.x_position_by_scenario.push_back(std::move(positions));
    }

    IloExpr objective(env);
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const double probability = scenario_probability_at(opt, s);
        for (int node = 0; node < node_count; ++node) {
            objective += probability * data.x[s][node];
        }
    }
    model.add(IloMinimize(env, objective));
    objective.end();

    IloExpr budget_expr(env);
    for (IloInt pos = 0; pos < data.y.getSize(); ++pos) {
        budget_expr += data.y[pos];
    }
    model.add(budget_expr <= opt.budget);
    budget_expr.end();

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        model.add(data.x[s][opt.scenarios[s].ignition_index] == 1.0);
    }

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        for (const auto& arc : opt.scenarios[s].arcs) {
            IloExpr expr(env);
            expr += data.x[s][arc.u_index];
            expr -= data.x[s][arc.v_index];
            const int y_pos = data.y_position_by_node[static_cast<std::size_t>(arc.v_index)];
            if (y_pos >= 0) {
                expr -= data.y[y_pos];
            }
            model.add(expr <= 0.0);
            expr.end();
        }
    }

    data.num_variables = static_cast<int>(data.y.getSize()) +
        static_cast<int>(opt.scenarios.size()) * node_count;
    data.base_constraints =
        1 +
        static_cast<int>(opt.scenarios.size()) +
        static_cast<int>(opt.total_arcs);
    return data;
}

LpModelData build_cut_lp_model(
    IloEnv& env,
    IloModel& model,
    const opt::OptimizationInstance& opt) {
    const int node_count = opt.node_mapper.size();
    const auto nodes_by_scenario = build_scenario_nodes_by_position(opt);
    const auto position_by_scenario = build_node_position_by_scenario(opt, nodes_by_scenario);

    LpModelData data(env);
    data.x_position_by_scenario = position_by_scenario;
    add_y_variables(env, opt, data);

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        IloNumVarArray x_s(env);
        IloNumVarArray q_s(env);
        for (const int node : nodes_by_scenario[s]) {
            std::ostringstream x_name;
            x_name << "x_s" << opt.scenarios[s].scenario_id << "_" << node;
            x_s.add(IloNumVar(env, 0.0, 1.0, ILOFLOAT, x_name.str().c_str()));

            std::ostringstream q_name;
            q_name << "q_s" << opt.scenarios[s].scenario_id << "_" << node;
            q_s.add(IloNumVar(env, 0.0, 1.0, ILOFLOAT, q_name.str().c_str()));
        }
        data.x.push_back(x_s);
        data.q.push_back(q_s);
    }

    IloExpr objective(env);
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const double probability = scenario_probability_at(opt, s);
        for (IloInt pos = 0; pos < data.x[s].getSize(); ++pos) {
            objective += probability * data.x[s][pos];
        }
    }
    model.add(IloMinimize(env, objective));
    objective.end();

    IloExpr budget_expr(env);
    for (IloInt pos = 0; pos < data.y.getSize(); ++pos) {
        budget_expr += data.y[pos];
    }
    model.add(budget_expr <= opt.budget);
    budget_expr.end();

    int root_constraints = 0;
    int propagation_constraints = 0;
    int pass_constraints = 0;
    int upper_constraints = 0;

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const int root = opt.scenarios[s].ignition_index;
        const int root_pos = data.x_position_by_scenario[s][static_cast<std::size_t>(root)];
        if (root_pos < 0) {
            throw std::runtime_error("Cut LP diagnostic root variable was not created.");
        }
        model.add(data.q[s][root_pos] == 1.0);
        model.add(data.x[s][root_pos] == 1.0);
        root_constraints += 2;
    }

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        for (const auto& arc : unique_arcs(opt.scenarios[s])) {
            const int u_pos = data.x_position_by_scenario[s][static_cast<std::size_t>(arc.first)];
            const int v_pos = data.x_position_by_scenario[s][static_cast<std::size_t>(arc.second)];
            if (u_pos < 0 || v_pos < 0) {
                throw std::runtime_error("Cut LP diagnostic arc endpoint variable was not created.");
            }
            model.add(data.q[s][v_pos] >= data.x[s][u_pos]);
            ++propagation_constraints;
        }
    }

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const int root = opt.scenarios[s].ignition_index;
        for (int compact_node : opt.scenarios[s].observed_node_indices) {
            if (compact_node == root) {
                continue;
            }
            const int local_pos = data.x_position_by_scenario[s][static_cast<std::size_t>(compact_node)];
            if (local_pos < 0) {
                continue;
            }
            const int y_pos = data.y_position_by_node[static_cast<std::size_t>(compact_node)];
            if (y_pos >= 0) {
                IloExpr pass_expr(env);
                pass_expr += data.x[s][local_pos];
                pass_expr -= data.q[s][local_pos];
                pass_expr += data.y[y_pos];
                model.add(pass_expr >= 0.0);
                pass_expr.end();
                ++pass_constraints;

                IloExpr upper_expr(env);
                upper_expr += data.x[s][local_pos];
                upper_expr += data.y[y_pos];
                model.add(upper_expr <= 1.0);
                upper_expr.end();
                ++upper_constraints;
            } else {
                model.add(data.x[s][local_pos] >= data.q[s][local_pos]);
                ++pass_constraints;
            }
        }
    }

    int observed_total = 0;
    for (const auto& nodes : nodes_by_scenario) {
        observed_total += static_cast<int>(nodes.size());
    }
    data.num_variables = static_cast<int>(data.y.getSize()) + 2 * observed_total;
    data.base_constraints =
        1 + root_constraints + propagation_constraints + pass_constraints + upper_constraints;
    (void)node_count;
    return data;
}

int add_firebreak_upper_bounds(
    IloEnv& env,
    IloModel& model,
    const opt::OptimizationInstance& opt,
    const LpModelData& data) {
    int added = 0;
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const int root = opt.scenarios[s].ignition_index;
        for (const int node : opt.scenarios[s].observed_node_indices) {
            if (node == root) {
                continue;
            }
            const int y_pos = data.y_position_by_node[static_cast<std::size_t>(node)];
            if (y_pos < 0) {
                continue;
            }
            const int x_pos = data.x_position_by_scenario[s][static_cast<std::size_t>(node)];
            if (x_pos < 0) {
                continue;
            }
            IloExpr expr(env);
            expr += data.x[s][x_pos];
            expr += data.y[y_pos];
            model.add(expr <= 1.0);
            expr.end();
            ++added;
        }
    }
    return added;
}

cuts::DominatorCutStats add_dominator_cuts(
    IloEnv& env,
    IloModel& model,
    const opt::OptimizationInstance& opt,
    const LpModelData& data,
    bool skip_self_individual_cuts,
    const FppLpViDiagnosticOptions& options) {
    cuts::DominatorVariableAccess access;
    access.has_x = [&](std::size_t scenario_index, int compact_node) {
        return scenario_index < data.x_position_by_scenario.size() &&
               compact_node >= 0 &&
               compact_node < static_cast<int>(data.x_position_by_scenario[scenario_index].size()) &&
               data.x_position_by_scenario[scenario_index][static_cast<std::size_t>(compact_node)] >= 0;
    };
    access.get_x = [&](std::size_t scenario_index, int compact_node) -> IloNumVar {
        const int pos = data.x_position_by_scenario[scenario_index][static_cast<std::size_t>(compact_node)];
        return data.x[scenario_index][pos];
    };
    access.has_y = [&](int compact_node) {
        return compact_node >= 0 &&
               compact_node < static_cast<int>(data.y_position_by_node.size()) &&
               data.y_position_by_node[static_cast<std::size_t>(compact_node)] >= 0;
    };
    access.get_y = [&](int compact_node) -> IloNumVar {
        return data.y[data.y_position_by_node[static_cast<std::size_t>(compact_node)]];
    };
    access.skip_self_individual_cuts = skip_self_individual_cuts;
    return cuts::add_dominator_cuts_to_model(
        env,
        model,
        opt,
        dominator_options_from_diagnostic(options),
        access);
}

double solve_lp(
    IloEnv& env,
    IloCplex& cplex,
    double time_limit,
    int threads,
    bool verbose,
    double& solve_seconds,
    std::string& status) {
    if (!verbose) {
        cplex.setOut(env.getNullStream());
        cplex.setWarning(env.getNullStream());
    }
    if (time_limit > 0.0) {
        cplex.setParam(IloCplex::Param::TimeLimit, time_limit);
    }
    if (threads > 0) {
        cplex.setParam(IloCplex::Param::Threads, threads);
    }
    const auto start = std::chrono::steady_clock::now();
    const bool ok = cplex.solve();
    const auto end = std::chrono::steady_clock::now();
    solve_seconds = std::chrono::duration<double>(end - start).count();
    std::ostringstream status_out;
    status_out << cplex.getStatus();
    status = status_out.str();
    if (!ok) {
        throw std::runtime_error("LP diagnostic CPLEX solve failed with status " + status + ".");
    }
    return cplex.getObjValue();
}

std::vector<double> extract_ybar(const opt::OptimizationInstance& opt, const LpModelData& data, IloCplex& cplex) {
    std::vector<double> ybar(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t node = 0; node < data.y_position_by_node.size(); ++node) {
        const int pos = data.y_position_by_node[node];
        if (pos >= 0) {
            ybar[node] = cplex.getValue(data.y[pos]);
        }
    }
    return ybar;
}

std::vector<std::vector<double>> extract_xbar(
    const opt::OptimizationInstance& opt,
    const LpModelData& data,
    IloCplex& cplex) {
    const int node_count = opt.node_mapper.size();
    std::vector<std::vector<double>> xbar(
        opt.scenarios.size(),
        std::vector<double>(static_cast<std::size_t>(node_count), 0.0));
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        for (int node = 0; node < node_count; ++node) {
            const int pos = data.x_position_by_scenario[s][static_cast<std::size_t>(node)];
            if (pos >= 0) {
                xbar[s][static_cast<std::size_t>(node)] = cplex.getValue(data.x[s][pos]);
            }
        }
    }
    return xbar;
}

void add_separator_cut(
    IloEnv& env,
    IloModel& model,
    const LpModelData& data,
    const cuts::CandidateSeparatorCut& cut) {
    const int x_pos =
        data.x_position_by_scenario[static_cast<std::size_t>(cut.scenario_index)]
            [static_cast<std::size_t>(cut.target_compact_node)];
    if (x_pos < 0) {
        throw std::runtime_error("Separator cut target x variable is missing.");
    }
    IloExpr expr(env);
    expr += data.x[static_cast<std::size_t>(cut.scenario_index)][x_pos];
    for (const int node : cut.separator_compact_nodes) {
        const int y_pos = data.y_position_by_node[static_cast<std::size_t>(node)];
        if (y_pos < 0) {
            expr.end();
            throw std::runtime_error("Separator cut contains a node without a y variable.");
        }
        expr += data.y[y_pos];
    }
    model.add(expr <= static_cast<double>(cut.separator_compact_nodes.size()));
    expr.end();
}

void run_separator_rounds(
    IloEnv& env,
    IloModel& model,
    IloCplex& cplex,
    const opt::OptimizationInstance& opt,
    const LpModelData& data,
    const FppLpViDiagnosticOptions& options,
    const std::string& formulation,
    const std::string& variant,
    double alpha,
    int case_id,
    double& current_objective,
    int& constraint_count,
    RawLpResult& raw,
    std::vector<RoundLpResult>& round_rows) {
    cuts::SeparatorCutSeparator separator(opt, separator_options_from_diagnostic(options));
    for (int round = 1; round <= options.offline_sep_max_rounds; ++round) {
        const auto ybar = extract_ybar(opt, data, cplex);
        const auto xbar = extract_xbar(opt, data, cplex);
        separator.resetStats();
        const auto cuts = separator.separate(ybar, xbar, options.offline_sep_max_cuts_per_round);
        const auto stats = separator.stats();
        double max_violation = 0.0;
        for (const auto& cut : cuts) {
            max_violation = std::max(max_violation, cut.violation);
        }

        RoundLpResult round_row;
        round_row.alpha = alpha;
        round_row.case_id = case_id;
        round_row.formulation = formulation;
        round_row.variant = variant;
        round_row.round = round;
        round_row.lp_objective_before_round = current_objective;
        round_row.cuts_added = static_cast<int>(cuts.size());
        round_row.min_cut_calls = stats.min_cut_calls;
        round_row.max_violation = max_violation;
        round_row.separation_time = stats.separator_time_sec;

        raw.separator_rounds = round;
        raw.separator_min_cut_calls += stats.min_cut_calls;
        raw.separator_time_sec += stats.separator_time_sec;
        raw.max_separator_violation = std::max(raw.max_separator_violation, max_violation);

        if (cuts.empty()) {
            round_row.lp_objective_after_round = current_objective;
            round_row.objective_improvement_round = 0.0;
            round_rows.push_back(round_row);
            raw.termination_reason = "no_cuts";
            return;
        }

        for (const auto& cut : cuts) {
            if (cut.violation <= options.offline_sep_min_violation) {
                throw std::runtime_error("Offline separator returned a nonviolated cut.");
            }
            add_separator_cut(env, model, data, cut);
        }
        raw.separator_cuts_added += static_cast<int>(cuts.size());
        constraint_count += static_cast<int>(cuts.size());

        double solve_time = 0.0;
        std::string status;
        const double next_objective = solve_lp(
            env,
            cplex,
            options.time_limit_seconds,
            options.threads,
            options.verbose,
            solve_time,
            status);
        raw.solve_time_total += solve_time;
        ++raw.lp_resolves;
        if (next_objective + kTolerance < current_objective) {
            throw std::runtime_error("LP objective decreased after adding separator cuts.");
        }

        round_row.lp_objective_after_round = next_objective;
        round_row.objective_improvement_round = next_objective - current_objective;
        round_rows.push_back(round_row);
        current_objective = next_objective;
    }
    raw.termination_reason = "max_rounds";
}

RawLpResult solve_lp_variant(
    const opt::OptimizationInstance& opt,
    const FppLpViDiagnosticOptions& options,
    double alpha,
    int case_id,
    unsigned int seed,
    const std::string& formulation,
    const std::string& variant,
    double native_objective,
    std::vector<RoundLpResult>& round_rows) {
    const auto full_start = std::chrono::steady_clock::now();
    IloEnv env;
    try {
        IloModel model(env);
        LpModelData data = formulation == "base"
            ? build_base_lp_model(env, model, opt)
            : build_cut_lp_model(env, model, opt);
        int constraint_count = data.base_constraints;

        RawLpResult raw;
        raw.alpha = alpha;
        raw.case_id = case_id;
        raw.seed = seed;
        raw.train_count = static_cast<int>(options.train_count);
        raw.formulation = formulation;
        raw.variant = variant;
        raw.lp_objective_initial = native_objective;
        raw.num_variables = data.num_variables;
        raw.constraints_before_cuts = constraint_count;
        raw.budget = opt.budget;
        raw.compact_node_count = opt.node_mapper.size();
        raw.eligible_node_count = static_cast<int>(opt.eligible_indices.size());
        raw.total_observed_scenario_nodes = total_observed_scenario_nodes(opt);
        raw.total_scenario_arcs = static_cast<int>(opt.total_arcs);

        if (variant == "lp_plus_firebreak_upper_bound") {
            if (formulation == "base") {
                const int upper_cuts = add_firebreak_upper_bounds(env, model, opt, data);
                constraint_count += upper_cuts;
                raw.notes = "Added firebreak upper-bound cuts x+y<=1 for base LP.";
            } else {
                raw.notes = "Cut formulation already includes firebreak upper-bound constraints.";
            }
        }

        if (variant == "lp_plus_dominator" ||
            variant == "lp_plus_dominator_plus_separator_offline") {
            const auto stats = add_dominator_cuts(
                env,
                model,
                opt,
                data,
                formulation == "cut",
                options);
            raw.dominator_cuts_added = stats.total_cuts_added();
            raw.dominator_aggregate_cuts_added = stats.aggregate_cuts_added;
            raw.dominator_individual_cuts_added = stats.individual_cuts_added;
            raw.dominator_preprocessing_time_sec = stats.preprocessing_time_sec;
            constraint_count += stats.total_cuts_added();
        }

        IloCplex cplex(model);
        double solve_time = 0.0;
        std::string status;
        double objective = solve_lp(
            env,
            cplex,
            options.time_limit_seconds,
            options.threads,
            options.verbose,
            solve_time,
            status);
        raw.solve_time_initial = solve_time;
        raw.solve_time_total = solve_time;
        raw.lp_resolves = 1;

        if (variant == "native_lp") {
            raw.lp_objective_initial = objective;
            raw.termination_reason = "single_solve";
        } else if (variant == "lp_plus_separator_offline" ||
                   variant == "lp_plus_dominator_plus_separator_offline") {
            run_separator_rounds(
                env,
                model,
                cplex,
                opt,
                data,
                options,
                formulation,
                variant,
                alpha,
                case_id,
                objective,
                constraint_count,
                raw,
                round_rows);
        } else {
            raw.termination_reason = raw.notes.empty() ? "single_solve" : "single_solve_note";
        }

        raw.lp_objective_final = objective;
        if (variant == "lp_plus_separator_offline" ||
            variant == "lp_plus_dominator_plus_separator_offline") {
            raw.lp_objective_final = round_rows.empty() ? objective : objective;
            if (!round_rows.empty()) {
                raw.lp_objective_final = round_rows.back().lp_objective_after_round;
            }
        }
        raw.constraints_after_cuts = constraint_count;

        if (raw.lp_objective_final + kTolerance < native_objective) {
            throw std::runtime_error(
                "LP diagnostic valid-inequality variant decreased below native LP objective.");
        }

        const auto full_end = std::chrono::steady_clock::now();
        raw.solve_time_total =
            std::chrono::duration<double>(full_end - full_start).count();
        env.end();
        return raw;
    } catch (...) {
        env.end();
        throw;
    }
}

#endif

[[maybe_unused]] std::filesystem::path split_path(
    const std::filesystem::path& output_dir,
    const std::string& landscape,
    unsigned int seed,
    std::size_t train_count,
    std::size_t test_count,
    std::size_t case_id,
    const std::string& suffix) {
    std::ostringstream name;
    name << landscape
         << "_seed" << seed
         << "_train" << train_count
         << "_test" << test_count
         << "_case" << case_id
         << "_" << suffix << ".csv";
    return output_dir / "splits" / name.str();
}

}  // namespace

int FppLpViDiagnosticRunner::run(const FppLpViDiagnosticOptions& options) const {
    if (!solver::cplex_support_enabled()) {
        throw std::runtime_error(solver::cplex_unavailable_message());
    }
    if (options.alphas.empty()) {
        throw std::runtime_error("LP VI diagnostic requires at least one alpha.");
    }
    if (options.train_count == 0 || options.num_cases == 0) {
        throw std::runtime_error("LP VI diagnostic requires positive train_count and num_cases.");
    }

#ifndef FIREBREAK_WITH_CPLEX
    (void)options;
    throw std::runtime_error(solver::cplex_unavailable_message());
#else
    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : io::resolve_input_path(options.results_path.string());
    const auto output_dir = io::resolve_output_path(options.output_dir.string());
    std::filesystem::create_directories(output_dir);
    std::filesystem::create_directories(output_dir / "splits");

    const auto raw_path = output_dir / "raw_lp_results.csv";
    const auto round_path = output_dir / "round_results.csv";
    const auto notes_path = output_dir / "README_or_notes.txt";

    std::ofstream raw_out(raw_path);
    std::ofstream round_out(round_path);
    if (!raw_out || !round_out) {
        throw std::runtime_error("Could not open LP diagnostic output CSV files.");
    }
    write_raw_header(raw_out);
    write_round_header(round_out);

    std::ofstream notes(notes_path);
    notes << "FPP LP valid-inequality diagnostic\n"
          << "Native LP relaxes y, x, and q variables to [0,1].\n"
          << "Separator variants use offline SeparatorCutSeparator rounds, not CPLEX callbacks.\n"
          << "Positive LP objective improvement means a stronger lower bound for this minimization model.\n"
          << "Seed convention: seed = seed_base + case_id, independent of alpha.\n";

    const auto inventory = io::detect_message_files(results_path);
    const auto available_ids = inventory.ids();
    if (options.train_count + options.test_count > available_ids.size()) {
        throw std::runtime_error("LP diagnostic train/test split exceeds available scenario count.");
    }

    io::Cell2FireReader reader;
    opt::OptimizationInstanceBuilder builder;

    const std::vector<std::string> formulations = {"base", "cut"};
    const std::vector<std::string> variants = {
        "native_lp",
        "lp_plus_firebreak_upper_bound",
        "lp_plus_dominator",
        "lp_plus_separator_offline",
        "lp_plus_dominator_plus_separator_offline",
    };

    int run_count = 0;
    for (const double alpha : options.alphas) {
        for (std::size_t case_id = 0; case_id < options.num_cases; ++case_id) {
            const unsigned int seed = options.seed_base + static_cast<unsigned int>(case_id);
            const auto split = io::generate_train_test_split(
                available_ids,
                seed,
                options.train_count,
                options.test_count);
            io::save_scenario_ids(
                split_path(output_dir, options.landscape, seed, options.train_count, options.test_count, case_id, "train"),
                split.train_ids);
            io::save_scenario_ids(
                split_path(output_dir, options.landscape, seed, options.train_count, options.test_count, case_id, "test"),
                split.test_ids);

            std::vector<std::string> warnings;
            auto train_instance = reader.load_instance(
                options.landscape,
                forest_path,
                results_path,
                split.train_ids,
                warnings);
            auto opt_instance = builder.build(train_instance, alpha, false);

            for (const auto& formulation : formulations) {
                std::vector<RoundLpResult> ignored_rounds;
                const auto native = solve_lp_variant(
                    opt_instance,
                    options,
                    alpha,
                    static_cast<int>(case_id),
                    seed,
                    formulation,
                    "native_lp",
                    0.0,
                    ignored_rounds);
                write_raw_row(raw_out, native);
                ++run_count;
                std::cout << "LP diagnostic alpha=" << alpha
                          << " case=" << case_id
                          << " formulation=" << formulation
                          << " variant=native_lp"
                          << " objective=" << native.lp_objective_final << "\n" << std::flush;

                for (const auto& variant : variants) {
                    if (variant == "native_lp") {
                        continue;
                    }
                    std::vector<RoundLpResult> round_rows;
                    auto result = solve_lp_variant(
                        opt_instance,
                        options,
                        alpha,
                        static_cast<int>(case_id),
                        seed,
                        formulation,
                        variant,
                        native.lp_objective_final,
                        round_rows);
                    write_raw_row(raw_out, result);
                    for (const auto& round : round_rows) {
                        write_round_row(round_out, round);
                    }
                    ++run_count;
                    std::cout << "LP diagnostic alpha=" << alpha
                              << " case=" << case_id
                              << " formulation=" << formulation
                              << " variant=" << variant
                              << " final=" << result.lp_objective_final
                              << " improvement=" << (result.lp_objective_final - result.lp_objective_initial)
                              << " cuts=" << (result.dominator_cuts_added + result.separator_cuts_added)
                              << "\n" << std::flush;
                }
            }
        }
    }

    std::cout << "LP diagnostic completed. Rows: " << run_count << "\n"
              << "Raw CSV: " << io::path_to_string(raw_path) << "\n"
              << "Round CSV: " << io::path_to_string(round_path) << "\n";
    return 0;
#endif
}

}  // namespace firebreak::experiments
