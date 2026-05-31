#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::io {

struct ScenarioFile {
    int scenario_id = 0;
    std::filesystem::path path;
    std::string filename;
};

struct ScenarioInventory {
    std::filesystem::path messages_dir;
    std::vector<ScenarioFile> files;

    int count() const;
    int min_id() const;
    int max_id() const;
    std::vector<int> ids() const;
    bool contains(int scenario_id) const;
    const ScenarioFile& file_for(int scenario_id) const;
};

ScenarioInventory detect_message_files(const std::filesystem::path& results_path);
std::vector<int> parse_scenario_id_list(const std::string& value);
void validate_scenario_ids(const ScenarioInventory& inventory, const std::vector<int>& requested_ids);

}  // namespace firebreak::io

