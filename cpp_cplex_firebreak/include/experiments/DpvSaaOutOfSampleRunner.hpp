#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct DpvSaaOutOfSampleOptions {
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
    bool verbose = false;
    std::string run_id;

    std::filesystem::path output_json_path;
    std::filesystem::path output_csv_path;
    std::filesystem::path solution_json_path;
    std::filesystem::path solution_csv_path;
    std::filesystem::path warm_start_solution_path;
};

class DpvSaaOutOfSampleRunner {
public:
    int run(const DpvSaaOutOfSampleOptions& options) const;
};

}  // namespace firebreak::experiments
