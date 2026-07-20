#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct StaticDpvOutOfSampleOptions {
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
    std::string run_id;

    std::filesystem::path output_json_path;
    std::filesystem::path output_csv_path;
    std::filesystem::path solution_json_path;
    std::filesystem::path solution_csv_path;
    std::filesystem::path weight_map_file;
    std::string dpv_ignition_policy = "fpp-safe";

    bool use_static_dpv_mip = false;
};

class StaticDpvOutOfSampleRunner {
public:
    int run(const StaticDpvOutOfSampleOptions& options) const;
};

}  // namespace firebreak::experiments
