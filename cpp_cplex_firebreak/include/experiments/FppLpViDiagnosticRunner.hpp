#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct FppLpViDiagnosticOptions {
    std::string landscape = "Sub20";
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<double> alphas;
    std::size_t train_count = 100;
    std::size_t test_count = 100;
    std::size_t num_cases = 5;
    unsigned int seed_base = 20260520;
    double time_limit_seconds = 300.0;
    int threads = 1;
    bool verbose = false;
    std::filesystem::path output_dir = "results/batch/fpp_lp_vi_diagnostic";

    int max_aggregate_dominator_cuts_per_scenario = 50;
    int max_individual_dominator_cuts_per_scenario = 100;

    int offline_sep_max_rounds = 10;
    int offline_sep_max_cuts_per_round = 500;
    double offline_sep_min_violation = 1.0e-6;
    int offline_sep_max_scenarios_per_round = 0;
    int offline_sep_max_nodes_per_scenario = 50;
    int offline_sep_max_cut_cardinality = 50;
};

class FppLpViDiagnosticRunner {
public:
    int run(const FppLpViDiagnosticOptions& options) const;
};

}  // namespace firebreak::experiments
