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
    std::filesystem::path weight_map_file;
    double cvar_beta = 0.9;
    // When true, any selected firebreak original ID absent from this instance's compact
    // evaluation universe is a hard error instead of a dropped-with-warning. Paired
    // reburn evaluation always sets this so a missing selected cell is never silently
    // ignored.
    bool require_full_firebreak_coverage = false;
};

class EvaluationRunner {
public:
    int run(const EvaluationOptions& options) const;
};

}  // namespace firebreak::experiments
