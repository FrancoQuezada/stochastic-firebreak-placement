#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::benchmarks {

struct StaticDpvMipOptions {
    std::vector<double> downstream_values_by_compact_index;
    std::vector<double> treatment_loss_by_compact_index;
    bool enable_treatment_loss_constraint = false;
    double treatment_loss_beta = std::numeric_limits<double>::quiet_NaN();
    double baseline_expected_loss = std::numeric_limits<double>::quiet_NaN();
};

struct StaticDpvMipNodeScore {
    int compact_index = -1;
    int original_node = 0;
    double dpv_score = 0.0;
    double treatment_loss = 0.0;
};

struct StaticDpvMipBenchmarkResult {
    std::vector<int> selected_firebreak_indices;
    std::vector<int> selected_firebreak_original_nodes;
    std::vector<double> selected_scores;
    std::vector<StaticDpvMipNodeScore> all_scores;
    double total_static_dpv_score = 0.0;
    double runtime_seconds = 0.0;
    std::string solver_status = "ExactTopBudget";
    std::size_t num_variables = 0;
    std::size_t num_constraints = 1;
};

class StaticDpvMipBenchmark {
public:
    StaticDpvMipBenchmarkResult run(
        const opt::OptimizationInstance& opt,
        int budget,
        const StaticDpvMipOptions& options = {}) const;
};

}  // namespace firebreak::benchmarks
