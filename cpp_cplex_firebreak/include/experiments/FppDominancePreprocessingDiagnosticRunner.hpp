#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct FppDominancePreprocessingDiagnosticOptions {
    std::string experiment_id = "sub20_alpha001_002_global_dominance_preprocessing";
    std::string case_id;
    unsigned int seed_base = 20260601;
    unsigned int seed = 0;
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<int> train_ids;
    double alpha = -1.0;
    std::filesystem::path output_json_path;
    std::filesystem::path output_csv_path;
};

class FppDominancePreprocessingDiagnosticRunner {
public:
    int run(const FppDominancePreprocessingDiagnosticOptions& options) const;
};

}  // namespace firebreak::experiments
