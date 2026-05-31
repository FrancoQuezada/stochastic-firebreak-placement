#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "experiments/SharedSplitUtils.hpp"

namespace {

std::filesystem::path temp_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "firebreak_shared_split_tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void test_same_inputs_give_same_filename_without_alpha() {
    const auto dir = temp_dir();
    const unsigned int seed = firebreak::experiments::shared_split_seed(9000, 0);
    const auto paths_alpha001 = firebreak::experiments::shared_split_paths(dir, "Sub20", seed, 5, 100, 0);
    const auto paths_alpha002 = firebreak::experiments::shared_split_paths(dir, "Sub20", seed, 5, 100, 0);

    assert(paths_alpha001.train_path == paths_alpha002.train_path);
    assert(paths_alpha001.test_path == paths_alpha002.test_path);

    const std::string train_name = paths_alpha001.train_path.filename().string();
    assert(train_name == "Sub20_seed9000_train5_test100_case0_train.csv");
    assert(train_name.find("alpha") == std::string::npos);
    assert(train_name.find("0.01") == std::string::npos);
    assert(train_name.find("001") == std::string::npos);
}

void test_missing_files_are_generated() {
    const auto dir = temp_dir();
    const std::vector<int> available_ids = {1, 2, 3, 4, 5, 6, 7, 8};

    const auto result = firebreak::experiments::load_or_create_shared_split(
        dir,
        "Sub20",
        available_ids,
        123,
        2,
        3,
        0);

    assert(result.generated);
    assert(!result.reused_existing);
    assert(result.seed == 123);
    assert(result.split.train_ids.size() == 2);
    assert(result.split.test_ids.size() == 3);
    assert(std::filesystem::exists(result.paths.train_path));
    assert(std::filesystem::exists(result.paths.test_path));
}

void test_existing_files_are_reused() {
    const auto dir = temp_dir();
    const std::vector<int> available_ids = {1, 2, 3, 4, 5};
    const unsigned int seed = firebreak::experiments::shared_split_seed(123, 0);
    const auto paths = firebreak::experiments::shared_split_paths(dir, "Sub20", seed, 2, 2, 0);
    firebreak::io::save_scenario_ids(paths.train_path, {1, 2});
    firebreak::io::save_scenario_ids(paths.test_path, {3, 4});

    const auto result = firebreak::experiments::load_or_create_shared_split(
        dir,
        "Sub20",
        available_ids,
        123,
        2,
        2,
        0);

    assert(result.reused_existing);
    assert(!result.generated);
    assert((result.split.train_ids == std::vector<int>{1, 2}));
    assert((result.split.test_ids == std::vector<int>{3, 4}));
}

void test_matching_partial_shared_split_is_completed() {
    const auto dir = temp_dir();
    const std::vector<int> available_ids = {1, 2, 3, 4, 5, 6, 7, 8};
    const auto expected = firebreak::experiments::load_or_create_shared_split(
        dir,
        "Sub20",
        available_ids,
        123,
        2,
        2,
        0);

    std::filesystem::remove(expected.paths.test_path);
    const auto result = firebreak::experiments::load_or_create_shared_split(
        dir,
        "Sub20",
        available_ids,
        123,
        2,
        2,
        0);

    assert(result.generated);
    assert(!result.reused_existing);
    assert(result.split.train_ids == expected.split.train_ids);
    assert(result.split.test_ids == expected.split.test_ids);
    assert(std::filesystem::exists(result.paths.test_path));
}

void test_nonmatching_partial_shared_split_fails() {
    const auto dir = temp_dir();
    const std::vector<int> available_ids = {1, 2, 3, 4, 5, 6, 7, 8};
    const unsigned int seed = firebreak::experiments::shared_split_seed(123, 0);
    const auto paths = firebreak::experiments::shared_split_paths(dir, "Sub20", seed, 2, 2, 0);
    const auto expected = firebreak::io::generate_train_test_split(available_ids, seed, 2, 2);
    std::vector<int> wrong_train = {1, 2};
    if (wrong_train == expected.train_ids) {
        wrong_train = {3, 4};
    }
    firebreak::io::save_scenario_ids(paths.train_path, wrong_train);

    bool threw = false;
    try {
        (void)firebreak::experiments::load_or_create_shared_split(
            dir,
            "Sub20",
            available_ids,
            123,
            2,
            2,
            0);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main() {
    test_same_inputs_give_same_filename_without_alpha();
    test_missing_files_are_generated();
    test_existing_files_are_reused();
    test_matching_partial_shared_split_is_completed();
    test_nonmatching_partial_shared_split_fails();
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "firebreak_shared_split_tests");
    std::cout << "All shared split tests passed.\n";
    return 0;
}
