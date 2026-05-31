#include "eval/FppRecourseEvaluator.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <unordered_set>

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

}  // namespace

FppRecourseEvaluator::FppRecourseEvaluator(const opt::OptimizationInstance& instance)
    : instance_(instance),
      node_count_(instance.node_mapper.size()) {
    if (node_count_ <= 0) {
        throw std::runtime_error("FPP recourse evaluator requires at least one mapped node.");
    }
    if (instance_.scenarios.empty()) {
        throw std::runtime_error("FPP recourse evaluator requires at least one scenario.");
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

FppRecourseResult FppRecourseEvaluator::evaluate(
    const std::vector<int>& selected_firebreak_compact_nodes,
    bool store_node_sets) const {
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

    auto result = evaluateFromBinaryVector(y_selected, store_node_sets);
    result.warnings.insert(
        result.warnings.begin(),
        validation_result.warnings.begin(),
        validation_result.warnings.end());
    return result;
}

FppRecourseResult FppRecourseEvaluator::evaluateFromBinaryVector(
    const std::vector<char>& y_selected,
    bool store_node_sets) const {
    if (y_selected.size() != static_cast<std::size_t>(node_count_)) {
        throw std::runtime_error("FPP recourse evaluator y_selected vector has the wrong size.");
    }

    FppRecourseResult result;
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
                frontier.push(next);
            }
        }

        ScenarioRecourseResult scenario_result;
        scenario_result.scenario_id = scenario.scenario_id;
        scenario_result.probability = probability;
        scenario_result.burned_count = burned_count;
        if (store_node_sets) {
            for (const int node : scenario.observed_node_indices) {
                ensure_node_in_range(node, node_count_, "Observed scenario");
                const auto node_pos = static_cast<std::size_t>(node);
                if (burned[node_pos]) {
                    scenario_result.burned_nodes.push_back(node);
                }
                if (reached[node_pos]) {
                    scenario_result.reached_nodes.push_back(node);
                }
            }
            if (std::find(
                    scenario_result.burned_nodes.begin(),
                    scenario_result.burned_nodes.end(),
                    root) == scenario_result.burned_nodes.end()) {
                scenario_result.burned_nodes.push_back(root);
            }
            if (std::find(
                    scenario_result.reached_nodes.begin(),
                    scenario_result.reached_nodes.end(),
                    root) == scenario_result.reached_nodes.end()) {
                scenario_result.reached_nodes.push_back(root);
            }
            std::sort(scenario_result.burned_nodes.begin(), scenario_result.burned_nodes.end());
            std::sort(scenario_result.reached_nodes.begin(), scenario_result.reached_nodes.end());
        }

        result.expected_burned_area += probability * burned_count;
        result.scenarios.push_back(std::move(scenario_result));
    }

    has_last_evaluation_ = true;
    // Unit burned area is used here to match the current FPP objective. TODO:
    // thread node weights through OptimizationInstance if weighted burned area
    // is introduced later.
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
