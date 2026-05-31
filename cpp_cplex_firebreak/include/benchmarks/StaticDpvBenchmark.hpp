#pragma once

#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::benchmarks {

struct StaticDpvNodeScore {
    int compact_index = -1;
    int original_node = 0;
    double score = 0.0;
};

struct StaticDpvBenchmarkResult {
    std::vector<int> selected_firebreak_indices;
    std::vector<int> selected_firebreak_original_nodes;
    std::vector<double> selected_scores;
    std::vector<StaticDpvNodeScore> all_scores;
    double total_static_dpv_score = 0.0;
    double runtime_seconds = 0.0;
};

class StaticDpvBenchmark {
public:
    StaticDpvBenchmarkResult run(const opt::OptimizationInstance& opt, int budget) const;
};

}  // namespace firebreak::benchmarks
