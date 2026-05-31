#include "solver/FppCutReachabilityCplexModel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "eval/FppRecourseEvaluator.hpp"
#include "risk/RiskMeasure.hpp"
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

void ensure_node_in_range(int node, int node_count, const std::string& context) {
    if (node < 0 || node >= node_count) {
        throw std::runtime_error(context + " compact node index is out of range.");
    }
}

void validate_opt_for_fpp_cut(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("FPP cut/reachability formulation requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("FPP cut/reachability formulation requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("FPP cut/reachability formulation requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0) {
        throw std::runtime_error("FPP cut/reachability formulation budget must be nonnegative.");
    }
    if (opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error(
            "FPP cut/reachability formulation budget exceeds the number of eligible firebreak nodes.");
    }

    const int node_count = opt.node_mapper.size();
    for (const int compact_node : opt.eligible_indices) {
        ensure_node_in_range(compact_node, node_count, "Eligible firebreak");
    }
    for (const auto& scenario : opt.scenarios) {
        ensure_node_in_range(scenario.ignition_index, node_count, "Ignition");
        for (const int compact_node : scenario.observed_node_indices) {
            ensure_node_in_range(compact_node, node_count, "Observed scenario");
        }
        for (const auto& arc : scenario.arcs) {
            ensure_node_in_range(arc.u_index, node_count, "Arc source");
            ensure_node_in_range(arc.v_index, node_count, "Arc target");
        }
    }
}

std::vector<int> scenario_node_set(const opt::OptimizationScenario& scenario) {
    std::vector<int> nodes = scenario.observed_node_indices;
    nodes.push_back(scenario.ignition_index);
    for (const auto& arc : scenario.arcs) {
        nodes.push_back(arc.u_index);
        nodes.push_back(arc.v_index);
    }
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    return nodes;
}

std::vector<std::pair<int, int>> unique_arcs(const opt::OptimizationScenario& scenario) {
    std::vector<std::pair<int, int>> arcs;
    arcs.reserve(scenario.arcs.size());
    for (const auto& arc : scenario.arcs) {
        arcs.emplace_back(arc.u_index, arc.v_index);
    }
    std::sort(arcs.begin(), arcs.end());
    arcs.erase(std::unique(arcs.begin(), arcs.end()), arcs.end());
    return arcs;
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
    throw std::runtime_error("FPP cut/reachability formulation found a nonfinite scenario probability.");
}

std::vector<std::vector<int>> build_scenario_nodes_by_position(const opt::OptimizationInstance& opt) {
    std::vector<std::vector<int>> nodes_by_scenario;
    nodes_by_scenario.reserve(opt.scenarios.size());
    for (const auto& scenario : opt.scenarios) {
        nodes_by_scenario.push_back(scenario_node_set(scenario));
    }
    return nodes_by_scenario;
}

std::vector<std::vector<int>> build_node_position_by_scenario(
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

void add_full_cut_reachability_mip_start(
    IloEnv& env,
    IloCplex& cplex,
    const opt::OptimizationInstance& opt,
    const WarmStart& warm_start,
    const std::vector<int>& y_position_by_node,
    const IloBoolVarArray& y,
    const std::vector<IloNumVarArray>& x,
    const std::vector<IloNumVarArray>& q,
    const std::vector<std::vector<int>>& nodes_by_scenario,
    ModelResult& result) {
    attach_warm_start_metadata(result, warm_start);
    if (warm_start.compact_indices.empty()) {
        result.notes.push_back(
            "Warm-start file was provided but no valid eligible nodes were available; solving without MIP start.");
        return;
    }

    const auto start_values =
        build_fpp_cut_reachability_mip_start_values(opt, warm_start.compact_indices);
    result.notes.insert(
        result.notes.end(),
        start_values.notes.begin(),
        start_values.notes.end());
    if (!start_values.feasible) {
        result.notes.push_back(
            "Cut/reachability full MIP start failed feasibility validation; solving without MIP start.");
        return;
    }

    try {
        IloNumVarArray start_vars(env);
        IloNumArray start_vals(env);

        for (std::size_t compact_node = 0; compact_node < y_position_by_node.size(); ++compact_node) {
            const int y_pos = y_position_by_node[compact_node];
            if (y_pos < 0) {
                continue;
            }
            start_vars.add(y[static_cast<IloInt>(y_pos)]);
            const char selected =
                compact_node < start_values.y_selected_by_compact_node.size()
                    ? start_values.y_selected_by_compact_node[compact_node]
                    : 0;
            start_vals.add(selected ? 1.0 : 0.0);
        }

        for (std::size_t s = 0; s < nodes_by_scenario.size(); ++s) {
            for (std::size_t local_pos = 0; local_pos < nodes_by_scenario[s].size(); ++local_pos) {
                start_vars.add(x[s][static_cast<IloInt>(local_pos)]);
                start_vals.add(
                    start_values.x_burned_by_scenario[s][local_pos] ? 1.0 : 0.0);

                start_vars.add(q[s][static_cast<IloInt>(local_pos)]);
                start_vals.add(
                    start_values.q_reached_by_scenario[s][local_pos] ? 1.0 : 0.0);
            }
        }

        if (start_vars.getSize() > 0) {
            cplex.addMIPStart(start_vars, start_vals);
            result.warm_start_used = true;
            result.mip_start_accepted = true;
            std::ostringstream note;
            note << "CPLEX full cut/reachability MIP start was added from warm-start solution "
                 << "with y/x/q values and recourse objective "
                 << start_values.recourse_objective << ".";
            result.notes.push_back(note.str());
        } else {
            result.notes.push_back("Warm start did not produce any model variables; solving without MIP start.");
        }
        start_vals.end();
        start_vars.end();
    } catch (const IloException& exc) {
        std::string warning = "CPLEX rejected warm-start MIP start: ";
        warning += exc.getMessage();
        result.notes.push_back(warning);
    }
}
#endif

}  // namespace

bool FppCutReachabilityModelStructure::has_y_for_node_index(int node_index) const {
    return std::find(y_indices.begin(), y_indices.end(), node_index) != y_indices.end();
}

FppCutReachabilityModelStructure analyze_fpp_cut_reachability_model_structure(
    const opt::OptimizationInstance& opt) {
    validate_opt_for_fpp_cut(opt);

    FppCutReachabilityModelStructure structure;
    structure.y_variable_count = opt.eligible_indices.size();
    structure.budget_constraint_count = 1;
    structure.root_constraint_count = 2 * opt.scenarios.size();
    structure.y_indices = opt.eligible_indices;

    const auto y_position_by_node = build_y_position_by_node_index(opt);
    for (const auto& scenario : opt.scenarios) {
        const auto nodes = scenario_node_set(scenario);
        const auto arcs = unique_arcs(scenario);

        structure.observed_node_count_by_scenario.push_back(nodes.size());
        structure.x_variable_count += nodes.size();
        structure.q_variable_count += nodes.size();
        structure.propagation_entrance_constraint_count += arcs.size();

        for (const auto& arc : arcs) {
            structure.propagation_constraints.push_back(CutPropagationConstraintDescriptor{
                scenario.scenario_id,
                arc.first,
                arc.second,
            });
        }

        for (const int compact_node : nodes) {
            if (compact_node == scenario.ignition_index) {
                continue;
            }
            const auto y_it = y_position_by_node.find(compact_node);
            const bool has_y = y_it != y_position_by_node.end();
            const int y_pos = has_y ? y_it->second : -1;
            structure.pass_through_constraints.push_back(CutPassThroughConstraintDescriptor{
                scenario.scenario_id,
                compact_node,
                has_y,
                y_pos,
            });
            ++structure.pass_through_constraint_count;
            if (has_y) {
                structure.firebreak_upper_bound_constraints.push_back(CutPassThroughConstraintDescriptor{
                    scenario.scenario_id,
                    compact_node,
                    true,
                    y_pos,
                });
                ++structure.firebreak_upper_bound_constraint_count;
            }
        }
    }

    structure.total_variable_count =
        structure.y_variable_count +
        structure.x_variable_count +
        structure.q_variable_count;
    structure.total_constraint_count =
        structure.budget_constraint_count +
        structure.root_constraint_count +
        structure.propagation_entrance_constraint_count +
        structure.pass_through_constraint_count +
        structure.firebreak_upper_bound_constraint_count;
    return structure;
}

FppCutReachabilityMipStartValues build_fpp_cut_reachability_mip_start_values(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& selected_firebreak_compact_nodes) {
    validate_opt_for_fpp_cut(opt);

    constexpr double kTolerance = 1.0e-9;
    const int node_count = opt.node_mapper.size();
    const auto y_position_by_node = build_y_position_by_node_index(opt);

    FppCutReachabilityMipStartValues values;
    values.y_selected_by_compact_node.assign(static_cast<std::size_t>(node_count), 0);
    values.nodes_by_scenario.reserve(opt.scenarios.size());
    values.x_burned_by_scenario.reserve(opt.scenarios.size());
    values.q_reached_by_scenario.reserve(opt.scenarios.size());

    auto mark_invalid = [&](const std::string& note) {
        values.feasible = false;
        values.notes.push_back(note);
    };

    std::unordered_set<int> seen_selected;
    int selected_count = 0;
    for (const int compact_node : selected_firebreak_compact_nodes) {
        if (compact_node < 0 || compact_node >= node_count) {
            mark_invalid(
                "Cut/reachability MIP start contains out-of-range selected compact node " +
                std::to_string(compact_node) + ".");
            continue;
        }
        if (!seen_selected.insert(compact_node).second) {
            continue;
        }
        if (y_position_by_node.find(compact_node) == y_position_by_node.end()) {
            mark_invalid(
                "Cut/reachability MIP start contains non-eligible selected compact node " +
                std::to_string(compact_node) + ".");
            continue;
        }
        values.y_selected_by_compact_node[static_cast<std::size_t>(compact_node)] = 1;
        ++selected_count;
    }
    if (selected_count > opt.budget) {
        mark_invalid("Cut/reachability MIP start selected nodes exceed the firebreak budget.");
    }

    eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluateFromBinaryVector(values.y_selected_by_compact_node, false);
    values.recourse_objective = recourse.expected_burned_area;
    values.notes.insert(values.notes.end(), recourse.warnings.begin(), recourse.warnings.end());

    // Reuse the canonical FPP traversal to populate the cut formulation state:
    // q records entrance reachability, while x records actual burning after
    // non-root firebreaks stop propagation.
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const auto nodes = scenario_node_set(opt.scenarios[s]);
        std::vector<char> x_s;
        std::vector<char> q_s;
        x_s.reserve(nodes.size());
        q_s.reserve(nodes.size());
        for (const int compact_node : nodes) {
            x_s.push_back(evaluator.isBurned(static_cast<int>(s), compact_node) ? 1 : 0);
            q_s.push_back(evaluator.isReached(static_cast<int>(s), compact_node) ? 1 : 0);
        }
        values.nodes_by_scenario.push_back(nodes);
        values.x_burned_by_scenario.push_back(std::move(x_s));
        values.q_reached_by_scenario.push_back(std::move(q_s));
    }

    // These checks mirror the cut/reachability constraints before the start is
    // handed to CPLEX, so an internal evaluator/model mismatch is caught early.
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const auto& scenario = opt.scenarios[s];
        const auto& nodes = values.nodes_by_scenario[s];
        auto local_position = [&](int compact_node) -> int {
            const auto it = std::find(nodes.begin(), nodes.end(), compact_node);
            return it == nodes.end() ? -1 : static_cast<int>(std::distance(nodes.begin(), it));
        };
        auto x_value = [&](int compact_node) -> double {
            const int pos = local_position(compact_node);
            return pos < 0 ? 0.0 : static_cast<double>(values.x_burned_by_scenario[s][static_cast<std::size_t>(pos)]);
        };
        auto q_value = [&](int compact_node) -> double {
            const int pos = local_position(compact_node);
            return pos < 0 ? 0.0 : static_cast<double>(values.q_reached_by_scenario[s][static_cast<std::size_t>(pos)]);
        };
        auto y_value = [&](int compact_node) -> double {
            if (compact_node < 0 || compact_node >= node_count) {
                return 0.0;
            }
            return static_cast<double>(values.y_selected_by_compact_node[static_cast<std::size_t>(compact_node)]);
        };

        const int root = scenario.ignition_index;
        const int root_pos = local_position(root);
        if (root_pos < 0) {
            mark_invalid(
                "Cut/reachability MIP start did not create root values for scenario " +
                std::to_string(scenario.scenario_id) + ".");
            continue;
        }
        if (q_value(root) < 1.0 - kTolerance || x_value(root) < 1.0 - kTolerance) {
            mark_invalid(
                "Cut/reachability MIP start violates root burn/reach convention for scenario " +
                std::to_string(scenario.scenario_id) + ".");
        }

        for (const int compact_node : nodes) {
            if (compact_node == root) {
                continue;
            }
            const bool has_y = y_position_by_node.find(compact_node) != y_position_by_node.end();
            const double x_node = x_value(compact_node);
            const double q_node = q_value(compact_node);
            const double y_node = has_y ? y_value(compact_node) : 0.0;
            if (has_y) {
                if (x_node > 1.0 - y_node + kTolerance) {
                    mark_invalid(
                        "Cut/reachability MIP start violates x_v + y_v <= 1 at compact node " +
                        std::to_string(compact_node) + ".");
                }
                if (x_node + kTolerance < q_node - y_node) {
                    mark_invalid(
                        "Cut/reachability MIP start violates x_v >= q_v - y_v at compact node " +
                        std::to_string(compact_node) + ".");
                }
            } else if (x_node + kTolerance < q_node) {
                mark_invalid(
                    "Cut/reachability MIP start violates x_v >= q_v at non-eligible compact node " +
                    std::to_string(compact_node) + ".");
            }
        }

        const auto arcs = unique_arcs(scenario);
        for (const auto& arc : arcs) {
            if (q_value(arc.second) + kTolerance < x_value(arc.first)) {
                mark_invalid(
                    "Cut/reachability MIP start violates q_j >= x_i for arc " +
                    std::to_string(arc.first) + " -> " + std::to_string(arc.second) + ".");
            }
        }
    }

    if (values.feasible) {
        values.notes.push_back(
            "Cut/reachability MIP start values were generated from FppRecourseEvaluator and passed feasibility checks.");
    }
    return values;
}

#ifndef FIREBREAK_WITH_CPLEX

ModelResult FppCutReachabilityCplexModel::solve(
    const opt::OptimizationInstance& opt,
    double,
    double,
    int,
    bool,
    const WarmStart*,
    const cuts::DominatorCutOptions* dominator_options,
    const cuts::SeparatorCutOptions* separator_options,
    const risk::RiskMeasureConfig& risk_config) const {
    (void)dominator_options;
    (void)separator_options;
    (void)risk_config;
    (void)analyze_fpp_cut_reachability_model_structure(opt);
    throw std::runtime_error(cplex_unavailable_message());
}

#else

ModelResult FppCutReachabilityCplexModel::solve(
    const opt::OptimizationInstance& opt,
    double time_limit_seconds,
    double mip_gap,
    int threads,
    bool verbose,
    const WarmStart* warm_start,
    const cuts::DominatorCutOptions* dominator_options,
    const cuts::SeparatorCutOptions* separator_options,
    const risk::RiskMeasureConfig& risk_config) const {
    risk::RiskMeasureConfig effective_risk_config = risk_config;
    if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
        effective_risk_config.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective_risk_config);
    const auto structure = analyze_fpp_cut_reachability_model_structure(opt);
    const int node_count = opt.node_mapper.size();
    const auto nodes_by_scenario = build_scenario_nodes_by_position(opt);
    const auto position_by_scenario = build_node_position_by_scenario(opt, nodes_by_scenario);
    const bool uses_cvar =
        effective_risk_config.type == risk::RiskMeasureType::CVaR ||
        effective_risk_config.type == risk::RiskMeasureType::MeanCVaR;
    const double cvar_tail_scale = 1.0 / (1.0 - effective_risk_config.cvarBeta);

    ModelResult result;
    result.method = "FPP-SAA cut/reachability CPLEX";
    result.formulation = "cut";
    result.risk_measure = risk::to_string(effective_risk_config.type);
    result.cvar_beta = effective_risk_config.cvarBeta;
    result.cvar_lambda = effective_risk_config.cvarLambda;
    result.num_variables = structure.total_variable_count + (uses_cvar ? 1 + opt.scenarios.size() : 0);
    result.num_constraints = structure.total_constraint_count + (uses_cvar ? opt.scenarios.size() : 0);

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

        std::vector<IloNumVarArray> x;
        std::vector<IloNumVarArray> q;
        x.reserve(opt.scenarios.size());
        q.reserve(opt.scenarios.size());
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
            x.push_back(x_s);
            q.push_back(q_s);
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
                for (std::size_t local_pos = 0; local_pos < nodes_by_scenario[s].size(); ++local_pos) {
                    objective += probability * x[s][static_cast<IloInt>(local_pos)];
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
                    for (std::size_t local_pos = 0; local_pos < nodes_by_scenario[s].size(); ++local_pos) {
                        objective += expected_weight * probability * x[s][static_cast<IloInt>(local_pos)];
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
                for (std::size_t local_pos = 0; local_pos < nodes_by_scenario[s].size(); ++local_pos) {
                    loss_minus_threshold += x[s][static_cast<IloInt>(local_pos)];
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
            const int root = opt.scenarios[s].ignition_index;
            const int root_pos = position_by_scenario[s][static_cast<std::size_t>(root)];
            if (root_pos < 0) {
                throw std::runtime_error("FPP cut/reachability formulation root variable was not created.");
            }
            model.add(q[s][static_cast<IloInt>(root_pos)] == 1.0);
            model.add(x[s][static_cast<IloInt>(root_pos)] == 1.0);
        }

        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            const auto arcs = unique_arcs(opt.scenarios[s]);
            for (const auto& arc : arcs) {
                const int u_pos = position_by_scenario[s][static_cast<std::size_t>(arc.first)];
                const int v_pos = position_by_scenario[s][static_cast<std::size_t>(arc.second)];
                if (u_pos < 0 || v_pos < 0) {
                    throw std::runtime_error(
                        "FPP cut/reachability formulation arc endpoint variable was not created.");
                }
                model.add(q[s][static_cast<IloInt>(v_pos)] >= x[s][static_cast<IloInt>(u_pos)]);
            }
        }

        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            const int root = opt.scenarios[s].ignition_index;
            for (std::size_t local_pos = 0; local_pos < nodes_by_scenario[s].size(); ++local_pos) {
                const int compact_node = nodes_by_scenario[s][local_pos];
                if (compact_node == root) {
                    continue;
                }
                const int y_pos = y_position_by_node[static_cast<std::size_t>(compact_node)];
                if (y_pos >= 0) {
                    IloExpr pass_expr(env);
                    pass_expr += x[s][static_cast<IloInt>(local_pos)];
                    pass_expr -= q[s][static_cast<IloInt>(local_pos)];
                    pass_expr += y[static_cast<IloInt>(y_pos)];
                    model.add(pass_expr >= 0.0);
                    pass_expr.end();

                    IloExpr upper_expr(env);
                    upper_expr += x[s][static_cast<IloInt>(local_pos)];
                    upper_expr += y[static_cast<IloInt>(y_pos)];
                    model.add(upper_expr <= 1.0);
                    upper_expr.end();
                } else {
                    model.add(
                        x[s][static_cast<IloInt>(local_pos)] >=
                        q[s][static_cast<IloInt>(local_pos)]);
                }
            }
        }

        if (dominator_options && dominator_options->enabled) {
            cuts::DominatorVariableAccess access;
            access.has_x = [&](std::size_t scenario_index, int compact_node) {
                return scenario_index < position_by_scenario.size() &&
                       compact_node >= 0 &&
                       compact_node < node_count &&
                       position_by_scenario[scenario_index][static_cast<std::size_t>(compact_node)] >= 0;
            };
            access.get_x = [&](std::size_t scenario_index, int compact_node) -> IloNumVar {
                const int local_pos =
                    position_by_scenario[scenario_index][static_cast<std::size_t>(compact_node)];
                return x[scenario_index][static_cast<IloInt>(local_pos)];
            };
            access.has_y = [&](int compact_node) {
                return compact_node >= 0 &&
                       compact_node < static_cast<int>(y_position_by_node.size()) &&
                       y_position_by_node[static_cast<std::size_t>(compact_node)] >= 0;
            };
            access.get_y = [&](int compact_node) -> IloNumVar {
                return y[y_position_by_node[static_cast<std::size_t>(compact_node)]];
            };
            access.skip_self_individual_cuts = true;

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
            add_full_cut_reachability_mip_start(
                env,
                cplex,
                opt,
                *warm_start,
                y_position_by_node,
                y,
                x,
                q,
                nodes_by_scenario,
                result);
        }

        std::unique_ptr<cuts::SeparatorContextCallback> separator_callback;
        if (separator_options && separator_options->enabled) {
            cuts::SeparatorVariableAccess separator_access;
            separator_access.y_vars = y.toNumVarArray();
            separator_access.y_position_by_node = y_position_by_node;
            separator_access.x_vars_by_scenario = x;
            separator_access.x_position_by_scenario = position_by_scenario;

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
                for (std::size_t local_pos = 0; local_pos < nodes_by_scenario[s].size(); ++local_pos) {
                    scenario_loss += cplex.getValue(x[s][static_cast<IloInt>(local_pos)]);
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

            eval::FppRecourseEvaluator evaluator(opt);
            const auto recourse = evaluator.evaluate(result.selected_firebreak_indices, false);
            const double validation_reference =
                effective_risk_config.type == risk::RiskMeasureType::Expected
                    ? result.objective_value
                    : result.expected_loss_component;
            const double abs_diff = std::fabs(recourse.expected_burned_area - validation_reference);
            const double scale = std::max(1.0, std::fabs(validation_reference));
            const double rel_diff = abs_diff / scale;
            if (abs_diff > 1.0e-5 && rel_diff > 1.0e-6) {
                std::ostringstream warning;
                warning << "Warning: FPP cut/reachability expected loss "
                        << validation_reference
                        << " differs from FppRecourseEvaluator expected loss "
                        << recourse.expected_burned_area
                        << " after solve.";
                result.notes.push_back(warning.str());
            } else {
                result.notes.push_back(
                    "FPP cut/reachability post-solve recourse validation matched the solver objective.");
            }
        }

        if (effective_risk_config.type == risk::RiskMeasureType::Expected) {
            result.notes.push_back("FPP cut/reachability objective is expected burned area over observed scenario nodes.");
        } else if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
            result.notes.push_back("FPP cut/reachability objective is pure CVaR of burned area over observed scenario nodes.");
        } else {
            result.notes.push_back("FPP cut/reachability objective is a mean-CVaR blend of burned area over observed scenario nodes.");
        }
        result.notes.push_back("x and q variables are continuous in [0,1]; y variables are binary for eligible nodes.");
        result.notes.push_back("Root nodes are forced to burn and propagate even if selected as firebreaks.");
        result.notes.push_back("Non-eligible non-root nodes use x_v >= q_v because no y_v variable exists.");
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
