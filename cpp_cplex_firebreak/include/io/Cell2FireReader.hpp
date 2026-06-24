#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Graph.hpp"
#include "core/Instance.hpp"

namespace firebreak::io {

enum class Cell2FireLayout {
    Legacy,
    NewInstances,
};

std::string layout_name(Cell2FireLayout layout);

struct IgnitionRecord {
    int scenario_id = 0;
    int ignition_node = 0;
    std::string weather;
};

struct IgnitionMetadata {
    Cell2FireLayout layout = Cell2FireLayout::Legacy;
    std::filesystem::path source_path;
    std::unordered_map<int, IgnitionRecord> records;
};

class Cell2FireReader {
public:
    core::Instance load_instance(
        const std::string& landscape_name,
        const std::filesystem::path& forest_path,
        const std::filesystem::path& results_path,
        const std::vector<int>& scenario_ids,
        std::vector<std::string>& warnings) const;

    struct ForestInfo {
        int rows = 0;
        int cols = 0;
        int n_cells = 0;
        bool has_size = false;
        bool available_known = false;
        std::vector<int> available_nodes;
    };

    Cell2FireLayout detect_layout(const std::filesystem::path& results_path) const;
    IgnitionMetadata read_ignition_metadata(
        const std::filesystem::path& results_path,
        std::vector<std::string>& warnings) const;
    ForestInfo read_forest_info(
        const std::filesystem::path& forest_path,
        const std::filesystem::path& results_path,
        Cell2FireLayout layout,
        std::vector<std::string>& warnings) const;
    core::Graph read_message_graph(const std::filesystem::path& message_path, std::vector<std::string>& warnings) const;

private:
    IgnitionMetadata read_legacy_ignitions(
        const std::filesystem::path& ignition_file,
        std::vector<std::string>& warnings) const;
    IgnitionMetadata read_new_instance_ignitions(
        const std::filesystem::path& ignition_file,
        std::vector<std::string>& warnings) const;
};

}  // namespace firebreak::io
