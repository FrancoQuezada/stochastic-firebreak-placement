#include "benders/DpvLiftedLowerBound.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "opt/WeightedDpvScoring.hpp"

namespace firebreak::benders {

namespace {

void validate_scenario_position(const opt::OptimizationInstance& opt, int scenario_position) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("DPV fixed-y loss requires at least one mapped node.");
    }
    if (scenario_position < 0 || scenario_position >= static_cast<int>(opt.scenarios.size())) {
        throw std::runtime_error("DPV fixed-y loss scenario position is out of range.");
    }
}

std::vector<std::vector<int>> build_adjacency(
    const opt::OptimizationScenario& scenario,
    int node_count) {
    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(node_count));
    for (const auto& arc : scenario.arcs) {
        if (arc.u_index < 0 || arc.u_index >= node_count ||
            arc.v_index < 0 || arc.v_index >= node_count) {
            throw std::runtime_error("DPV fixed-y loss found an arc endpoint outside the node range.");
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
        throw std::runtime_error("DPV downstream source index is out of range.");
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
                throw std::runtime_error("DPV downstream traversal found an out-of-range node.");
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
        throw std::runtime_error("DPV fixed-y loss firebreak vector has wrong size.");
    }
    if (scenario.ignition_index < 0 || scenario.ignition_index >= node_count) {
        throw std::runtime_error("DPV fixed-y loss ignition is outside the node range.");
    }
    if (adjacency.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error("DPV fixed-y loss adjacency has wrong size.");
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

double count_dpv_loss_from_burned_set(
    const opt::OptimizationScenario& scenario,
    const std::vector<char>& burned_by_compact_index,
    int node_count,
    const std::vector<double>& compact_weights) {
    if (burned_by_compact_index.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error("DPV loss burned vector has wrong size.");
    }
    if (compact_weights.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error("DPV loss compact weight vector has wrong size.");
    }

    double loss = 0.0;
    for (const auto& pair : scenario.dpv.product_pairs) {
        if (pair.successor_index < 0 || pair.successor_index >= node_count ||
            pair.descendant_index < 0 || pair.descendant_index >= node_count) {
            throw std::runtime_error("DPV product pair contains an out-of-range node index.");
        }
        if (burned_by_compact_index[static_cast<std::size_t>(pair.successor_index)] &&
            burned_by_compact_index[static_cast<std::size_t>(pair.descendant_index)]) {
            loss += compact_weights[static_cast<std::size_t>(pair.descendant_index)];
        }
    }
    return loss;
}

struct DpvLiftedScenarioStructure {
    const opt::OptimizationScenario* scenario = nullptr;
    int scenario_id = 0;
    int node_count = 0;
    int ignition_index = -1;
    double f_empty = 0.0;
    std::vector<int> eligible_indices;
    std::vector<std::vector<int>> adjacency;
    std::vector<char> empty_burned_by_compact_index;
    std::vector<std::vector<int>> product_ids_touching_node;
    std::vector<double> product_weight_by_id;
    std::vector<double> compact_weights;
};

DpvLiftedScenarioStructure build_lifted_scenario_structure(
    const opt::OptimizationInstance& opt,
    int scenario_position) {
    validate_scenario_position(opt, scenario_position);
    const int node_count = opt.node_mapper.size();
    const auto& scenario = opt.scenarios[static_cast<std::size_t>(scenario_position)];
    if (scenario.dpv.product_pairs.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("DPV scenario has too many product pairs for LLBI stamping.");
    }

    DpvLiftedScenarioStructure structure;
    structure.scenario = &scenario;
    structure.scenario_id = scenario.scenario_id;
    structure.node_count = node_count;
    structure.ignition_index = scenario.ignition_index;
    structure.eligible_indices = opt.eligible_indices;
    structure.adjacency = build_adjacency(scenario, node_count);
    structure.compact_weights = opt::canonical_compact_dpv_weights_or_unit(opt);

    const std::vector<char> empty_selection(static_cast<std::size_t>(node_count), 0);
    structure.empty_burned_by_compact_index = compute_burned_set_with_adjacency(
        scenario,
        structure.adjacency,
        node_count,
        empty_selection);

    structure.product_ids_touching_node.assign(static_cast<std::size_t>(node_count), {});
    structure.product_weight_by_id.assign(scenario.dpv.product_pairs.size(), 0.0);
    for (std::size_t product_id = 0; product_id < scenario.dpv.product_pairs.size(); ++product_id) {
        const auto& pair = scenario.dpv.product_pairs[product_id];
        if (pair.successor_index < 0 || pair.successor_index >= node_count ||
            pair.descendant_index < 0 || pair.descendant_index >= node_count) {
            throw std::runtime_error("DPV product pair contains an out-of-range node index.");
        }
        if (!structure.empty_burned_by_compact_index[static_cast<std::size_t>(pair.successor_index)] ||
            !structure.empty_burned_by_compact_index[static_cast<std::size_t>(pair.descendant_index)]) {
            continue;
        }

        const int compact_product_id = static_cast<int>(product_id);
        const double product_weight =
            structure.compact_weights[static_cast<std::size_t>(pair.descendant_index)];
        structure.product_weight_by_id[product_id] = product_weight;
        structure.f_empty += product_weight;
        structure.product_ids_touching_node[static_cast<std::size_t>(pair.successor_index)]
            .push_back(compact_product_id);
        if (pair.descendant_index != pair.successor_index) {
            structure.product_ids_touching_node[static_cast<std::size_t>(pair.descendant_index)]
                .push_back(compact_product_id);
        }
    }

    return structure;
}

double count_active_products_touching_nodes(
    const DpvLiftedScenarioStructure& structure,
    const std::vector<int>& compact_nodes,
    std::vector<int>& product_stamp,
    int& current_stamp) {
    if (product_stamp.size() != structure.scenario->dpv.product_pairs.size()) {
        throw std::runtime_error("LLBI product stamp array has wrong size.");
    }

    if (current_stamp == std::numeric_limits<int>::max()) {
        std::fill(product_stamp.begin(), product_stamp.end(), 0);
        current_stamp = 0;
    }
    ++current_stamp;

    double delta = 0.0;
    for (const int compact_node : compact_nodes) {
        if (compact_node < 0 || compact_node >= structure.node_count) {
            throw std::runtime_error("LLBI downstream node is out of range.");
        }
        for (const int product_id :
             structure.product_ids_touching_node[static_cast<std::size_t>(compact_node)]) {
            if (product_stamp[static_cast<std::size_t>(product_id)] == current_stamp) {
                continue;
            }
            product_stamp[static_cast<std::size_t>(product_id)] = current_stamp;
            delta += structure.product_weight_by_id[static_cast<std::size_t>(product_id)];
        }
    }
    return delta;
}

DpvLiftedLowerBoundInequality build_lifted_lower_bound_from_structure(
    const DpvLiftedScenarioStructure& structure,
    double coefficient_threshold) {
    DpvLiftedLowerBoundInequality inequality;
    inequality.scenario_id = structure.scenario_id;
    inequality.f_empty = structure.f_empty;
    inequality.rhs_constant = structure.f_empty;
    inequality.coefficients_by_compact_index.reserve(structure.eligible_indices.size());

    std::vector<int> product_stamp(structure.scenario->dpv.product_pairs.size(), 0);
    int current_stamp = 0;

    for (const int compact_index : structure.eligible_indices) {
        if (compact_index < 0 || compact_index >= structure.node_count) {
            throw std::runtime_error("LLBI eligible compact node is out of range.");
        }

        double coefficient = 0.0;
        if (compact_index != structure.ignition_index) {
            const auto downstream_nodes =
                closed_downstream_nodes(compact_index, structure.adjacency);
            const double delta = count_active_products_touching_nodes(
                structure,
                downstream_nodes,
                product_stamp,
                current_stamp);
            coefficient = -delta;
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
    const DpvLiftedLowerBoundInequality& inequality,
    int scenario_position,
    int budget,
    int start_pos,
    std::vector<int>& selected_positions,
    DpvLiftedLowerBoundValidationResult& validation,
    double tolerance) {
    if (!validation.valid) {
        return;
    }

    std::vector<char> selected_by_compact(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    for (const int eligible_pos : selected_positions) {
        const int compact_index = opt.eligible_indices[static_cast<std::size_t>(eligible_pos)];
        selected_by_compact[static_cast<std::size_t>(compact_index)] = 1;
    }

    const double actual = evaluate_fixed_y_dpv_loss(opt, scenario_position, selected_by_compact).loss;
    const double lower_bound = inequality.evaluateAtCompact(selected_by_compact);
    if (actual + tolerance < lower_bound) {
        std::ostringstream message;
        message << "Lifted lower-bound inequality violated for scenario "
                << inequality.scenario_id
                << ": actual fixed-y DPV loss=" << actual
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

double DpvLiftedLowerBoundInequality::evaluateAt(
    const std::vector<int>& y_by_eligible_position,
    const opt::OptimizationInstance& opt) const {
    if (y_by_eligible_position.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("LLBI evaluation y vector must match eligible-node count.");
    }
    std::vector<char> compact_y(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    for (std::size_t pos = 0; pos < y_by_eligible_position.size(); ++pos) {
        compact_y[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            y_by_eligible_position[pos] == 0 ? 0 : 1;
    }
    return evaluateAtCompact(compact_y);
}

double DpvLiftedLowerBoundInequality::evaluateAtCompact(
    const std::vector<char>& y_by_compact_index) const {
    double value = rhs_constant;
    for (const auto& [compact_index, coefficient] : coefficients_by_compact_index) {
        if (compact_index < 0 || compact_index >= static_cast<int>(y_by_compact_index.size())) {
            throw std::runtime_error("LLBI evaluation coefficient compact index is out of range.");
        }
        value += coefficient * static_cast<double>(y_by_compact_index[static_cast<std::size_t>(compact_index)] != 0);
    }
    return value;
}

FixedDpvLossResult evaluate_fixed_y_dpv_loss(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    const std::vector<char>& selected_firebreak_by_compact_index) {
    validate_scenario_position(opt, scenario_position);
    const int node_count = opt.node_mapper.size();
    if (selected_firebreak_by_compact_index.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error("DPV fixed-y loss firebreak vector has wrong size.");
    }

    const auto& scenario = opt.scenarios[static_cast<std::size_t>(scenario_position)];
    if (scenario.ignition_index < 0 || scenario.ignition_index >= node_count) {
        throw std::runtime_error("DPV fixed-y loss ignition is outside the node range.");
    }
    const auto adjacency = build_adjacency(scenario, node_count);

    FixedDpvLossResult result;
    result.scenario_id = scenario.scenario_id;
    result.burned_by_compact_index = compute_burned_set_with_adjacency(
        scenario,
        adjacency,
        node_count,
        selected_firebreak_by_compact_index);

    result.loss = count_dpv_loss_from_burned_set(
        scenario,
        result.burned_by_compact_index,
        node_count,
        opt::canonical_compact_dpv_weights_or_unit(opt));
    return result;
}

FixedDpvLossResult evaluate_optimistic_singleton_dpv_loss(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    int firebreak_compact_index) {
    const auto structure = build_lifted_scenario_structure(opt, scenario_position);
    if (firebreak_compact_index < 0 || firebreak_compact_index >= structure.node_count) {
        throw std::runtime_error("Optimistic singleton DPV loss firebreak index is out of range.");
    }

    FixedDpvLossResult result;
    result.scenario_id = structure.scenario_id;
    result.loss = structure.f_empty;
    result.burned_by_compact_index = structure.empty_burned_by_compact_index;

    if (firebreak_compact_index == structure.ignition_index) {
        return result;
    }

    const auto downstream_nodes =
        closed_downstream_nodes(firebreak_compact_index, structure.adjacency);
    std::vector<int> product_stamp(structure.scenario->dpv.product_pairs.size(), 0);
    int current_stamp = 0;
    const double delta = count_active_products_touching_nodes(
        structure,
        downstream_nodes,
        product_stamp,
        current_stamp);
    for (const int compact_index : downstream_nodes) {
        result.burned_by_compact_index[static_cast<std::size_t>(compact_index)] = 0;
    }

    result.loss = structure.f_empty - delta;
    return result;
}

DpvLiftedLowerBoundInequality build_dpv_lifted_lower_bound_for_scenario(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    double coefficient_threshold) {
    validate_scenario_position(opt, scenario_position);
    if (coefficient_threshold < 0.0) {
        throw std::runtime_error("LLBI coefficient threshold must be nonnegative.");
    }

    const auto structure = build_lifted_scenario_structure(opt, scenario_position);
    return build_lifted_lower_bound_from_structure(structure, coefficient_threshold);
}

DpvLiftedLowerBoundPrecomputeResult build_dpv_lifted_lower_bounds(
    const opt::OptimizationInstance& opt,
    double coefficient_threshold) {
    const auto start = std::chrono::steady_clock::now();
    DpvLiftedLowerBoundPrecomputeResult result;
    result.inequalities.reserve(opt.scenarios.size());
    result.min_rhs = std::numeric_limits<double>::infinity();
    result.max_rhs = -std::numeric_limits<double>::infinity();
    result.no_firebreak_loss_min = std::numeric_limits<double>::infinity();
    result.no_firebreak_loss_max = -std::numeric_limits<double>::infinity();
    result.singleton_benefit_min = std::numeric_limits<double>::infinity();
    result.singleton_benefit_max = -std::numeric_limits<double>::infinity();
    const auto compact_weights = opt::canonical_compact_dpv_weights_or_unit(opt);
    result.weighted = true;
    result.weight_map_hash = opt::weighted_dpv_weight_map_hash(opt, compact_weights);
    result.validity_mode = "weighted_exhaustive_valid_for_small_tests";

    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        auto inequality = build_dpv_lifted_lower_bound_for_scenario(
            opt,
            static_cast<int>(s),
            coefficient_threshold);
        result.total_nonzero_coefficients += inequality.nonzero_coefficients;
        result.min_rhs = std::min(result.min_rhs, inequality.rhs_constant);
        result.max_rhs = std::max(result.max_rhs, inequality.rhs_constant);
        result.no_firebreak_loss_min = std::min(result.no_firebreak_loss_min, inequality.f_empty);
        result.no_firebreak_loss_max = std::max(result.no_firebreak_loss_max, inequality.f_empty);
        for (const int compact_index : opt.eligible_indices) {
            double coefficient = 0.0;
            for (const auto& [candidate_compact_index, candidate_coefficient] :
                 inequality.coefficients_by_compact_index) {
                if (candidate_compact_index == compact_index) {
                    coefficient = candidate_coefficient;
                    break;
                }
            }
            const double benefit = -coefficient;
            result.singleton_benefit_min = std::min(result.singleton_benefit_min, benefit);
            result.singleton_benefit_max = std::max(result.singleton_benefit_max, benefit);
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
        result.singleton_benefit_min = 0.0;
        result.singleton_benefit_max = 0.0;
    }
    if (!std::isfinite(result.singleton_benefit_min)) {
        result.singleton_benefit_min = 0.0;
        result.singleton_benefit_max = 0.0;
    }
    result.scenarios_precomputed = static_cast<int>(result.inequalities.size());
    result.notes.push_back(
        "Weighted lifted lower-bound inequalities use optimistic downstream singleton losses; true singleton recourse values are not used for coefficients.");
    result.notes.push_back(
        "DPV LLBI product-pair multiplicity is preserved and each product value is weight(descendant).");

    const auto end = std::chrono::steady_clock::now();
    result.precompute_time_sec = std::chrono::duration<double>(end - start).count();
    return result;
}

DpvLiftedLowerBoundValidationResult validate_dpv_lifted_lower_bound_exhaustive(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    int budget,
    double tolerance) {
    validate_scenario_position(opt, scenario_position);
    if (budget < 0) {
        throw std::runtime_error("LLBI validation budget must be nonnegative.");
    }
    if (budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("LLBI validation budget exceeds eligible-node count.");
    }
    if (tolerance < 0.0) {
        throw std::runtime_error("LLBI validation tolerance must be nonnegative.");
    }

    const auto inequality = build_dpv_lifted_lower_bound_for_scenario(opt, scenario_position);
    DpvLiftedLowerBoundValidationResult validation;
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
        validation.message = "Lifted lower-bound inequality passed exhaustive validation.";
    }
    return validation;
}

}  // namespace firebreak::benders
