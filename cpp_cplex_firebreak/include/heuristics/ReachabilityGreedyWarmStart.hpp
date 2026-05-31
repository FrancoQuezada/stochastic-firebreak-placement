#pragma once

#include <string>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::heuristics {

struct ReachabilityGreedyWarmStartOptions {
    int candidate_pool_size_multiplier = 5;
    int candidate_pool_min_size = 50;
    bool enable_greedy_exact_marginal = true;
    bool verbose = false;
    double improvement_tolerance = 1.0e-9;
};

struct ReachabilityGreedyWarmStartResult {
    std::vector<int> selected_firebreak_compact_nodes;
    std::vector<char> y_selected;
    double objective = 0.0;
    double empty_objective = 0.0;
    double runtime_sec = 0.0;
    int iterations = 0;
    int exact_evaluations = 0;
    bool stopped_early = false;
    std::vector<std::string> notes;
};

class ReachabilityGreedyWarmStart {
public:
    ReachabilityGreedyWarmStart(
        const opt::OptimizationInstance& instance,
        ReachabilityGreedyWarmStartOptions options);

    ReachabilityGreedyWarmStartResult run() const;

private:
    const opt::OptimizationInstance& instance_;
    ReachabilityGreedyWarmStartOptions options_;
    std::vector<std::vector<int>> outdegree_by_scenario_;
};

}  // namespace firebreak::heuristics
