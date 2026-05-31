#include "heuristics/ReachabilityGreedyWarmStart.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "eval/FppRecourseEvaluator.hpp"

namespace firebreak::heuristics {

namespace {

constexpr double kSuccessorScreeningWeight = 1.0e-3;

struct CandidateScore {
    int compact_node = -1;
    double score = 0.0;
};

void validate_input(const opt::OptimizationInstance& instance) {
    if (instance.node_mapper.size() <= 0) {
        throw std::runtime_error("Reachability-greedy warm start requires at least one mapped node.");
    }
    if (instance.scenarios.empty()) {
        throw std::runtime_error("Reachability-greedy warm start requires at least one scenario.");
    }
    if (instance.eligible_indices.empty()) {
        throw std::runtime_error("Reachability-greedy warm start requires at least one eligible firebreak node.");
    }
    if (instance.budget < 0) {
        throw std::runtime_error("Reachability-greedy warm-start budget must be nonnegative.");
    }
    if (instance.budget > static_cast<int>(instance.eligible_indices.size())) {
        throw std::runtime_error(
            "Reachability-greedy warm-start budget exceeds the number of eligible firebreak nodes.");
    }
}

std::vector<std::vector<int>> build_outdegrees_by_scenario(const opt::OptimizationInstance& instance) {
    const int node_count = instance.node_mapper.size();
    std::vector<std::vector<int>> outdegrees;
    outdegrees.reserve(instance.scenarios.size());

    for (const auto& scenario : instance.scenarios) {
        std::vector<std::vector<int>> successors(static_cast<std::size_t>(node_count));
        for (const auto& arc : scenario.arcs) {
            if (arc.u_index < 0 || arc.u_index >= node_count || arc.v_index < 0 || arc.v_index >= node_count) {
                throw std::runtime_error("Reachability-greedy warm start found an out-of-range scenario arc.");
            }
            successors[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
        }

        std::vector<int> scenario_outdegree(static_cast<std::size_t>(node_count), 0);
        for (int node = 0; node < node_count; ++node) {
            auto& node_successors = successors[static_cast<std::size_t>(node)];
            std::sort(node_successors.begin(), node_successors.end());
            node_successors.erase(
                std::unique(node_successors.begin(), node_successors.end()),
                node_successors.end());
            scenario_outdegree[static_cast<std::size_t>(node)] = static_cast<int>(node_successors.size());
        }
        outdegrees.push_back(std::move(scenario_outdegree));
    }

    return outdegrees;
}

int exact_all_threshold(const ReachabilityGreedyWarmStartOptions& options) {
    const int product = options.candidate_pool_min_size * options.candidate_pool_size_multiplier;
    return std::max(1, product);
}

int screened_pool_size(const ReachabilityGreedyWarmStartOptions& options, int budget, int candidate_count) {
    const int budget_scaled = options.candidate_pool_size_multiplier * std::max(1, budget);
    const int requested = std::max(1, std::max(options.candidate_pool_min_size, budget_scaled));
    return std::min(requested, candidate_count);
}

std::vector<int> unselected_eligible_candidates(
    const opt::OptimizationInstance& instance,
    const std::vector<char>& y_selected) {
    std::vector<int> candidates;
    candidates.reserve(instance.eligible_indices.size());
    for (const int compact_node : instance.eligible_indices) {
        if (compact_node < 0 || compact_node >= static_cast<int>(y_selected.size())) {
            throw std::runtime_error("Eligible compact node is out of range for reachability-greedy warm start.");
        }
        if (!y_selected[static_cast<std::size_t>(compact_node)]) {
            candidates.push_back(compact_node);
        }
    }
    return candidates;
}

std::vector<int> build_screened_candidate_pool(
    const opt::OptimizationInstance& instance,
    const std::vector<int>& candidates,
    const eval::FppRecourseEvaluator& evaluator,
    const eval::FppRecourseResult& current_recourse,
    const std::vector<std::vector<int>>& outdegree_by_scenario,
    int pool_size) {
    std::vector<char> is_candidate(static_cast<std::size_t>(instance.node_mapper.size()), 0);
    for (const int candidate : candidates) {
        is_candidate[static_cast<std::size_t>(candidate)] = 1;
    }

    std::vector<double> score_by_node(static_cast<std::size_t>(instance.node_mapper.size()), 0.0);
    for (std::size_t scenario_index = 0; scenario_index < current_recourse.scenarios.size(); ++scenario_index) {
        const auto& scenario_result = current_recourse.scenarios[scenario_index];
        const bool has_observed_node_sets =
            scenario_index < instance.scenarios.size() &&
            !instance.scenarios[scenario_index].observed_node_indices.empty();

        if (has_observed_node_sets) {
            for (const int compact_node : scenario_result.burned_nodes) {
                if (compact_node < 0 || compact_node >= static_cast<int>(score_by_node.size())) {
                    continue;
                }
                if (!is_candidate[static_cast<std::size_t>(compact_node)]) {
                    continue;
                }
                if (compact_node == instance.scenarios[scenario_index].ignition_index) {
                    continue;
                }
                score_by_node[static_cast<std::size_t>(compact_node)] += scenario_result.probability;
                score_by_node[static_cast<std::size_t>(compact_node)] +=
                    kSuccessorScreeningWeight *
                    scenario_result.probability *
                    outdegree_by_scenario[scenario_index][static_cast<std::size_t>(compact_node)];
            }
        } else {
            for (const int compact_node : candidates) {
                if (!evaluator.isBurned(static_cast<int>(scenario_index), compact_node)) {
                    continue;
                }
                if (compact_node == instance.scenarios[scenario_index].ignition_index) {
                    continue;
                }
                score_by_node[static_cast<std::size_t>(compact_node)] += scenario_result.probability;
                score_by_node[static_cast<std::size_t>(compact_node)] +=
                    kSuccessorScreeningWeight *
                    scenario_result.probability *
                    outdegree_by_scenario[scenario_index][static_cast<std::size_t>(compact_node)];
            }
        }
    }

    std::vector<CandidateScore> scores;
    scores.reserve(candidates.size());
    for (const int candidate : candidates) {
        scores.push_back(CandidateScore{
            candidate,
            score_by_node[static_cast<std::size_t>(candidate)],
        });
    }

    std::sort(scores.begin(), scores.end(), [](const CandidateScore& lhs, const CandidateScore& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.compact_node < rhs.compact_node;
    });

    std::vector<int> pool;
    pool.reserve(static_cast<std::size_t>(pool_size));
    for (int i = 0; i < pool_size && i < static_cast<int>(scores.size()); ++i) {
        pool.push_back(scores[static_cast<std::size_t>(i)].compact_node);
    }
    return pool;
}

bool better_candidate(
    double delta,
    int candidate,
    double best_delta,
    int best_candidate,
    double tolerance) {
    if (delta > best_delta + tolerance) {
        return true;
    }
    if (std::fabs(delta - best_delta) <= tolerance &&
        delta > tolerance &&
        (best_candidate < 0 || candidate < best_candidate)) {
        return true;
    }
    return false;
}

std::string format_double(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

}  // namespace

ReachabilityGreedyWarmStart::ReachabilityGreedyWarmStart(
    const opt::OptimizationInstance& instance,
    ReachabilityGreedyWarmStartOptions options)
    : instance_(instance),
      options_(options),
      outdegree_by_scenario_(build_outdegrees_by_scenario(instance)) {
    validate_input(instance_);
    if (options_.candidate_pool_size_multiplier < 0) {
        throw std::runtime_error("candidate_pool_size_multiplier must be nonnegative.");
    }
    if (options_.candidate_pool_min_size < 0) {
        throw std::runtime_error("candidate_pool_min_size must be nonnegative.");
    }
    if (options_.improvement_tolerance < 0.0) {
        throw std::runtime_error("Reachability-greedy improvement tolerance must be nonnegative.");
    }
}

ReachabilityGreedyWarmStartResult ReachabilityGreedyWarmStart::run() const {
    const auto start = std::chrono::steady_clock::now();
    const int node_count = instance_.node_mapper.size();
    const int budget = instance_.budget;

    eval::FppRecourseEvaluator evaluator(instance_);

    ReachabilityGreedyWarmStartResult result;
    result.y_selected.assign(static_cast<std::size_t>(node_count), 0);

    auto current_recourse = evaluator.evaluateFromBinaryVector(result.y_selected, true);
    result.empty_objective = current_recourse.expected_burned_area;
    result.objective = current_recourse.expected_burned_area;

    if (budget == 0) {
        result.stopped_early = true;
        result.notes.push_back("Reachability-greedy warm start stopped immediately because the budget is zero.");
    }

    for (int step = 0; step < budget; ++step) {
        const auto candidates = unselected_eligible_candidates(instance_, result.y_selected);
        if (candidates.empty()) {
            result.stopped_early = true;
            result.notes.push_back("Reachability-greedy warm start stopped because no eligible candidates remained.");
            break;
        }

        std::vector<int> candidate_pool;
        const bool use_exact_all =
            options_.enable_greedy_exact_marginal &&
            static_cast<int>(candidates.size()) <= exact_all_threshold(options_);
        if (use_exact_all) {
            candidate_pool = candidates;
        } else {
            candidate_pool = build_screened_candidate_pool(
                instance_,
                candidates,
                evaluator,
                current_recourse,
                outdegree_by_scenario_,
                screened_pool_size(options_, budget, static_cast<int>(candidates.size())));
        }

        int best_candidate = -1;
        double best_delta = 0.0;
        double best_objective = result.objective;
        for (const int candidate : candidate_pool) {
            result.y_selected[static_cast<std::size_t>(candidate)] = 1;
            const auto trial = evaluator.evaluateFromBinaryVector(result.y_selected, false);
            ++result.exact_evaluations;
            result.y_selected[static_cast<std::size_t>(candidate)] = 0;

            const double delta = result.objective - trial.expected_burned_area;
            if (better_candidate(
                    delta,
                    candidate,
                    best_delta,
                    best_candidate,
                    options_.improvement_tolerance)) {
                best_candidate = candidate;
                best_delta = delta;
                best_objective = trial.expected_burned_area;
            }
        }

        if (best_candidate < 0 || best_delta <= options_.improvement_tolerance) {
            result.stopped_early = true;
            result.notes.push_back(
                "Reachability-greedy warm start stopped because no candidate had positive marginal improvement.");
            break;
        }

        result.y_selected[static_cast<std::size_t>(best_candidate)] = 1;
        result.selected_firebreak_compact_nodes.push_back(best_candidate);
        result.objective = best_objective;
        ++result.iterations;

        if (options_.verbose) {
            std::cout << "Reachability-greedy warm start step " << result.iterations
                      << " selected compact node " << best_candidate
                      << " original node " << instance_.node_mapper.to_node(best_candidate)
                      << " improvement " << best_delta
                      << " objective " << result.objective << "\n";
        }

        current_recourse = evaluator.evaluateFromBinaryVector(result.y_selected, true);
        result.objective = current_recourse.expected_burned_area;
    }

    const auto end = std::chrono::steady_clock::now();
    result.runtime_sec = std::chrono::duration<double>(end - start).count();
    result.notes.push_back(
        "Reachability-greedy warm start empty objective: " + format_double(result.empty_objective) + ".");
    result.notes.push_back(
        "Reachability-greedy warm start final objective: " + format_double(result.objective) + ".");
    result.notes.push_back(
        "Reachability-greedy warm start exact marginal evaluations: " +
        std::to_string(result.exact_evaluations) + ".");
    return result;
}

}  // namespace firebreak::heuristics
