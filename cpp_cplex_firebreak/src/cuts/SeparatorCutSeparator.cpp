#include "cuts/SeparatorCutSeparator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <utility>

namespace firebreak::cuts {
namespace {

constexpr double kValueEpsilon = 1.0e-9;

int max_compact_node_in_instance(const opt::OptimizationInstance& instance) {
    int max_node = -1;
    for (const int node : instance.eligible_indices) {
        max_node = std::max(max_node, node);
    }
    for (const auto& scenario : instance.scenarios) {
        max_node = std::max(max_node, scenario.ignition_index);
        for (const int node : scenario.observed_node_indices) {
            max_node = std::max(max_node, node);
        }
        for (const auto& arc : scenario.arcs) {
            max_node = std::max(max_node, std::max(arc.u_index, arc.v_index));
        }
    }
    return max_node;
}

struct RankedScenario {
    int scenario_index = -1;
    double score = 0.0;
};

struct RankedTarget {
    int compact_node = -1;
    double score = 0.0;
};

bool better_scenario(const RankedScenario& lhs, const RankedScenario& rhs) {
    if (std::fabs(lhs.score - rhs.score) > kValueEpsilon) {
        return lhs.score > rhs.score;
    }
    return lhs.scenario_index < rhs.scenario_index;
}

bool better_target(const RankedTarget& lhs, const RankedTarget& rhs) {
    if (std::fabs(lhs.score - rhs.score) > kValueEpsilon) {
        return lhs.score > rhs.score;
    }
    return lhs.compact_node < rhs.compact_node;
}

double clamped_unit_value(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, value));
}

}  // namespace

SeparatorCutSeparator::SeparatorCutSeparator(
    const opt::OptimizationInstance& instance,
    SeparatorCutOptions options)
    : instance_(instance),
      options_(std::move(options)),
      separator_engine_(instance) {
    const int max_node = max_compact_node_in_instance(instance_);
    if (max_node >= 0) {
        eligible_by_node_.assign(static_cast<std::size_t>(max_node + 1), 0);
    }
    for (const int node : instance_.eligible_indices) {
        if (node >= 0) {
            if (node >= static_cast<int>(eligible_by_node_.size())) {
                eligible_by_node_.resize(static_cast<std::size_t>(node + 1), 0);
            }
            eligible_by_node_[static_cast<std::size_t>(node)] = 1;
        }
    }
}

std::vector<CandidateSeparatorCut> SeparatorCutSeparator::separate(
    const std::vector<double>& ybar_by_compact_node,
    const std::vector<std::vector<double>>& xbar_by_scenario_and_compact_node,
    int max_cuts_override) {
    const auto start = std::chrono::steady_clock::now();
    std::vector<CandidateSeparatorCut> cuts;
    const int max_cuts =
        max_cuts_override > 0
            ? max_cuts_override
            : options_.sep_max_cuts_per_call;

    std::vector<RankedScenario> scenario_scores;
    scenario_scores.reserve(instance_.scenarios.size());
    for (std::size_t s = 0; s < instance_.scenarios.size(); ++s) {
        const auto& scenario = instance_.scenarios[s];
        double burn_sum = 0.0;
        for (const int compact_node : scenario.observed_node_indices) {
            burn_sum += xValue(xbar_by_scenario_and_compact_node, s, compact_node);
        }
        const double score = scenarioProbability(s) * burn_sum;
        if (score > options_.sep_min_violation) {
            scenario_scores.push_back(RankedScenario{static_cast<int>(s), score});
        }
    }

    std::sort(scenario_scores.begin(), scenario_scores.end(), better_scenario);
    if (options_.sep_max_scenarios_per_call > 0 &&
        static_cast<int>(scenario_scores.size()) > options_.sep_max_scenarios_per_call) {
        scenario_scores.resize(static_cast<std::size_t>(options_.sep_max_scenarios_per_call));
    }

    for (const RankedScenario& ranked_scenario : scenario_scores) {
        if (max_cuts > 0 && static_cast<int>(cuts.size()) >= max_cuts) {
            break;
        }

        const int scenario_index = ranked_scenario.scenario_index;
        const auto& scenario = instance_.scenarios[static_cast<std::size_t>(scenario_index)];
        std::vector<RankedTarget> targets;
        targets.reserve(scenario.observed_node_indices.size());
        for (const int target : scenario.observed_node_indices) {
            if (target == scenario.ignition_index) {
                continue;
            }
            const double x_target =
                xValue(xbar_by_scenario_and_compact_node, static_cast<std::size_t>(scenario_index), target);
            if (x_target <= options_.sep_min_violation) {
                continue;
            }
            targets.push_back(RankedTarget{
                target,
                scenarioProbability(static_cast<std::size_t>(scenario_index)) * x_target});
        }

        std::sort(targets.begin(), targets.end(), better_target);
        if (options_.sep_max_nodes_per_scenario > 0 &&
            static_cast<int>(targets.size()) > options_.sep_max_nodes_per_scenario) {
            targets.resize(static_cast<std::size_t>(options_.sep_max_nodes_per_scenario));
        }

        for (const RankedTarget& target : targets) {
            if (max_cuts > 0 && static_cast<int>(cuts.size()) >= max_cuts) {
                break;
            }

            ++stats_.min_cut_calls;
            const auto separator =
                separator_engine_.compute(scenario_index, target.compact_node, ybar_by_compact_node);
            if (!separator.feasible || separator.separator_compact_nodes.empty()) {
                continue;
            }
            if (!separatorIsUsable(scenario, separator.separator_compact_nodes)) {
                continue;
            }

            double separator_capacity = 0.0;
            double separator_y_sum = 0.0;
            for (const int compact_node : separator.separator_compact_nodes) {
                const double y = yValue(ybar_by_compact_node, compact_node);
                separator_capacity += 1.0 - y;
                separator_y_sum += y;
            }

            const double x_target =
                xValue(xbar_by_scenario_and_compact_node, static_cast<std::size_t>(scenario_index), target.compact_node);
            const double lhs_value = x_target + separator_y_sum;
            const double rhs_value = static_cast<double>(separator.separator_compact_nodes.size());
            const double violation = lhs_value - rhs_value;
            stats_.max_cut_violation = std::max(stats_.max_cut_violation, violation);
            if (violation <= options_.sep_min_violation) {
                continue;
            }

            if (options_.sep_max_cut_cardinality > 0 &&
                static_cast<int>(separator.separator_compact_nodes.size()) > options_.sep_max_cut_cardinality &&
                violation <= 10.0 * options_.sep_min_violation) {
                ++stats_.large_cuts_skipped;
                continue;
            }

            const std::string key = cutKey(
                scenario_index,
                target.compact_node,
                separator.separator_compact_nodes);
            if (std::binary_search(cut_cache_.begin(), cut_cache_.end(), key)) {
                ++stats_.duplicate_cuts_skipped;
                continue;
            }
            cut_cache_.insert(std::upper_bound(cut_cache_.begin(), cut_cache_.end(), key), key);

            cuts.push_back(CandidateSeparatorCut{
                scenario_index,
                target.compact_node,
                separator.separator_compact_nodes,
                separator_capacity,
                lhs_value,
                rhs_value,
                violation,
            });
        }
    }

    stats_.cuts_added += static_cast<int>(cuts.size());
    const auto end = std::chrono::steady_clock::now();
    stats_.separator_time_sec += std::chrono::duration<double>(end - start).count();
    return cuts;
}

const SeparatorCutStats& SeparatorCutSeparator::stats() const {
    return stats_;
}

void SeparatorCutSeparator::resetStats() {
    stats_ = SeparatorCutStats{};
}

void SeparatorCutSeparator::clearCutCache() {
    cut_cache_.clear();
}

bool SeparatorCutSeparator::isEligible(int compact_node) const {
    return compact_node >= 0 &&
           compact_node < static_cast<int>(eligible_by_node_.size()) &&
           eligible_by_node_[static_cast<std::size_t>(compact_node)] != 0;
}

bool SeparatorCutSeparator::separatorIsUsable(
    const opt::OptimizationScenario& scenario,
    const std::vector<int>& separator) const {
    for (const int compact_node : separator) {
        if (compact_node == scenario.ignition_index || !isEligible(compact_node)) {
            return false;
        }
    }
    return true;
}

double SeparatorCutSeparator::yValue(
    const std::vector<double>& ybar_by_compact_node,
    int compact_node) const {
    if (compact_node < 0 || compact_node >= static_cast<int>(ybar_by_compact_node.size())) {
        return 0.0;
    }
    return clamped_unit_value(ybar_by_compact_node[static_cast<std::size_t>(compact_node)]);
}

double SeparatorCutSeparator::xValue(
    const std::vector<std::vector<double>>& xbar_by_scenario_and_compact_node,
    std::size_t scenario_index,
    int compact_node) const {
    if (scenario_index >= xbar_by_scenario_and_compact_node.size() ||
        compact_node < 0 ||
        compact_node >= static_cast<int>(xbar_by_scenario_and_compact_node[scenario_index].size())) {
        return 0.0;
    }
    return clamped_unit_value(
        xbar_by_scenario_and_compact_node[scenario_index][static_cast<std::size_t>(compact_node)]);
}

double SeparatorCutSeparator::scenarioProbability(std::size_t scenario_index) const {
    const double scenario_probability = instance_.scenarios[scenario_index].probability;
    if (std::isfinite(scenario_probability)) {
        return scenario_probability;
    }
    if (scenario_index < instance_.scenario_probabilities.size() &&
        std::isfinite(instance_.scenario_probabilities[scenario_index])) {
        return instance_.scenario_probabilities[scenario_index];
    }
    return 0.0;
}

std::string SeparatorCutSeparator::cutKey(
    int scenario_index,
    int target_compact_node,
    const std::vector<int>& separator) const {
    std::ostringstream key;
    key << scenario_index << "|" << target_compact_node << "|";
    for (std::size_t i = 0; i < separator.size(); ++i) {
        if (i > 0) {
            key << ",";
        }
        key << separator[i];
    }
    return key.str();
}

}  // namespace firebreak::cuts
