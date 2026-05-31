#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::solver {

struct WarmStart {
    bool enabled = false;
    std::string source_path;
    std::vector<int> original_node_ids;
    std::vector<int> compact_indices;
    std::vector<int> ignored_original_node_ids;
    std::vector<int> duplicate_original_node_ids;
    std::vector<int> trimmed_original_node_ids;
    std::string status;
    std::vector<std::string> notes;
};

WarmStart prepare_warm_start_from_original_nodes(
    const std::vector<int>& original_node_ids,
    const opt::OptimizationInstance& opt,
    int budget,
    const std::string& source_path);

WarmStart load_warm_start_from_csv(
    const std::filesystem::path& input_path,
    const opt::OptimizationInstance& opt,
    int budget);

}  // namespace firebreak::solver
