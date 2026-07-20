#include "benders/FppLiftedLowerBound.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "solver/FppWeightedLossUtils.hpp"

namespace firebreak::benders {

namespace {

void validate_scenario_position(const opt::OptimizationInstance& opt, int scenario_position) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("FPP fixed-y loss requires at least one mapped node.");
    }
    if (scenario_position < 0 || scenario_position >= static_cast<int>(opt.scenarios.size())) {
        throw std::runtime_error("FPP fixed-y loss scenario position is out of range.");
    }
}

std::vector<std::vector<int>> build_adjacency(
    const opt::OptimizationScenario& scenario,
    int node_count) {
    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(node_count));
    for (const auto& arc : scenario.arcs) {
        if (arc.u_index < 0 || arc.u_index >= node_count ||
            arc.v_index < 0 || arc.v_index >= node_count) {
            throw std::runtime_error("FPP fixed-y loss found an arc endpoint outside the node range.");
        }
        adjacency[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
    }
    for (auto& successors : adjacency) {
        std::sort(successors.begin(), successors.end());
        successors.erase(std::unique(successors.begin(), successors.end()), successors.end());
    }
    return adjacency;
}

std::vector<int> closed_downstream_nodes(
    int source,
    const std::vector<std::vector<int>>& adjacency) {
    if (source < 0 || source >= static_cast<int>(adjacency.size())) {
        throw std::runtime_error("FPP downstream source index is out of range.");
    }

    std::vector<char> visited(adjacency.size(), 0);
    std::vector<int> downstream_nodes;
    std::queue<int> frontier;
    visited[static_cast<std::size_t>(source)] = 1;
    frontier.push(source);

    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        downstream_nodes.push_back(current);
        for (const int next : adjacency[static_cast<std::size_t>(current)]) {
            if (next < 0 || next >= static_cast<int>(adjacency.size())) {
                throw std::runtime_error("FPP downstream traversal found an out-of-range node.");
            }
            const auto next_pos = static_cast<std::size_t>(next);
            if (!visited[next_pos]) {
                visited[next_pos] = 1;
                frontier.push(next);
            }
        }
    }

    return downstream_nodes;
}

std::vector<char> compute_burned_set_with_adjacency(
    const opt::OptimizationScenario& scenario,
    const std::vector<std::vector<int>>& adjacency,
    int node_count,
    const std::vector<char>& selected_firebreak_by_compact_index) {
    if (selected_firebreak_by_compact_index.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error("FPP fixed-y loss firebreak vector has wrong size.");
    }
    if (scenario.ignition_index < 0 || scenario.ignition_index >= node_count) {
        throw std::runtime_error("FPP fixed-y loss ignition is outside the node range.");
    }
    if (adjacency.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error("FPP fixed-y loss adjacency has wrong size.");
    }

    std::vector<char> burned(static_cast<std::size_t>(node_count), 0);
    std::queue<int> frontier;
    const int root = scenario.ignition_index;
    burned[static_cast<std::size_t>(root)] = 1;
    frontier.push(root);

    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        for (const int next : adjacency[static_cast<std::size_t>(current)]) {
            const auto next_pos = static_cast<std::size_t>(next);
            if (burned[next_pos]) {
                continue;
            }
            if (next != root && selected_firebreak_by_compact_index[next_pos]) {
                continue;
            }
            burned[next_pos] = 1;
            frontier.push(next);
        }
    }

    return burned;
}

double count_burned_nodes(const std::vector<char>& burned_by_compact_index) {
    return static_cast<double>(std::count(
        burned_by_compact_index.begin(),
        burned_by_compact_index.end(),
        static_cast<char>(1)));
}

double weighted_burn_loss(
    const std::vector<char>& burned_by_compact_index,
    const std::vector<double>& compact_weights) {
    if (burned_by_compact_index.size() != compact_weights.size()) {
        throw std::runtime_error("FPP weighted burned-loss vector sizes do not match.");
    }
    double loss = 0.0;
    for (std::size_t compact_index = 0; compact_index < burned_by_compact_index.size();
         ++compact_index) {
        if (burned_by_compact_index[compact_index]) {
            loss += compact_weights[compact_index];
        }
    }
    return loss;
}

bool has_nonunit_weights(const std::vector<double>& compact_weights) {
    for (const double weight : compact_weights) {
        if (std::fabs(weight - 1.0) > 1.0e-9) {
            return true;
        }
    }
    return false;
}

std::string llbi_weight_map_hash(const opt::OptimizationInstance& opt) {
    if (!opt.cell_weight_map.deterministic_hash.empty()) {
        return opt.cell_weight_map.deterministic_hash;
    }
    if (opt.compact_cell_weights.empty()) {
        return "homogeneous-unit";
    }
    std::ostringstream out;
    out << "compact-weights:n=" << opt.compact_cell_weights.size();
    double total = 0.0;
    double weighted_index_sum = 0.0;
    for (std::size_t i = 0; i < opt.compact_cell_weights.size(); ++i) {
        total += opt.compact_cell_weights[i];
        weighted_index_sum += static_cast<double>(i + 1) * opt.compact_cell_weights[i];
    }
    out << ":sum=" << total << ":idxsum=" << weighted_index_sum;
    return out.str();
}

struct FppLiftedScenarioStructure {
    int scenario_id = 0;
    int node_count = 0;
    int ignition_index = -1;
    double f_empty = 0.0;
    double empty_burned_area = 0.0;
    bool weighted = false;
    std::string weight_map_hash;
    std::vector<int> eligible_indices;
    std::vector<double> compact_weights;
    std::vector<std::vector<int>> adjacency;
    std::vector<char> empty_burned_by_compact_index;
};

FppLiftedScenarioStructure build_lifted_scenario_structure(
    const opt::OptimizationInstance& opt,
    int scenario_position) {
    validate_scenario_position(opt, scenario_position);
    const int node_count = opt.node_mapper.size();
    const auto& scenario = opt.scenarios[static_cast<std::size_t>(scenario_position)];

    FppLiftedScenarioStructure structure;
    structure.scenario_id = scenario.scenario_id;
    structure.node_count = node_count;
    structure.ignition_index = scenario.ignition_index;
    structure.eligible_indices = opt.eligible_indices;
    structure.compact_weights = solver::direct_fpp_compact_weights_or_unit(opt);
    structure.weighted = has_nonunit_weights(structure.compact_weights);
    structure.weight_map_hash = llbi_weight_map_hash(opt);
    structure.adjacency = build_adjacency(scenario, node_count);

    const std::vector<char> empty_selection(static_cast<std::size_t>(node_count), 0);
    structure.empty_burned_by_compact_index = compute_burned_set_with_adjacency(
        scenario,
        structure.adjacency,
        node_count,
        empty_selection);
    structure.empty_burned_area = count_burned_nodes(structure.empty_burned_by_compact_index);
    structure.f_empty = weighted_burn_loss(
        structure.empty_burned_by_compact_index,
        structure.compact_weights);
    return structure;
}

FppLiftedLowerBoundInequality build_lifted_lower_bound_from_structure(
    const FppLiftedScenarioStructure& structure,
    double coefficient_threshold) {
    FppLiftedLowerBoundInequality inequality;
    inequality.scenario_id = structure.scenario_id;
    inequality.f_empty = structure.f_empty;
    inequality.rhs_constant = structure.f_empty;
    inequality.coefficients_by_compact_index.reserve(structure.eligible_indices.size());

    for (const int compact_index : structure.eligible_indices) {
        if (compact_index < 0 || compact_index >= structure.node_count) {
            throw std::runtime_error("FPP LLBI eligible compact node is out of range.");
        }

        double coefficient = 0.0;
        if (compact_index != structure.ignition_index) {
            const auto downstream_nodes =
                closed_downstream_nodes(compact_index, structure.adjacency);
            double removed = 0.0;
            for (const int downstream : downstream_nodes) {
                if (structure.empty_burned_by_compact_index[static_cast<std::size_t>(downstream)]) {
                    removed += structure.compact_weights[static_cast<std::size_t>(downstream)];
                }
            }
            coefficient = -removed;
        }

        if (std::fabs(coefficient) > coefficient_threshold) {
            inequality.coefficients_by_compact_index.push_back({compact_index, coefficient});
            ++inequality.nonzero_coefficients;
        }
    }

    return inequality;
}

void enumerate_subsets(
    const opt::OptimizationInstance& opt,
    const FppLiftedLowerBoundInequality& inequality,
    int scenario_position,
    int budget,
    int start_pos,
    std::vector<int>& selected_positions,
    FppLiftedLowerBoundValidationResult& validation,
    double tolerance) {
    if (!validation.valid) {
        return;
    }

    std::vector<char> selected_by_compact(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    for (const int eligible_pos : selected_positions) {
        const int compact_index = opt.eligible_indices[static_cast<std::size_t>(eligible_pos)];
        selected_by_compact[static_cast<std::size_t>(compact_index)] = 1;
    }

    const double actual =
        evaluate_fixed_y_fpp_loss(opt, scenario_position, selected_by_compact).weighted_burn_loss;
    const double lower_bound = inequality.evaluateAtCompact(selected_by_compact);
    if (actual + tolerance < lower_bound) {
        std::ostringstream message;
        message << "FPP lifted lower-bound inequality violated for scenario "
                << inequality.scenario_id
                << ": actual fixed-y weighted burn loss=" << actual
                << ", lower_bound=" << lower_bound
                << ", selected original nodes=";
        bool first = true;
        for (const int eligible_pos : selected_positions) {
            if (!first) {
                message << ";";
            }
            const int compact_index = opt.eligible_indices[static_cast<std::size_t>(eligible_pos)];
            message << opt.node_mapper.to_node(compact_index);
            first = false;
        }
        validation.valid = false;
        validation.message = message.str();
        return;
    }

    if (static_cast<int>(selected_positions.size()) >= budget) {
        return;
    }
    for (int pos = start_pos; pos < static_cast<int>(opt.eligible_indices.size()); ++pos) {
        selected_positions.push_back(pos);
        enumerate_subsets(
            opt,
            inequality,
            scenario_position,
            budget,
            pos + 1,
            selected_positions,
            validation,
            tolerance);
        selected_positions.pop_back();
        if (!validation.valid) {
            return;
        }
    }
}

}  // namespace

double FppLiftedLowerBoundInequality::evaluateAt(
    const std::vector<int>& y_by_eligible_position,
    const opt::OptimizationInstance& opt) const {
    if (y_by_eligible_position.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("FPP LLBI evaluation y vector must match eligible-node count.");
    }
    std::vector<char> compact_y(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    for (std::size_t pos = 0; pos < y_by_eligible_position.size(); ++pos) {
        compact_y[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            y_by_eligible_position[pos] == 0 ? 0 : 1;
    }
    return evaluateAtCompact(compact_y);
}

double FppLiftedLowerBoundInequality::evaluateAtCompact(
    const std::vector<char>& y_by_compact_index) const {
    double value = rhs_constant;
    for (const auto& [compact_index, coefficient] : coefficients_by_compact_index) {
        if (compact_index < 0 || compact_index >= static_cast<int>(y_by_compact_index.size())) {
            throw std::runtime_error("FPP LLBI evaluation coefficient compact index is out of range.");
        }
        value += coefficient *
            static_cast<double>(y_by_compact_index[static_cast<std::size_t>(compact_index)] != 0);
    }
    return value;
}

FixedFppLossResult evaluate_fixed_y_fpp_loss(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    const std::vector<char>& selected_firebreak_by_compact_index) {
    validate_scenario_position(opt, scenario_position);
    const int node_count = opt.node_mapper.size();
    if (selected_firebreak_by_compact_index.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error("FPP fixed-y loss firebreak vector has wrong size.");
    }

    const auto& scenario = opt.scenarios[static_cast<std::size_t>(scenario_position)];
    const auto adjacency = build_adjacency(scenario, node_count);

    FixedFppLossResult result;
    result.scenario_id = scenario.scenario_id;
    result.burned_by_compact_index = compute_burned_set_with_adjacency(
        scenario,
        adjacency,
        node_count,
        selected_firebreak_by_compact_index);
    result.burned_area = count_burned_nodes(result.burned_by_compact_index);
    result.weighted_burn_loss = weighted_burn_loss(
        result.burned_by_compact_index,
        solver::direct_fpp_compact_weights_or_unit(opt));
    return result;
}

FixedFppLossResult evaluate_optimistic_singleton_fpp_loss(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    int firebreak_compact_index) {
    const auto structure = build_lifted_scenario_structure(opt, scenario_position);
    if (firebreak_compact_index < 0 || firebreak_compact_index >= structure.node_count) {
        throw std::runtime_error("Optimistic singleton FPP loss firebreak index is out of range.");
    }

    FixedFppLossResult result;
    result.scenario_id = structure.scenario_id;
    result.burned_area = structure.empty_burned_area;
    result.weighted_burn_loss = structure.f_empty;
    result.burned_by_compact_index = structure.empty_burned_by_compact_index;

    if (firebreak_compact_index == structure.ignition_index) {
        return result;
    }

    const auto downstream_nodes =
        closed_downstream_nodes(firebreak_compact_index, structure.adjacency);
    for (const int compact_index : downstream_nodes) {
        result.burned_by_compact_index[static_cast<std::size_t>(compact_index)] = 0;
    }
    result.burned_area = count_burned_nodes(result.burned_by_compact_index);
    result.weighted_burn_loss = weighted_burn_loss(
        result.burned_by_compact_index,
        structure.compact_weights);
    return result;
}

FppLiftedLowerBoundInequality build_fpp_lifted_lower_bound_for_scenario(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    double coefficient_threshold) {
    validate_scenario_position(opt, scenario_position);
    if (coefficient_threshold < 0.0) {
        throw std::runtime_error("FPP LLBI coefficient threshold must be nonnegative.");
    }

    const auto structure = build_lifted_scenario_structure(opt, scenario_position);
    return build_lifted_lower_bound_from_structure(structure, coefficient_threshold);
}

FppLiftedLowerBoundPrecomputeResult build_fpp_lifted_lower_bounds(
    const opt::OptimizationInstance& opt,
    double coefficient_threshold) {
    const auto start = std::chrono::steady_clock::now();
    FppLiftedLowerBoundPrecomputeResult result;
    result.inequalities.reserve(opt.scenarios.size());
    result.min_rhs = std::numeric_limits<double>::infinity();
    result.max_rhs = -std::numeric_limits<double>::infinity();
    result.no_firebreak_loss_min = std::numeric_limits<double>::infinity();
    result.no_firebreak_loss_max = -std::numeric_limits<double>::infinity();
    result.singleton_benefit_min = std::numeric_limits<double>::infinity();
    result.singleton_benefit_max = -std::numeric_limits<double>::infinity();
    const auto compact_weights = solver::direct_fpp_compact_weights_or_unit(opt);
    result.weighted = has_nonunit_weights(compact_weights);
    result.weight_map_hash = llbi_weight_map_hash(opt);
    result.scenarios_precomputed = static_cast<int>(opt.scenarios.size());
    result.validity_mode = "downstream-union-bound";

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        auto inequality = build_fpp_lifted_lower_bound_for_scenario(
            opt,
            static_cast<int>(s),
            coefficient_threshold);
        result.total_nonzero_coefficients += inequality.nonzero_coefficients;
        result.min_rhs = std::min(result.min_rhs, inequality.rhs_constant);
        result.max_rhs = std::max(result.max_rhs, inequality.rhs_constant);
        result.no_firebreak_loss_min =
            std::min(result.no_firebreak_loss_min, inequality.rhs_constant);
        result.no_firebreak_loss_max =
            std::max(result.no_firebreak_loss_max, inequality.rhs_constant);
        for (const int compact_index : opt.eligible_indices) {
            double benefit = 0.0;
            const auto found = std::find_if(
                inequality.coefficients_by_compact_index.begin(),
                inequality.coefficients_by_compact_index.end(),
                [compact_index](const auto& entry) {
                    return entry.first == compact_index;
                });
            if (found != inequality.coefficients_by_compact_index.end()) {
                benefit = -found->second;
            }
            result.singleton_benefit_min =
                std::min(result.singleton_benefit_min, benefit);
            result.singleton_benefit_max =
                std::max(result.singleton_benefit_max, benefit);
            ++result.singletons_evaluated;
        }
        result.notes.insert(
            result.notes.end(),
            inequality.notes.begin(),
            inequality.notes.end());
        result.inequalities.push_back(std::move(inequality));
    }

    if (result.inequalities.empty()) {
        result.min_rhs = 0.0;
        result.max_rhs = 0.0;
        result.no_firebreak_loss_min = 0.0;
        result.no_firebreak_loss_max = 0.0;
    }
    if (result.singletons_evaluated == 0) {
        result.singleton_benefit_min = 0.0;
        result.singleton_benefit_max = 0.0;
    }
    result.notes.push_back(
        "FPP lifted lower-bound inequalities use optimistic downstream singleton weighted losses; singleton LP subproblems are not solved for coefficients.");
    result.notes.push_back(
        "Validity mode downstream-union-bound: each coefficient is the weighted empty-burned mass in the candidate's closed downstream set, so selected firebreaks subtract at most a union upper bound.");

    const auto end = std::chrono::steady_clock::now();
    result.precompute_time_sec = std::chrono::duration<double>(end - start).count();
    return result;
}

FppLiftedLowerBoundValidationResult validate_fpp_lifted_lower_bound_exhaustive(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    int budget,
    double tolerance) {
    validate_scenario_position(opt, scenario_position);
    if (budget < 0) {
        throw std::runtime_error("FPP LLBI validation budget must be nonnegative.");
    }
    if (budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("FPP LLBI validation budget exceeds eligible-node count.");
    }
    if (tolerance < 0.0) {
        throw std::runtime_error("FPP LLBI validation tolerance must be nonnegative.");
    }

    const auto inequality = build_fpp_lifted_lower_bound_for_scenario(opt, scenario_position);
    FppLiftedLowerBoundValidationResult validation;
    std::vector<int> selected_positions;
    enumerate_subsets(
        opt,
        inequality,
        scenario_position,
        budget,
        0,
        selected_positions,
        validation,
        tolerance);
    if (validation.valid) {
        validation.message = "FPP lifted lower-bound inequality passed exhaustive validation.";
    }
    return validation;
}

}  // namespace firebreak::benders
