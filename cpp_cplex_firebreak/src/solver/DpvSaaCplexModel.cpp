#include "solver/DpvSaaCplexModel.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::solver {

namespace {

std::unordered_map<int, int> build_y_position_by_node_index(const opt::OptimizationInstance& opt) {
    std::unordered_map<int, int> y_position_by_node;
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        y_position_by_node[opt.eligible_indices[pos]] = static_cast<int>(pos);
    }
    return y_position_by_node;
}

void validate_opt_for_dpv_saa(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("DPV-SAA requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("DPV-SAA requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("DPV-SAA requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0) {
        throw std::runtime_error("DPV-SAA budget must be nonnegative.");
    }
    if (opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("DPV-SAA budget exceeds the number of eligible firebreak nodes.");
    }
    std::size_t counted_pairs = 0;
    for (const auto& scenario : opt.scenarios) {
        counted_pairs += scenario.dpv.product_pairs.size();
    }
    if (counted_pairs != opt.total_dpv_pairs) {
        throw std::runtime_error("DPV-SAA total_dpv_pairs does not match scenario DPV product-pair counts.");
    }
}

#ifdef FIREBREAK_WITH_CPLEX
void attach_warm_start_metadata(ModelResult& result, const WarmStart& warm_start) {
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
#endif

}  // namespace

bool DpvSaaModelStructure::has_y_for_node_index(int node_index) const {
    return std::find(y_indices.begin(), y_indices.end(), node_index) != y_indices.end();
}

DpvSaaModelStructure analyze_dpv_saa_model_structure(const opt::OptimizationInstance& opt) {
    validate_opt_for_dpv_saa(opt);

    DpvSaaModelStructure structure;
    const auto node_count = static_cast<std::size_t>(opt.node_mapper.size());
    const auto scenario_count = opt.scenarios.size();

    structure.x_variable_count = node_count * scenario_count;
    structure.y_variable_count = opt.eligible_indices.size();
    structure.z_variable_count = opt.total_dpv_pairs;
    structure.total_variable_count =
        structure.x_variable_count +
        structure.y_variable_count +
        structure.z_variable_count;
    structure.budget_constraint_count = 1;
    structure.ignition_constraint_count = scenario_count;
    structure.propagation_constraint_count = opt.total_arcs;
    structure.linearization_constraint_count = 3 * opt.total_dpv_pairs;
    structure.total_constraint_count =
        structure.budget_constraint_count +
        structure.ignition_constraint_count +
        structure.propagation_constraint_count +
        structure.linearization_constraint_count;
    structure.y_indices = opt.eligible_indices;

    const auto y_position_by_node = build_y_position_by_node_index(opt);
    structure.propagation_constraints.reserve(opt.total_arcs);
    structure.objective_terms.reserve(opt.total_dpv_pairs);
    for (const auto& scenario : opt.scenarios) {
        for (const auto& arc : scenario.arcs) {
            const auto y_it = y_position_by_node.find(arc.v_index);
            structure.propagation_constraints.push_back(PropagationConstraintDescriptor{
                scenario.scenario_id,
                arc.u_index,
                arc.v_index,
                y_it != y_position_by_node.end(),
                y_it == y_position_by_node.end() ? -1 : y_it->second,
            });
        }
        for (const auto& pair : scenario.dpv.product_pairs) {
            structure.objective_terms.push_back(DpvObjectiveTermDescriptor{
                scenario.scenario_id,
                pair.source_index,
                pair.successor_index,
                pair.descendant_index,
            });
        }
    }

    return structure;
}

#ifndef FIREBREAK_WITH_CPLEX

ModelResult DpvSaaCplexModel::solve(
    const opt::OptimizationInstance& opt,
    double,
    double,
    int,
    bool,
    const WarmStart*) const {
    (void)analyze_dpv_saa_model_structure(opt);
    throw std::runtime_error(cplex_unavailable_message());
}

#else

ModelResult DpvSaaCplexModel::solve(
    const opt::OptimizationInstance& opt,
    double time_limit_seconds,
    double mip_gap,
    int threads,
    bool verbose,
    const WarmStart* warm_start) const {
    const auto structure = analyze_dpv_saa_model_structure(opt);
    const int node_count = opt.node_mapper.size();

    ModelResult result;
    result.method = "DPV-SAA direct CPLEX";
    result.num_variables = structure.total_variable_count;
    result.num_constraints = structure.total_constraint_count;

    IloEnv env;
    try {
        IloModel model(env);

        IloBoolVarArray y(env, static_cast<IloInt>(opt.eligible_indices.size()));
        std::vector<int> y_position_by_node(static_cast<std::size_t>(node_count), -1);
        for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
            const int node_index = opt.eligible_indices[pos];
            y_position_by_node[static_cast<std::size_t>(node_index)] = static_cast<int>(pos);
            std::ostringstream name;
            name << "y_" << node_index;
            y[static_cast<IloInt>(pos)].setName(name.str().c_str());
        }

        std::vector<IloBoolVarArray> x;
        x.reserve(opt.scenarios.size());
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            IloBoolVarArray x_s(env, node_count);
            for (int i = 0; i < node_count; ++i) {
                std::ostringstream name;
                name << "x_s" << opt.scenarios[s].scenario_id << "_" << i;
                x_s[i].setName(name.str().c_str());
            }
            x.push_back(x_s);
        }

        std::vector<IloNumVarArray> z;
        z.reserve(opt.scenarios.size());
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            const auto z_count = static_cast<IloInt>(opt.scenarios[s].dpv.product_pairs.size());
            IloNumVarArray z_s(env, z_count, 0.0, 1.0, ILOFLOAT);
            for (IloInt p = 0; p < z_count; ++p) {
                std::ostringstream name;
                name << "z_s" << opt.scenarios[s].scenario_id << "_" << p;
                z_s[p].setName(name.str().c_str());
            }
            z.push_back(z_s);
        }

        IloExpr objective(env);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            const double probability = opt.scenarios[s].probability;
            for (IloInt p = 0; p < z[s].getSize(); ++p) {
                objective += probability * z[s][p];
            }
        }
        model.add(IloMinimize(env, objective));
        objective.end();

        IloExpr budget_expr(env);
        for (IloInt pos = 0; pos < y.getSize(); ++pos) {
            budget_expr += y[pos];
        }
        model.add(budget_expr <= opt.budget);
        budget_expr.end();

        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            model.add(x[s][opt.scenarios[s].ignition_index] == 1);
        }

        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            for (const auto& arc : opt.scenarios[s].arcs) {
                IloExpr expr(env);
                expr += x[s][arc.u_index];
                expr -= x[s][arc.v_index];
                const int y_pos = y_position_by_node[static_cast<std::size_t>(arc.v_index)];
                if (y_pos >= 0) {
                    expr -= y[y_pos];
                }
                model.add(expr <= 0);
                expr.end();
            }
        }

        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            for (std::size_t p = 0; p < opt.scenarios[s].dpv.product_pairs.size(); ++p) {
                const auto& pair = opt.scenarios[s].dpv.product_pairs[p];
                const IloNumVar z_var = z[s][static_cast<IloInt>(p)];
                const IloBoolVar x_successor = x[s][pair.successor_index];
                const IloBoolVar x_descendant = x[s][pair.descendant_index];

                IloExpr lower(env);
                lower += x_successor;
                lower += x_descendant;
                lower -= 1.0;
                model.add(z_var >= lower);
                lower.end();

                model.add(z_var <= x_successor);
                model.add(z_var <= x_descendant);
            }
        }

        IloCplex cplex(model);
        if (!verbose) {
            cplex.setOut(env.getNullStream());
            cplex.setWarning(env.getNullStream());
        }
        if (time_limit_seconds > 0.0) {
            cplex.setParam(IloCplex::Param::TimeLimit, time_limit_seconds);
        }
        if (mip_gap >= 0.0) {
            cplex.setParam(IloCplex::Param::MIP::Tolerances::MIPGap, mip_gap);
        }
        if (threads > 0) {
            cplex.setParam(IloCplex::Param::Threads, threads);
        }

        if (warm_start && warm_start->enabled) {
            attach_warm_start_metadata(result, *warm_start);
            if (warm_start->compact_indices.empty()) {
                result.notes.push_back("Warm-start file was provided but no valid eligible nodes were available; solving without MIP start.");
            } else {
                try {
                    IloNumVarArray start_vars(env);
                    IloNumArray start_vals(env);
                    for (const int compact_index : warm_start->compact_indices) {
                        const int y_pos = y_position_by_node[static_cast<std::size_t>(compact_index)];
                        if (y_pos >= 0) {
                            start_vars.add(y[y_pos]);
                            start_vals.add(1.0);
                        }
                    }
                    if (start_vars.getSize() > 0) {
                        cplex.addMIPStart(start_vars, start_vals);
                        result.warm_start_used = true;
                        result.notes.push_back("CPLEX y-only MIP start was added from warm-start solution.");
                    } else {
                        result.notes.push_back("Warm-start nodes did not match y variables; solving without MIP start.");
                    }
                    start_vals.end();
                    start_vars.end();
                } catch (const IloException& exc) {
                    std::string warning = "CPLEX rejected warm-start MIP start: ";
                    warning += exc.getMessage();
                    result.notes.push_back(warning);
                }
            }
        }

        const auto start = std::chrono::steady_clock::now();
        const bool solved = cplex.solve();
        const auto end = std::chrono::steady_clock::now();
        result.runtime_seconds = std::chrono::duration<double>(end - start).count();

        std::ostringstream status;
        status << cplex.getStatus();
        result.status = solved ? status.str() : "No feasible solution";
        result.solver_status_code = static_cast<int>(cplex.getCplexStatus());
        result.best_bound = cplex.getBestObjValue();
        if (solved) {
            result.objective_value = cplex.getObjValue();
            result.mip_gap = cplex.getMIPRelativeGap();

            for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
                if (cplex.getValue(y[static_cast<IloInt>(pos)]) > 0.5) {
                    const int compact_index = opt.eligible_indices[pos];
                    result.selected_firebreak_indices.push_back(compact_index);
                    result.selected_firebreak_original_nodes.push_back(opt.node_mapper.to_node(compact_index));
                }
            }
        }

        result.notes.push_back("DPV-SAA objective is solution-dependent DPV with unit weights.");
        result.notes.push_back("DPV product-pair multiplicity is preserved.");
        result.notes.push_back("z variables are continuous in [0,1].");
        result.notes.push_back("Propagation constraints use y_v only when v is eligible.");
        env.end();
        return result;
    } catch (const IloException& exc) {
        std::string message = "CPLEX exception: ";
        message += exc.getMessage();
        env.end();
        throw std::runtime_error(message);
    } catch (...) {
        env.end();
        throw;
    }
}

#endif

}  // namespace firebreak::solver
