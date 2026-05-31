#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct GraphDiagnosticsOptions {
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<int> scenario_ids;
    std::filesystem::path output_path;
};

class GraphDiagnosticsRunner {
public:
    int run(const GraphDiagnosticsOptions& options) const;
};

}  // namespace firebreak::experiments

