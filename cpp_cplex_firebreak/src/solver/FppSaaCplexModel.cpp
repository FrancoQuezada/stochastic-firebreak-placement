#include "solver/FppSaaCplexModel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
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

void validate_opt_for_fpp_saa(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("FPP-SAA requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("FPP-SAA requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("FPP-SAA requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0) {
        throw std::runtime_error("FPP-SAA budget must be nonnegative.");
    }
    if (opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("FPP-SAA budget exceeds the number of eligible firebreak nodes.");
    }
}

#ifdef FIREBREAK_WITH_CPLEX
double scenario_probability_at(
    const opt::OptimizationInstance& opt,
    std::size_t scenario_index) {
    const double scenario_probability = opt.scenarios[scenario_index].probability;
    if (std::isfinite(scenario_probability)) {
        return scenario_probability;
    }
    if (scenario_index < opt.scenario_probabilities.size() &&
        std::isfinite(opt.scenario_probabilities[scenario_index])) {
        return opt.scenario_probabilities[scenario_index];
    }
    throw std::runtime_error("FPP-SAA formulation found a nonfinite scenario probability.");
}

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

bool FppSaaModelStructure::has_y_for_node_index(int node_index) const {
    return std::find(y_indices.begin(), y_indices.end(), node_index) != y_indices.end();
}

FppSaaModelStructure analyze_fpp_saa_model_structure(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config) {
    validate_opt_for_fpp_saa(opt);
    risk::validate_risk_measure_config(risk_config);

    FppSaaModelStructure structure;
    const auto node_count = static_cast<std::size_t>(opt.node_mapper.size());
    const auto scenario_count = opt.scenarios.size();
    const bool uses_cvar =
        risk_config.type == risk::RiskMeasureType::CVaR ||
        risk_config.type == risk::RiskMeasureType::MeanCVaR;

    structure.x_variable_count = node_count * scenario_count;
    structure.y_variable_count = opt.eligible_indices.size();
    if (uses_cvar) {
        structure.risk_threshold_variable_count = 1;
        structure.cvar_excess_variable_count = scenario_count;
        structure.cvar_excess_constraint_count = scenario_count;
    }
    structure.total_variable_count =
        structure.x_variable_count +
        structure.y_variable_count +
        structure.risk_threshold_variable_count +
        structure.cvar_excess_variable_count;
    structure.budget_constraint_count = 1;
    structure.ignition_constraint_count = scenario_count;
    structure.propagation_constraint_count = opt.total_arcs;
    structure.total_constraint_count =
        structure.budget_constraint_count +
        structure.ignition_constraint_count +
        structure.propagation_constraint_count +
        structure.cvar_excess_constraint_count;
    structure.y_indices = opt.eligible_indices;

    const auto y_position_by_node = build_y_position_by_node_index(opt);
    structure.propagation_constraints.reserve(opt.total_arcs);
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
    }

    return structure;
}

FppSaaModelStructure analyze_fpp_saa_model_structure(const opt::OptimizationInstance& opt) {
    return analyze_fpp_saa_model_structure(opt, risk::RiskMeasureConfig());
}

#ifndef FIREBREAK_WITH_CPLEX

ModelResult FppSaaCplexModel::solve(
    const opt::OptimizationInstance& opt,
    double,
    double,
    int,
    bool,
    const WarmStart*,
    const cuts::DominatorCutOptions* dominator_options,
    const cuts::SeparatorCutOptions* separator_options,
    const risk::RiskMeasureConfig& risk_config,
    const benders::FppStrengtheningOptions* strengthening_options) const {
    (void)dominator_options;
    (void)separator_options;
    (void)strengthening_options;
    (void)analyze_fpp_saa_model_structure(opt, risk_config);
    throw std::runtime_error(cplex_unavailable_message());
}

#else

ModelResult FppSaaCplexModel::solve(
    const opt::OptimizationInstance& opt,
    double time_limit_seconds,
    double mip_gap,
    int threads,
    bool verbose,
    const WarmStart* warm_start,
    const cuts::DominatorCutOptions* dominator_options,
    const cuts::SeparatorCutOptions* separator_options,
    const risk::RiskMeasureConfig& risk_config,
    const benders::FppStrengtheningOptions* strengthening_options) const {
    risk::RiskMeasureConfig effective_risk_config = risk_config;
    if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
        effective_risk_config.cvarLambda = 1.0;
    }
    const auto structure = analyze_fpp_saa_model_structure(opt, effective_risk_config);
    const int node_count = opt.node_mapper.size();
    const bool uses_cvar =
        effective_risk_config.type == risk::RiskMeasureType::CVaR ||
        effective_risk_config.type == risk::RiskMeasureType::MeanCVaR;
    const double cvar_tail_scale = 1.0 / (1.0 - effective_risk_config.cvarBeta);

    ModelResult result;
    result.method = "FPP-SAA direct CPLEX";
    result.risk_measure = risk::to_string(effective_risk_config.type);
    result.cvar_beta = effective_risk_config.cvarBeta;
    result.cvar_lambda = effective_risk_config.cvarLambda;
    result.num_variables = structure.total_variable_count;
    result.num_constraints = structure.total_constraint_count;
    if (strengthening_options) {
        result.coverage_llbi_enabled = strengthening_options->use_coverage_llbi;
        result.path_llbi_enabled = strengthening_options->use_path_llbi;
        result.global_dominance_enabled =
            strengthening_options->use_global_dominance_preprocessing;
        result.conditional_zero_benefit_enabled =
            strengthening_options->use_conditional_zero_benefit_fixing;
        if (strengthening_options->use_coverage_llbi ||
            strengthening_options->use_path_llbi) {
            result.notes.push_back(
                "Coverage/path LLBI are redundant in direct FPP-SAA because the model already contains scenario burn variables and propagation constraints; no auxiliary eta strengthening variables were added.");
        }
    }

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

        IloNumVar risk_threshold;
        IloNumVarArray cvar_excess(env);
        if (uses_cvar) {
            const double max_scenario_loss = static_cast<double>(node_count);
            risk_threshold = IloNumVar(env, 0.0, max_scenario_loss, ILOFLOAT);
            risk_threshold.setName("risk_threshold");
            cvar_excess = IloNumVarArray(env, static_cast<IloInt>(opt.scenarios.size()));
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                cvar_excess[static_cast<IloInt>(s)] =
                    IloNumVar(env, 0.0, max_scenario_loss, ILOFLOAT);
                std::ostringstream name;
                name << "cvar_excess_s" << opt.scenarios[s].scenario_id;
                cvar_excess[static_cast<IloInt>(s)].setName(name.str().c_str());
            }
        }

        IloExpr objective(env);
        if (!uses_cvar) {
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                const double probability = scenario_probability_at(opt, s);
                for (int i = 0; i < node_count; ++i) {
                    objective += probability * x[s][i];
                }
            }
        } else {
            IloExpr cvar_tail(env);
            const bool include_expected_term =
                effective_risk_config.type == risk::RiskMeasureType::MeanCVaR;
            const double expected_weight = include_expected_term
                ? (1.0 - effective_risk_config.cvarLambda)
                : 0.0;
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                const double probability = scenario_probability_at(opt, s);
                if (include_expected_term && expected_weight != 0.0) {
                    for (int i = 0; i < node_count; ++i) {
                        objective += expected_weight * probability * x[s][i];
                    }
                }
                cvar_tail += probability * cvar_excess[static_cast<IloInt>(s)];
            }
            objective +=
                effective_risk_config.cvarLambda * risk_threshold +
                effective_risk_config.cvarLambda * cvar_tail_scale * cvar_tail;
            cvar_tail.end();
        }
        model.add(IloMinimize(env, objective));
        objective.end();

        if (uses_cvar) {
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                IloExpr loss_minus_threshold(env);
                for (int i = 0; i < node_count; ++i) {
                    loss_minus_threshold += x[s][i];
                }
                loss_minus_threshold -= risk_threshold;
                model.add(cvar_excess[static_cast<IloInt>(s)] >= loss_minus_threshold);
                loss_minus_threshold.end();
            }
        }

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

        if (dominator_options && dominator_options->enabled) {
            cuts::DominatorVariableAccess access;
            access.has_x = [&](std::size_t scenario_index, int compact_node) {
                return scenario_index < x.size() && compact_node >= 0 && compact_node < node_count;
            };
            access.get_x = [&](std::size_t scenario_index, int compact_node) -> IloNumVar {
                return x[scenario_index][compact_node];
            };
            access.has_y = [&](int compact_node) {
                return compact_node >= 0 &&
                       compact_node < static_cast<int>(y_position_by_node.size()) &&
                       y_position_by_node[static_cast<std::size_t>(compact_node)] >= 0;
            };
            access.get_y = [&](int compact_node) -> IloNumVar {
                return y[y_position_by_node[static_cast<std::size_t>(compact_node)]];
            };
            access.skip_self_individual_cuts = false;

            const auto dominator_stats = cuts::add_dominator_cuts_to_model(
                env,
                model,
                opt,
                *dominator_options,
                access);
            result.dominator_cuts_added = dominator_stats.total_cuts_added();
            result.dominator_aggregate_cuts_added = dominator_stats.aggregate_cuts_added;
            result.dominator_individual_cuts_added = dominator_stats.individual_cuts_added;
            result.dominator_dag_scenarios = dominator_stats.dag_scenarios;
            result.dominator_fallback_scenarios = dominator_stats.fallback_scenarios;
            result.dominator_preprocessing_time_sec = dominator_stats.preprocessing_time_sec;
            result.num_constraints += static_cast<std::size_t>(dominator_stats.total_cuts_added());
            result.notes.insert(
                result.notes.end(),
                dominator_stats.notes.begin(),
                dominator_stats.notes.end());
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
        if (separator_options && separator_options->enabled) {
            cplex.setParam(IloCplex::Param::Threads, 1);
            result.notes.push_back(
                "Separator context callback enabled; CPLEX Threads forced to 1 for deterministic cut-cache safety.");
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
                        result.mip_start_accepted = true;
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

        std::unique_ptr<cuts::SeparatorContextCallback> separator_callback;
        if (separator_options && separator_options->enabled) {
            cuts::SeparatorVariableAccess separator_access;
            separator_access.y_vars = y.toNumVarArray();
            separator_access.y_position_by_node = y_position_by_node;
            separator_access.x_vars_by_scenario.reserve(x.size());
            separator_access.x_position_by_scenario.reserve(x.size());
            for (std::size_t s = 0; s < x.size(); ++s) {
                separator_access.x_vars_by_scenario.push_back(x[s].toNumVarArray());
                std::vector<int> x_position(static_cast<std::size_t>(node_count), -1);
                for (int compact_node = 0; compact_node < node_count; ++compact_node) {
                    x_position[static_cast<std::size_t>(compact_node)] = compact_node;
                }
                separator_access.x_position_by_scenario.push_back(std::move(x_position));
            }

            separator_callback = std::make_unique<cuts::SeparatorContextCallback>(
                opt,
                *separator_options,
                std::move(separator_access));
            cplex.use(separator_callback.get(), IloCplex::Callback::Context::Id::Relaxation);
            result.separator_cuts_enabled = true;
            result.notes.push_back(
                "Separator cuts registered through the CPLEX generic relaxation context callback.");
        }

        const auto start = std::chrono::steady_clock::now();
        const bool solved = cplex.solve();
        const auto end = std::chrono::steady_clock::now();
        result.runtime_seconds = std::chrono::duration<double>(end - start).count();

        if (separator_callback) {
            const auto& separator_stats = separator_callback->stats();
            result.separator_cuts_added = separator_stats.cuts_added;
            result.separator_min_cut_calls = separator_stats.min_cut_calls;
            result.separator_callback_invocations = separator_stats.callback_invocations;
            result.separator_duplicate_cuts_skipped = separator_stats.duplicate_cuts_skipped;
            result.separator_large_cuts_skipped = separator_stats.large_cuts_skipped;
            result.separator_time_sec = separator_stats.separator_time_sec;
            result.max_cut_violation =
                std::max(result.max_cut_violation, separator_stats.max_cut_violation);
            result.cuts_added += separator_stats.cuts_added;
            result.num_constraints += static_cast<std::size_t>(separator_stats.cuts_added);
            result.notes.push_back(
                "Separator callback stats: invocations=" +
                std::to_string(separator_stats.callback_invocations) +
                ", min_cut_calls=" + std::to_string(separator_stats.min_cut_calls) +
                ", cuts_added=" + std::to_string(separator_stats.cuts_added) +
                ", duplicates_skipped=" + std::to_string(separator_stats.duplicate_cuts_skipped) +
                ", large_cuts_skipped=" + std::to_string(separator_stats.large_cuts_skipped) + ".");
        }

        std::ostringstream status;
        status << cplex.getStatus();
        result.status = solved ? status.str() : "No feasible solution";
        result.solver_status_code = static_cast<int>(cplex.getCplexStatus());
        result.explored_nodes = static_cast<long long>(cplex.getNnodes());
        result.best_bound = cplex.getBestObjValue();
        if (solved) {
            result.objective_value = cplex.getObjValue();
            result.mip_gap = cplex.getMIPRelativeGap();

            std::vector<risk::WeightedLoss> scenario_losses;
            scenario_losses.reserve(opt.scenarios.size());
            double expected_loss_component = 0.0;
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                double scenario_loss = 0.0;
                for (int i = 0; i < node_count; ++i) {
                    scenario_loss += cplex.getValue(x[s][i]);
                }
                const double probability = scenario_probability_at(opt, s);
                expected_loss_component += probability * scenario_loss;
                scenario_losses.push_back({
                    opt.scenarios[s].scenario_id,
                    scenario_loss,
                    probability,
                });
            }
            result.expected_loss_component = expected_loss_component;
            if (uses_cvar) {
                result.risk_threshold_value = cplex.getValue(risk_threshold);
                const auto metrics = risk::compute_weighted_risk_metrics(
                    scenario_losses,
                    effective_risk_config.cvarBeta);
                result.cvar_loss_component = metrics.cvar;
            }

            for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
                if (cplex.getValue(y[static_cast<IloInt>(pos)]) > 0.5) {
                    const int compact_index = opt.eligible_indices[pos];
                    result.selected_firebreak_indices.push_back(compact_index);
                    result.selected_firebreak_original_nodes.push_back(opt.node_mapper.to_node(compact_index));
                }
            }
        }

        if (effective_risk_config.type == risk::RiskMeasureType::Expected) {
            result.notes.push_back("FPP-SAA objective is expected burned area over loaded scenarios.");
        } else if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
            result.notes.push_back("FPP-SAA objective is pure CVaR of burned area over loaded scenarios.");
        } else {
            result.notes.push_back("FPP-SAA objective is a mean-CVaR blend of burned area over loaded scenarios.");
        }
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
