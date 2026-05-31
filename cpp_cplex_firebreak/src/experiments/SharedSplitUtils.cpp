#include "experiments/SharedSplitUtils.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace firebreak::experiments {

namespace {

void validate_loaded_split(
    const io::ScenarioSplit& split,
    const std::vector<int>& available_ids,
    std::size_t train_count,
    std::size_t test_count) {
    if (split.train_ids.size() != train_count) {
        throw std::runtime_error("Shared split train file has an unexpected number of scenario IDs.");
    }
    if (split.test_ids.size() != test_count) {
        throw std::runtime_error("Shared split test file has an unexpected number of scenario IDs.");
    }

    std::unordered_set<int> available_set(available_ids.begin(), available_ids.end());
    std::unordered_set<int> seen_train;
    for (const int id : split.train_ids) {
        if (available_set.find(id) == available_set.end()) {
            throw std::runtime_error("Shared split train file contains a scenario ID not available for this landscape.");
        }
        if (!seen_train.insert(id).second) {
            throw std::runtime_error("Shared split train file contains duplicate scenario IDs.");
        }
    }

    std::unordered_set<int> seen_test;
    for (const int id : split.test_ids) {
        if (available_set.find(id) == available_set.end()) {
            throw std::runtime_error("Shared split test file contains a scenario ID not available for this landscape.");
        }
        if (!seen_test.insert(id).second) {
            throw std::runtime_error("Shared split test file contains duplicate scenario IDs.");
        }
        if (seen_train.find(id) != seen_train.end()) {
            throw std::runtime_error("Shared split train and test files are not disjoint.");
        }
    }
}

}  // namespace

unsigned int shared_split_seed(unsigned int seed_base, std::size_t case_id) {
    return seed_base + static_cast<unsigned int>(case_id);
}

SharedSplitPaths shared_split_paths(
    const std::filesystem::path& split_dir,
    const std::string& landscape,
    unsigned int seed,
    std::size_t train_count,
    std::size_t test_count,
    std::size_t case_id) {
    std::ostringstream prefix;
    prefix << landscape
           << "_seed" << seed
           << "_train" << train_count
           << "_test" << test_count
           << "_case" << case_id;

    SharedSplitPaths paths;
    paths.train_path = split_dir / (prefix.str() + "_train.csv");
    paths.test_path = split_dir / (prefix.str() + "_test.csv");
    return paths;
}

SharedSplitResult load_or_create_shared_split(
    const std::filesystem::path& split_dir,
    const std::string& landscape,
    const std::vector<int>& available_ids,
    unsigned int seed_base,
    std::size_t train_count,
    std::size_t test_count,
    std::size_t case_id) {
    const unsigned int seed = shared_split_seed(seed_base, case_id);
    const auto paths = shared_split_paths(split_dir, landscape, seed, train_count, test_count, case_id);
    const bool train_exists = std::filesystem::exists(paths.train_path);
    const bool test_exists = std::filesystem::exists(paths.test_path);

    SharedSplitResult result;
    result.paths = paths;
    result.seed = seed;

    if (train_exists && test_exists) {
        result.split.train_ids = io::load_scenario_ids(paths.train_path);
        result.split.test_ids = io::load_scenario_ids(paths.test_path);
        validate_loaded_split(result.split, available_ids, train_count, test_count);
        result.reused_existing = true;
        return result;
    }

    result.split = io::generate_train_test_split(available_ids, seed, train_count, test_count);
    if (train_exists != test_exists) {
        if (train_exists) {
            const auto existing_train = io::load_scenario_ids(paths.train_path);
            if (existing_train != result.split.train_ids) {
                throw std::runtime_error("Existing shared split train file does not match the deterministic split.");
            }
            io::save_scenario_ids(paths.test_path, result.split.test_ids);
        } else {
            const auto existing_test = io::load_scenario_ids(paths.test_path);
            if (existing_test != result.split.test_ids) {
                throw std::runtime_error("Existing shared split test file does not match the deterministic split.");
            }
            io::save_scenario_ids(paths.train_path, result.split.train_ids);
        }
        result.generated = true;
        return result;
    }

    io::save_scenario_ids(paths.train_path, result.split.train_ids);
    io::save_scenario_ids(paths.test_path, result.split.test_ids);
    result.generated = true;
    return result;
}

}  // namespace firebreak::experiments
