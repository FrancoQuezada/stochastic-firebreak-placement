#pragma once

#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"
#include "benders/FppStrengthening.hpp"
#include "risk/RiskMeasure.hpp"

namespace firebreak::experiments {

struct FppBranchBendersOutOfSampleOptions {
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;

    std::vector<int> train_ids;
    std::vector<int> test_ids;
    bool use_generated_split = false;
    unsigned int seed = 0;
    std::size_t train_count = 0;
    std::size_t test_count = 0;

    double alpha = -1.0;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    double tolerance = 1.0e-6;
    bool verbose = false;
    risk::RiskMeasureConfig risk_config;
    bool use_lifted_lower_bounds = false;
    bool use_root_user_cuts = false;
    int root_user_cut_max_rounds = 1;
    double root_user_cut_tolerance = std::numeric_limits<double>::quiet_NaN();
    benders::FppCombinatorialBendersOptions combinatorial_options;
    benders::FppStrengtheningOptions strengthening_options;
    std::string run_id;

    std::filesystem::path output_json_path;
    std::filesystem::path output_csv_path;
    std::filesystem::path solution_json_path;
    std::filesystem::path solution_csv_path;
};

class FppBranchBendersOutOfSampleRunner {
public:
    int run(const FppBranchBendersOutOfSampleOptions& options) const;
};

}  // namespace firebreak::experiments
