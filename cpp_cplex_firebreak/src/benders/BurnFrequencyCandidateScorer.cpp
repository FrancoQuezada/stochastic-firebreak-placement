#include "benders/BurnFrequencyCandidateScorer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "benders/RestrictedCandidateManager.hpp"

namespace firebreak::benders {

namespace {

void validate_instance_for_burn_frequency(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("Burn-frequency scoring requires at least one mapped node.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("Burn-frequency scoring requires at least one eligible candidate.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("Burn-frequency scoring requires at least one scenario.");
    }
    for (const int compact_index : opt.eligible_indices) {
        if (compact_index < 0 || compact_index >= opt.node_mapper.size()) {
            throw std::runtime_error("Burn-frequency scoring found an eligible index outside the mapped node range.");
        }
    }
}

bool scenario_probabilities_are_usable(const opt::OptimizationInstance& opt) {
    double total = 0.0;
    for (const auto& scenario : opt.scenarios) {
        if (!std::isfinite(scenario.probability) || scenario.probability < 0.0) {
            return false;
        }
        total += scenario.probability;
    }
    return total > 0.0;
}

std::vector<double> compact_weights_or_unit(const opt::OptimizationInstance& opt) {
    if (opt.compact_cell_weights.empty()) {
        return std::vector<double>(static_cast<std::size_t>(opt.node_mapper.size()), 1.0);
    }
    if (opt.compact_cell_weights.size() !=
        static_cast<std::size_t>(opt.node_mapper.size())) {
        throw std::runtime_error(
            "Burn-frequency weighted scoring requires one compact weight per mapped node.");
    }
    for (const double weight : opt.compact_cell_weights) {
        if (!std::isfinite(weight) || weight <= 0.0) {
            throw std::runtime_error(
                "Burn-frequency weighted scoring requires finite positive compact weights.");
        }
    }
    return opt.compact_cell_weights;
}

std::vector<char> reachable_nodes_without_firebreaks(
    const opt::OptimizationInstance& opt,
    const opt::OptimizationScenario& scenario) {
    const int node_count = opt.node_mapper.size();
    if (scenario.ignition_index < 0 || scenario.ignition_index >= node_count) {
        throw std::runtime_error("Burn-frequency scoring found a scenario ignition outside the mapped node range.");
    }

    std::vector<std::vector<int>> outgoing(static_cast<std::size_t>(node_count));
    for (const auto& arc : scenario.arcs) {
        if (arc.u_index < 0 || arc.u_index >= node_count ||
            arc.v_index < 0 || arc.v_index >= node_count) {
            throw std::runtime_error("Burn-frequency scoring found an arc outside the mapped node range.");
        }
        outgoing[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
    }

    std::vector<char> reachable(static_cast<std::size_t>(node_count), 0);
    std::vector<int> stack;
    stack.push_back(scenario.ignition_index);
    reachable[static_cast<std::size_t>(scenario.ignition_index)] = 1;

    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        for (const int next : outgoing[static_cast<std::size_t>(node)]) {
            if (reachable[static_cast<std::size_t>(next)] != 0) {
                continue;
            }
            reachable[static_cast<std::size_t>(next)] = 1;
            stack.push_back(next);
        }
    }
    return reachable;
}

}  // namespace

std::vector<BurnFrequencyCandidateScore> BurnFrequencyCandidateScorer::scoreDetailed(
    const opt::OptimizationInstance& opt) const {
    validate_instance_for_burn_frequency(opt);

    std::vector<BurnFrequencyCandidateScore> scores;
    scores.reserve(opt.eligible_indices.size());
    const auto compact_weights = compact_weights_or_unit(opt);
    const bool weighted = !opt.compact_cell_weights.empty();
    const std::string weight_map_hash = opt.cell_weight_map.deterministic_hash;
    for (std::size_t candidate = 0; candidate < opt.eligible_indices.size(); ++candidate) {
        const int compact_index = opt.eligible_indices[candidate];
        BurnFrequencyCandidateScore score;
        score.candidate = static_cast<int>(candidate);
        score.compact_index = compact_index;
        score.original_node = opt.node_mapper.to_node(compact_index);
        score.cell_weight = compact_weights[static_cast<std::size_t>(compact_index)];
        score.weighted = weighted;
        score.weight_map_hash = weight_map_hash;
        scores.push_back(score);
    }

    const bool use_probabilities = scenario_probabilities_are_usable(opt);
    for (const auto& scenario : opt.scenarios) {
        const double weight = use_probabilities ? scenario.probability : 1.0;
        const auto reachable = reachable_nodes_without_firebreaks(opt, scenario);
        for (auto& score : scores) {
            if (reachable[static_cast<std::size_t>(score.compact_index)] != 0) {
                score.burn_frequency += weight;
                score.score += weight * score.cell_weight;
                ++score.scenarios_burned;
            }
        }
    }

    for (const auto& score : scores) {
        if (!std::isfinite(score.score) || score.score < 0.0) {
            throw std::runtime_error("Burn-frequency scoring produced an invalid score.");
        }
    }
    return scores;
}

std::vector<std::pair<int, double>> BurnFrequencyCandidateScorer::scoreCandidates(
    const opt::OptimizationInstance& opt) const {
    const auto detailed = scoreDetailed(opt);
    std::vector<std::pair<int, double>> scores;
    scores.reserve(detailed.size());
    for (const auto& score : detailed) {
        scores.push_back({score.candidate, score.score});
    }
    return scores;
}

std::vector<int> makeInitialActiveSetFromBurnFrequency(
    const opt::OptimizationInstance& opt,
    int initial_size) {
    BurnFrequencyCandidateScorer scorer;
    return makeInitialActiveSetFromScores(
        static_cast<int>(opt.eligible_indices.size()),
        opt.budget,
        scorer.scoreCandidates(opt),
        initial_size);
}

std::vector<std::pair<int, double>> topBurnFrequencyCandidates(
    const std::vector<std::pair<int, double>>& scores,
    int limit) {
    if (limit < 0) {
        throw std::invalid_argument("topBurnFrequencyCandidates requires limit >= 0.");
    }
    std::vector<std::pair<int, double>> sorted = scores;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });
    if (static_cast<int>(sorted.size()) > limit) {
        sorted.resize(static_cast<std::size_t>(limit));
    }
    return sorted;
}

}  // namespace firebreak::benders
