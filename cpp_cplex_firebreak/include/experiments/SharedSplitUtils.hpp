#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "io/ScenarioSplitUtils.hpp"

namespace firebreak::experiments {

struct SharedSplitPaths {
    std::filesystem::path train_path;
    std::filesystem::path test_path;
};

struct SharedSplitResult {
    io::ScenarioSplit split;
    SharedSplitPaths paths;
    unsigned int seed = 0;
    bool reused_existing = false;
    bool generated = false;
};

unsigned int shared_split_seed(unsigned int seed_base, std::size_t case_id);

SharedSplitPaths shared_split_paths(
    const std::filesystem::path& split_dir,
    const std::string& landscape,
    unsigned int seed,
    std::size_t train_count,
    std::size_t test_count,
    std::size_t case_id);

SharedSplitResult load_or_create_shared_split(
    const std::filesystem::path& split_dir,
    const std::string& landscape,
    const std::vector<int>& available_ids,
    unsigned int seed_base,
    std::size_t train_count,
    std::size_t test_count,
    std::size_t case_id);

}  // namespace firebreak::experiments
