#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct FppSaaOptions {
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<int> scenario_ids;
    double alpha = -1.0;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    bool verbose = false;
    std::filesystem::path output_path;
    std::filesystem::path weight_map_file;
};

class FppSaaRunner {
public:
    int run(const FppSaaOptions& options) const;
};

}  // namespace firebreak::experiments
