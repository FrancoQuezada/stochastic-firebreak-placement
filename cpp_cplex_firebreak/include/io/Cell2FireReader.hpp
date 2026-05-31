#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Graph.hpp"
#include "core/Instance.hpp"

namespace firebreak::io {

class Cell2FireReader {
public:
    core::Instance load_instance(
        const std::string& landscape_name,
        const std::filesystem::path& forest_path,
        const std::filesystem::path& results_path,
        const std::vector<int>& scenario_ids,
        std::vector<std::string>& warnings) const;

private:
    struct ForestInfo {
        int rows = 0;
        int cols = 0;
        int n_cells = 0;
        bool has_size = false;
        bool available_known = false;
        std::vector<int> available_nodes;
    };

    ForestInfo read_forest_info(const std::filesystem::path& forest_path, std::vector<std::string>& warnings) const;
    std::unordered_map<int, int> read_ignitions(const std::filesystem::path& results_path, std::vector<std::string>& warnings) const;
    core::Graph read_message_graph(const std::filesystem::path& message_path, std::vector<std::string>& warnings) const;
};

}  // namespace firebreak::io

