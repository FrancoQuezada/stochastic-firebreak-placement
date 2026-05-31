#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::io {

struct ScenarioSplit {
    std::vector<int> train_ids;
    std::vector<int> test_ids;
};

ScenarioSplit generate_train_test_split(
    const std::vector<int>& available_ids,
    unsigned int seed,
    std::size_t train_count,
    std::size_t test_count);

void save_scenario_ids(const std::filesystem::path& output_path, const std::vector<int>& ids);
std::vector<int> load_scenario_ids(const std::filesystem::path& input_path);

void save_train_test_split(
    const std::filesystem::path& output_dir,
    const std::string& landscape,
    unsigned int seed,
    std::size_t train_count,
    std::size_t test_count,
    const ScenarioSplit& split);

}  // namespace firebreak::io

