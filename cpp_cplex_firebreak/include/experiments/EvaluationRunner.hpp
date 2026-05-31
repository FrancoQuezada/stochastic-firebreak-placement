#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct EvaluationOptions {
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<int> scenario_ids;
    std::string firebreaks_csv;
    std::filesystem::path output_path;
};

class EvaluationRunner {
public:
    int run(const EvaluationOptions& options) const;
};

}  // namespace firebreak::experiments

