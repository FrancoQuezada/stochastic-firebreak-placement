#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct OptInstanceOptions {
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<int> scenario_ids;
    double alpha = -1.0;
    std::filesystem::path output_path;
};

class OptInstanceRunner {
public:
    int run(const OptInstanceOptions& options) const;
};

}  // namespace firebreak::experiments

