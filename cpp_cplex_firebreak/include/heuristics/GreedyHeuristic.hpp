#pragma once

#include <string>
#include <vector>

#include "heuristics/GreedyMetrics.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::heuristics {

struct GreedyResult {
    std::string method_name;
    std::string objective_metric;
    std::vector<int> selected_firebreak_indices;
    std::vector<int> selected_firebreak_original_nodes;
    std::vector<double> selected_scores;
    double total_score = 0.0;
    double runtime_seconds = 0.0;
    std::vector<std::string> metric_notes;
};

class GreedyHeuristic {
public:
    GreedyResult runGreedy(
        const opt::OptimizationInstance& opt,
        GreedyMetricType metric,
        bool recompute_each_iteration,
        bool verbose) const;
};

}  // namespace firebreak::heuristics
