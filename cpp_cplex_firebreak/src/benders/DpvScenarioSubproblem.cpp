#include "benders/DpvScenarioSubproblem.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "opt/WeightedDpvScoring.hpp"
#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::benders {

namespace {

void validate_scenario_position(const opt::OptimizationInstance& opt, int scenario_position) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("DPV Benders subproblem requires at least one mapped node.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("DPV Benders subproblem requires at least one eligible firebreak node.");
    }
    if (scenario_position < 0 || scenario_position >= static_cast<int>(opt.scenarios.size())) {
        throw std::runtime_error("DPV Benders subproblem scenario position is out of range.");
    }
}

void validate_ybar(const opt::OptimizationInstance& opt, const std::vector<int>& ybar_binary) {
    if (ybar_binary.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("DPV Benders subproblem ybar length must equal the eligible-node count.");
    }
    for (const int value : ybar_binary) {
        if (value != 0 && value != 1) {
            throw std::runtime_error("DPV Benders subproblem ybar values must be binary.");
        }
    }
}

void validate_fractional_ybar(const opt::OptimizationInstance& opt, const std::vector<double>& ybar_values) {
    if (ybar_values.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("DPV Benders subproblem fractional ybar length must equal the eligible-node count.");
    }
    for (const double value : ybar_values) {
        if (value < -1.0e-9 || value > 1.0 + 1.0e-9) {
            throw std::runtime_error("DPV Benders subproblem fractional ybar values must be in [0, 1].");
        }
    }
}

#ifdef FIREBREAK_WITH_CPLEX
std::unordered_map<int, int> build_y_position_by_node_index(const opt::OptimizationInstance& opt) {
    std::unordered_map<int, int> y_position_by_node;
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        y_position_by_node[opt.eligible_indices[pos]] = static_cast<int>(pos);
    }
    return y_position_by_node;
}
#endif

}  // namespace

DpvScenarioSubproblemStructure analyze_dpv_scenario_subproblem_structure(
    const opt::OptimizationInstance& opt,
    int scenario_position) {
    validate_scenario_position(opt, scenario_position);
    const auto& scenario = opt.scenarios[static_cast<std::size_t>(scenario_position)];

    DpvScenarioSubproblemStructure structure;
    structure.scenario_id = scenario.scenario_id;
    structure.x_variable_count = static_cast<std::size_t>(opt.node_mapper.size());
    structure.z_variable_count = scenario.dpv.product_pairs.size();
    structure.y_copy_variable_count = opt.eligible_indices.size();
    structure.total_variable_count =
        structure.x_variable_count +
        structure.z_variable_count +
        structure.y_copy_variable_count;
    structure.y_fix_constraint_count = opt.eligible_indices.size();
    structure.ignition_constraint_count = 1;
    structure.propagation_constraint_count = scenario.arcs.size();
    structure.linearization_constraint_count = 3 * scenario.dpv.product_pairs.size();
    structure.total_constraint_count =
        structure.y_fix_constraint_count +
        structure.ignition_constraint_count +
        structure.propagation_constraint_count +
        structure.linearization_constraint_count;
    return structure;
}

#ifndef FIREBREAK_WITH_CPLEX

SubproblemResult DpvScenarioSubproblem::solve(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    const std::vector<int>& ybar_binary,
    bool) const {
    (void)analyze_dpv_scenario_subproblem_structure(opt, scenario_position);
    validate_ybar(opt, ybar_binary);
    throw std::runtime_error(solver::cplex_unavailable_message());
}

SubproblemResult DpvScenarioSubproblem::solveFractional(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    const std::vector<double>& ybar_values,
    bool) const {
    (void)analyze_dpv_scenario_subproblem_structure(opt, scenario_position);
    validate_fractional_ybar(opt, ybar_values);
    throw std::runtime_error(solver::cplex_unavailable_message());
}

#else

namespace {

SubproblemResult solve_dpv_scenario_subproblem_impl(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    const std::vector<double>& ybar_values,
    bool verbose) {
    const auto structure = analyze_dpv_scenario_subproblem_structure(opt, scenario_position);
    validate_fractional_ybar(opt, ybar_values);
    const auto& scenario = opt.scenarios[static_cast<std::size_t>(scenario_position)];
    const int node_count = opt.node_mapper.size();
    const auto y_position_by_node = build_y_position_by_node_index(opt);
    const auto compact_weights = opt::canonical_compact_dpv_weights_or_unit(opt);

    SubproblemResult result;
    result.scenario_id = scenario.scenario_id;
    result.num_variables = structure.total_variable_count;
    result.num_constraints = structure.total_constraint_count;

    IloEnv env;
    try {
        IloModel model(env);

        IloNumVarArray x(env, node_count, 0.0, 1.0, ILOFLOAT);
        for (int i = 0; i < node_count; ++i) {
            std::ostringstream name;
            name << "x_s" << scenario.scenario_id << "_" << i;
            x[i].setName(name.str().c_str());
        }

        const auto z_count = static_cast<IloInt>(scenario.dpv.product_pairs.size());
        IloNumVarArray z(env, z_count, 0.0, 1.0, ILOFLOAT);
        for (IloInt p = 0; p < z_count; ++p) {
            std::ostringstream name;
            name << "z_s" << scenario.scenario_id << "_" << p;
            z[p].setName(name.str().c_str());
        }

        IloNumVarArray y_copy(env, static_cast<IloInt>(opt.eligible_indices.size()), 0.0, 1.0, ILOFLOAT);
        for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
            std::ostringstream name;
            name << "ycopy_s" << scenario.scenario_id << "_" << opt.eligible_indices[pos];
            y_copy[static_cast<IloInt>(pos)].setName(name.str().c_str());
        }

        IloRangeArray y_fix(env);
        for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
            IloRange fix = (y_copy[static_cast<IloInt>(pos)] == ybar_values[pos]);
            std::ostringstream name;
            name << "fix_ycopy_s" << scenario.scenario_id << "_" << opt.eligible_indices[pos];
            fix.setName(name.str().c_str());
            y_fix.add(fix);
            model.add(fix);
        }

        IloExpr objective(env);
        for (IloInt p = 0; p < z.getSize(); ++p) {
            const auto& pair = scenario.dpv.product_pairs[static_cast<std::size_t>(p)];
            objective += compact_weights[static_cast<std::size_t>(pair.descendant_index)] * z[p];
        }
        model.add(IloMinimize(env, objective));
        objective.end();

        model.add(x[scenario.ignition_index] == 1.0);

        for (const auto& arc : scenario.arcs) {
            IloExpr expr(env);
            expr += x[arc.u_index];
            expr -= x[arc.v_index];
            const auto y_it = y_position_by_node.find(arc.v_index);
            if (y_it != y_position_by_node.end()) {
                expr -= y_copy[static_cast<IloInt>(y_it->second)];
            }
            model.add(expr <= 0.0);
            expr.end();
        }

        for (std::size_t p = 0; p < scenario.dpv.product_pairs.size(); ++p) {
            const auto& pair = scenario.dpv.product_pairs[p];
            const IloNumVar z_var = z[static_cast<IloInt>(p)];
            const IloNumVar x_successor = x[pair.successor_index];
            const IloNumVar x_descendant = x[pair.descendant_index];

            IloExpr lower(env);
            lower += x_successor;
            lower += x_descendant;
            lower -= 1.0;
            model.add(z_var >= lower);
            lower.end();

            model.add(z_var <= x_successor);
            model.add(z_var <= x_descendant);
        }

        IloCplex cplex(model);
        if (!verbose) {
            cplex.setOut(env.getNullStream());
            cplex.setWarning(env.getNullStream());
        }

        const auto start = std::chrono::steady_clock::now();
        const bool solved = cplex.solve();
        const auto end = std::chrono::steady_clock::now();
        result.runtime_seconds = std::chrono::duration<double>(end - start).count();

        std::ostringstream status;
        status << cplex.getStatus();
        result.status = solved ? status.str() : "No feasible LP solution";
        if (!solved) {
            throw std::runtime_error("DPV Benders subproblem did not solve to a feasible LP solution.");
        }

        result.objective_value = cplex.getObjValue();
        result.duals_for_y_copy.reserve(opt.eligible_indices.size());
        for (IloInt pos = 0; pos < y_fix.getSize(); ++pos) {
            result.duals_for_y_copy.push_back(cplex.getDual(y_fix[pos]));
        }

        BendersCut cut;
        cut.scenario_id = scenario.scenario_id;
        cut.subproblem_objective = result.objective_value;
        cut.coefficients_by_compact_index.reserve(opt.eligible_indices.size());
        cut.ybar_compact_values.reserve(opt.eligible_indices.size());
        double dual_dot_ybar = 0.0;
        for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
            const double dual = result.duals_for_y_copy[pos];
            const int compact_index = opt.eligible_indices[pos];
            const double ybar_value = ybar_values[pos];
            cut.coefficients_by_compact_index.push_back({compact_index, dual});
            cut.ybar_compact_values.push_back({compact_index, ybar_value});
            dual_dot_ybar += dual * ybar_value;
        }
        cut.rhs_constant = result.objective_value - dual_dot_ybar;
        cut.notes.push_back(
            "CPLEX getDual values from equality rows y_copy_i = ybar_i are used directly as Benders coefficients.");
        cut.notes.push_back(
            "The resulting cut is eta_s >= Q_s(ybar) + sum_i pi_i * (y_i - ybar_i).");
        cut.notes.push_back(
            "DPV subproblem objective uses weight(descendant) for each product pair; multiplicity is preserved.");
        result.benders_cut = cut;
        result.notes = cut.notes;

        env.end();
        return result;
    } catch (const IloException& exc) {
        std::string message = "CPLEX exception in DPV Benders subproblem: ";
        message += exc.getMessage();
        env.end();
        throw std::runtime_error(message);
    } catch (...) {
        env.end();
        throw;
    }
}

}  // namespace

SubproblemResult DpvScenarioSubproblem::solve(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    const std::vector<int>& ybar_binary,
    bool verbose) const {
    validate_ybar(opt, ybar_binary);
    std::vector<double> ybar_values;
    ybar_values.reserve(ybar_binary.size());
    for (const int value : ybar_binary) {
        ybar_values.push_back(static_cast<double>(value));
    }
    return solve_dpv_scenario_subproblem_impl(opt, scenario_position, ybar_values, verbose);
}

SubproblemResult DpvScenarioSubproblem::solveFractional(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    const std::vector<double>& ybar_values,
    bool verbose) const {
    return solve_dpv_scenario_subproblem_impl(opt, scenario_position, ybar_values, verbose);
}

#endif

}  // namespace firebreak::benders
