#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct SmokeOptions {
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<int> scenario_ids;
    std::filesystem::path output_path;
};

class SmokeRunner {
public:
    int run(const SmokeOptions& options) const;
};

}  // namespace firebreak::experiments

