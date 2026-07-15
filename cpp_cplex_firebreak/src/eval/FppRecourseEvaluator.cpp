#include "eval/FppRecourseEvaluator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_set>

#include "core/LandscapeWeightMap.hpp"
namespace firebreak::eval {

namespace {

void ensure_node_in_range(int node, int node_count, const std::string& context) {
    if (node < 0 || node >= node_count) {
        throw std::runtime_error(context + " node index is out of range.");
    }
}

double scenario_probability_at(
    const opt::OptimizationInstance& instance,
    std::size_t scenario_index) {
    const double probability = instance.scenarios[scenario_index].probability;
    if (std::isfinite(probability)) {
        return probability;
    }
    if (scenario_index < instance.scenario_probabilities.size() &&
        std::isfinite(instance.scenario_probabilities[scenario_index])) {
        return instance.scenario_probabilities[scenario_index];
    }
    throw std::runtime_error("Scenario probability is not finite.");
}

void validate_compact_weights(
    const std::vector<double>& weights,
    int node_count) {
    if (weights.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error("FPP recourse evaluator compact weight vector has the wrong size.");
    }
    for (const double weight : weights) {
        if (!std::isfinite(weight) || weight <= 0.0) {
            throw std::runtime_error(
                "FPP recourse evaluator compact weights must be finite and strictly positive.");
        }
    }
}

std::vector<int> build_compact_cluster_vector(
    const core::LandscapeWeightMap& weight_map,
    const opt::IndexMapper& mapper) {
    std::vector<int> clusters(static_cast<std::size_t>(mapper.size()), 0);
    for (int compact = 0; compact < mapper.size(); ++compact) {
        const int original = mapper.to_node(compact);
        const auto it = weight_map.cluster_id_by_original_cell_id.find(original);
        clusters[static_cast<std::size_t>(compact)] =
            it == weight_map.cluster_id_by_original_cell_id.end() ? 0 : it->second;
        if (clusters[static_cast<std::size_t>(compact)] < 0) {
            throw std::runtime_error("FPP recourse evaluator cluster IDs must be nonnegative.");
        }
    }
    return clusters;
}

double total_weight_from_compact(const std::vector<double>& compact_weights) {
    double total = 0.0;
    for (const double weight : compact_weights) {
        total += weight;
    }
    return total;
}

std::size_t high_value_cells_from_map(const core::LandscapeWeightMap& weight_map) {
    std::size_t count = 0;
    for (const auto& [cell_id, cluster_id] : weight_map.cluster_id_by_original_cell_id) {
        (void)cell_id;
        if (cluster_id > 0) {
            ++count;
        }
    }
    return count;
}

double high_value_weight_from_map(const core::LandscapeWeightMap& weight_map) {
    double total = 0.0;
    for (const auto& [cell_id, cluster_id] : weight_map.cluster_id_by_original_cell_id) {
        if (cluster_id <= 0) {
            continue;
        }
        const auto weight_it = weight_map.weight_by_original_cell_id.find(cell_id);
        if (weight_it == weight_map.weight_by_original_cell_id.end()) {
            throw std::runtime_error("Clustered landscape weight cell is missing a final weight.");
        }
        total += weight_it->second;
    }
    return total;
}

WeightedLossStatistics compute_scenario_weighted_loss_statistics(
    const std::vector<ScenarioRecourseResult>& scenarios,
    double beta) {
    if (scenarios.empty()) {
        throw std::runtime_error("Cannot compute weighted recourse statistics without scenarios.");
    }
    std::vector<risk::WeightedLoss> losses;
    losses.reserve(scenarios.size());
    for (const auto& scenario : scenarios) {
        if (!std::isfinite(scenario.weighted_burn_loss)) {
            throw std::runtime_error("Weighted burn losses must be finite.");
        }
        if (!std::isfinite(scenario.probability) || scenario.probability < 0.0) {
            throw std::runtime_error("Scenario probabilities must be finite and nonnegative.");
        }
        losses.push_back({
            scenario.scenario_id,
            scenario.weighted_burn_loss,
            scenario.probability,
        });
    }
    return compute_weighted_loss_statistics(losses, beta);
}

}  // namespace

WeightedLossStatistics compute_weighted_loss_statistics(
    const std::vector<risk::WeightedLoss>& losses,
    double beta) {
    if (losses.empty()) {
        throw std::runtime_error("Cannot compute weighted recourse statistics without losses.");
    }

    WeightedLossStatistics stats;
    stats.beta = beta;
    stats.minimum = std::numeric_limits<double>::infinity();
    stats.maximum = -std::numeric_limits<double>::infinity();
    double total_probability = 0.0;
    for (const auto& entry : losses) {
        if (!std::isfinite(entry.loss)) {
            throw std::runtime_error("Weighted burn losses must be finite.");
        }
        if (!std::isfinite(entry.probability) || entry.probability < 0.0) {
            throw std::runtime_error("Scenario probabilities must be finite and nonnegative.");
        }
        total_probability += entry.probability;
        stats.minimum = std::min(stats.minimum, entry.loss);
        stats.maximum = std::max(stats.maximum, entry.loss);
    }
    if (total_probability <= 0.0) {
        throw std::runtime_error("Scenario probabilities must have positive total mass.");
    }

    const auto risk_metrics = risk::compute_weighted_risk_metrics(losses, beta);
    stats.expected = risk_metrics.expected;
    stats.var = risk_metrics.var;
    stats.cvar = risk_metrics.cvar;
    stats.tail_count = risk_metrics.tail_count;
    for (const auto& entry : losses) {
        const double normalized_probability = entry.probability / total_probability;
        const double diff = entry.loss - stats.expected;
        stats.variance += normalized_probability * diff * diff;
    }
    stats.standard_deviation = std::sqrt(stats.variance);
    return stats;
}

FppRecourseEvaluator::FppRecourseEvaluator(const opt::OptimizationInstance& instance)
    : instance_(instance),
      node_count_(instance.node_mapper.size()) {
    if (node_count_ <= 0) {
        throw std::runtime_error("FPP recourse evaluator requires at least one mapped node.");
    }
    if (instance_.scenarios.empty()) {
        throw std::runtime_error("FPP recourse evaluator requires at least one scenario.");
    }

    if (instance_.compact_cell_weights.empty()) {
        compact_weights_.assign(static_cast<std::size_t>(node_count_), 1.0);
        compact_cluster_ids_.assign(static_cast<std::size_t>(node_count_), 0);
        total_landscape_weight_ = static_cast<double>(node_count_);
        total_high_value_cells_ = 0;
        total_high_value_weight_ = 0.0;
        weight_profile_ = "homogeneous";
        weight_map_hash_.clear();
    } else {
        compact_weights_ = instance_.compact_cell_weights;
        validate_compact_weights(compact_weights_, node_count_);
        const bool has_weight_map =
            !instance_.cell_weight_map.weight_by_original_cell_id.empty();
        if (has_weight_map) {
            core::validate_landscape_weight_map(instance_.cell_weight_map);
            compact_cluster_ids_ = build_compact_cluster_vector(
                instance_.cell_weight_map,
                instance_.node_mapper);
            total_landscape_weight_ = instance_.cell_weight_map.total_weight;
            total_high_value_cells_ = high_value_cells_from_map(instance_.cell_weight_map);
            total_high_value_weight_ = high_value_weight_from_map(instance_.cell_weight_map);
            weight_profile_ = instance_.cell_weight_map.profile;
            weight_map_hash_ = instance_.cell_weight_map.deterministic_hash;
        } else {
            compact_cluster_ids_.assign(static_cast<std::size_t>(node_count_), 0);
            total_landscape_weight_ = total_weight_from_compact(compact_weights_);
            total_high_value_cells_ = 0;
            total_high_value_weight_ = 0.0;
            weight_profile_ = "compact";
            weight_map_hash_.clear();
        }
    }

    eligible_.assign(static_cast<std::size_t>(node_count_), 0);
    for (const int node : instance_.eligible_indices) {
        ensure_node_in_range(node, node_count_, "Eligible firebreak");
        eligible_[static_cast<std::size_t>(node)] = 1;
    }

    adjacency_by_scenario_.reserve(instance_.scenarios.size());
    for (const auto& scenario : instance_.scenarios) {
        ensure_node_in_range(scenario.ignition_index, node_count_, "Ignition");
        std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(node_count_));
        for (const auto& arc : scenario.arcs) {
            ensure_node_in_range(arc.u_index, node_count_, "Arc source");
            ensure_node_in_range(arc.v_index, node_count_, "Arc target");
            adjacency[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
        }
        for (auto& successors : adjacency) {
            std::sort(successors.begin(), successors.end());
            successors.erase(std::unique(successors.begin(), successors.end()), successors.end());
        }
        adjacency_by_scenario_.push_back(std::move(adjacency));
    }

    last_burned_by_scenario_.assign(
        instance_.scenarios.size(),
        std::vector<char>(static_cast<std::size_t>(node_count_), 0));
    last_reached_by_scenario_.assign(
        instance_.scenarios.size(),
        std::vector<char>(static_cast<std::size_t>(node_count_), 0));
}

FppRecourseEvaluator::FppRecourseEvaluator(
    const opt::OptimizationInstance& instance,
    const core::LandscapeWeightMap& weight_map)
    : FppRecourseEvaluator(instance) {
    core::validate_landscape_weight_map(weight_map);
    compact_weights_ = core::build_compact_weight_vector(weight_map, instance_.node_mapper);
    validate_compact_weights(compact_weights_, node_count_);
    compact_cluster_ids_ = build_compact_cluster_vector(weight_map, instance_.node_mapper);
    total_landscape_weight_ = weight_map.total_weight;
    total_high_value_cells_ = high_value_cells_from_map(weight_map);
    total_high_value_weight_ = high_value_weight_from_map(weight_map);
    weight_profile_ = weight_map.profile;
    weight_map_hash_ = weight_map.deterministic_hash;
}

FppRecourseResult FppRecourseEvaluator::evaluate(
    const std::vector<int>& selected_firebreak_compact_nodes,
    bool store_node_sets,
    double cvar_beta) const {
    std::vector<char> y_selected(static_cast<std::size_t>(node_count_), 0);
    FppRecourseResult validation_result;
    std::unordered_set<int> seen;
    for (const int compact_node : selected_firebreak_compact_nodes) {
        if (compact_node < 0 || compact_node >= node_count_) {
            validation_result.warnings.push_back(
                "Ignored selected firebreak compact node outside the optimization node range: " +
                std::to_string(compact_node) + ".");
            continue;
        }
        if (!seen.insert(compact_node).second) {
            continue;
        }
        y_selected[static_cast<std::size_t>(compact_node)] = 1;
    }

    auto result = evaluateFromBinaryVector(y_selected, store_node_sets, cvar_beta);
    result.warnings.insert(
        result.warnings.begin(),
        validation_result.warnings.begin(),
        validation_result.warnings.end());
    return result;
}

FppRecourseResult FppRecourseEvaluator::evaluateFromBinaryVector(
    const std::vector<char>& y_selected,
    bool store_node_sets,
    double cvar_beta) const {
    if (y_selected.size() != static_cast<std::size_t>(node_count_)) {
        throw std::runtime_error("FPP recourse evaluator y_selected vector has the wrong size.");
    }

    FppRecourseResult result;
    result.total_landscape_weight = total_landscape_weight_;
    result.total_high_value_cells = total_high_value_cells_;
    result.total_high_value_weight = total_high_value_weight_;
    result.weight_profile = weight_profile_;
    result.weight_map_hash = weight_map_hash_;
    result.scenarios.reserve(instance_.scenarios.size());
    for (int node = 0; node < node_count_; ++node) {
        const auto node_pos = static_cast<std::size_t>(node);
        if (y_selected[node_pos] != 0 && !eligible_[node_pos]) {
            result.warnings.push_back(
                "Selected firebreak compact node is not eligible in the optimization instance: " +
                std::to_string(node) + ".");
        }
    }

    for (auto& burned : last_burned_by_scenario_) {
        std::fill(burned.begin(), burned.end(), 0);
    }
    for (auto& reached : last_reached_by_scenario_) {
        std::fill(reached.begin(), reached.end(), 0);
    }

    std::queue<int> frontier;
    for (std::size_t scenario_index = 0; scenario_index < instance_.scenarios.size(); ++scenario_index) {
        const auto& scenario = instance_.scenarios[scenario_index];
        const auto& adjacency = adjacency_by_scenario_[scenario_index];
        auto& burned = last_burned_by_scenario_[scenario_index];
        auto& reached = last_reached_by_scenario_[scenario_index];
        const int root = scenario.ignition_index;
        const double probability = scenario_probability_at(instance_, scenario_index);

        while (!frontier.empty()) {
            frontier.pop();
        }

        reached[static_cast<std::size_t>(root)] = 1;
        burned[static_cast<std::size_t>(root)] = 1;
        frontier.push(root);

        double burned_count = 1.0;
        double weighted_burn_loss = compact_weights_[static_cast<std::size_t>(root)];
        std::size_t high_value_cells_burned = 0;
        double high_value_weight_burned = 0.0;
        if (compact_cluster_ids_[static_cast<std::size_t>(root)] > 0) {
            high_value_cells_burned = 1;
            high_value_weight_burned = weighted_burn_loss;
        }
        while (!frontier.empty()) {
            const int current = frontier.front();
            frontier.pop();

            for (const int next : adjacency[static_cast<std::size_t>(current)]) {
                const auto next_pos = static_cast<std::size_t>(next);
                if (reached[next_pos]) {
                    continue;
                }
                reached[next_pos] = 1;

                const bool blocked_non_root =
                    next != root && y_selected[next_pos] != 0;
                if (blocked_non_root) {
                    continue;
                }

                burned[next_pos] = 1;
                burned_count += 1.0;
                weighted_burn_loss += compact_weights_[next_pos];
                if (compact_cluster_ids_[next_pos] > 0) {
                    ++high_value_cells_burned;
                    high_value_weight_burned += compact_weights_[next_pos];
                }
                frontier.push(next);
            }
        }

        ScenarioRecourseResult scenario_result;
        scenario_result.scenario_id = scenario.scenario_id;
        scenario_result.probability = probability;
        scenario_result.burned_count = burned_count;
        scenario_result.burned_cell_count = static_cast<std::size_t>(burned_count);
        scenario_result.weighted_burn_loss = weighted_burn_loss;
        scenario_result.high_value_cells_burned = high_value_cells_burned;
        scenario_result.high_value_weight_burned = high_value_weight_burned;
        scenario_result.percentage_landscape_value_burned =
            total_landscape_weight_ > 0.0
                ? 100.0 * weighted_burn_loss / total_landscape_weight_
                : 0.0;
        scenario_result.percentage_high_value_weight_burned =
            total_high_value_weight_ > 0.0
                ? 100.0 * high_value_weight_burned / total_high_value_weight_
                : 0.0;
        if (store_node_sets) {
            for (const int node : scenario.observed_node_indices) {
                ensure_node_in_range(node, node_count_, "Observed scenario");
                const auto node_pos = static_cast<std::size_t>(node);
                if (burned[node_pos]) {
                    scenario_result.burned_nodes.push_back(node);
                    scenario_result.burned_original_cell_ids.push_back(
                        instance_.node_mapper.to_node(node));
                }
                if (reached[node_pos]) {
                    scenario_result.reached_nodes.push_back(node);
                    scenario_result.reached_original_cell_ids.push_back(
                        instance_.node_mapper.to_node(node));
                }
            }
            if (std::find(
                    scenario_result.burned_nodes.begin(),
                    scenario_result.burned_nodes.end(),
                    root) == scenario_result.burned_nodes.end()) {
                scenario_result.burned_nodes.push_back(root);
                scenario_result.burned_original_cell_ids.push_back(
                    instance_.node_mapper.to_node(root));
            }
            if (std::find(
                    scenario_result.reached_nodes.begin(),
                    scenario_result.reached_nodes.end(),
                    root) == scenario_result.reached_nodes.end()) {
                scenario_result.reached_nodes.push_back(root);
                scenario_result.reached_original_cell_ids.push_back(
                    instance_.node_mapper.to_node(root));
            }
            std::sort(scenario_result.burned_nodes.begin(), scenario_result.burned_nodes.end());
            std::sort(scenario_result.reached_nodes.begin(), scenario_result.reached_nodes.end());
            std::sort(
                scenario_result.burned_original_cell_ids.begin(),
                scenario_result.burned_original_cell_ids.end());
            std::sort(
                scenario_result.reached_original_cell_ids.begin(),
                scenario_result.reached_original_cell_ids.end());
        }

        result.expected_burned_area += probability * burned_count;
        result.expected_weighted_burn_loss += probability * weighted_burn_loss;
        result.expected_high_value_weight_burned += probability * high_value_weight_burned;
        result.scenarios.push_back(std::move(scenario_result));
    }

    result.weighted_loss_statistics =
        compute_scenario_weighted_loss_statistics(result.scenarios, cvar_beta);
    if (result.total_landscape_weight > 0.0) {
        result.expected_percentage_landscape_value_burned =
            100.0 * result.expected_weighted_burn_loss / result.total_landscape_weight;
    }
    if (result.total_high_value_weight > 0.0) {
        result.expected_percentage_high_value_weight_burned =
            100.0 * result.expected_high_value_weight_burned / result.total_high_value_weight;
    }

    has_last_evaluation_ = true;
    return result;
}

bool FppRecourseEvaluator::isBurned(int scenario_index, int compact_node) const {
    if (!has_last_evaluation_ ||
        scenario_index < 0 ||
        scenario_index >= static_cast<int>(last_burned_by_scenario_.size()) ||
        compact_node < 0 ||
        compact_node >= node_count_) {
        return false;
    }
    return last_burned_by_scenario_[static_cast<std::size_t>(scenario_index)]
                                   [static_cast<std::size_t>(compact_node)] != 0;
}

bool FppRecourseEvaluator::isReached(int scenario_index, int compact_node) const {
    if (!has_last_evaluation_ ||
        scenario_index < 0 ||
        scenario_index >= static_cast<int>(last_reached_by_scenario_.size()) ||
        compact_node < 0 ||
        compact_node >= node_count_) {
        return false;
    }
    return last_reached_by_scenario_[static_cast<std::size_t>(scenario_index)]
                                    [static_cast<std::size_t>(compact_node)] != 0;
}

}  // namespace firebreak::eval
