#include "io/ScenarioSplitUtils.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "io/PathUtils.hpp"

namespace firebreak::io {

namespace {

std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

int parse_positive_int(const std::string& token) {
    const std::string cleaned = trim(token);
    if (cleaned.empty()) {
        throw std::runtime_error("Empty scenario ID token.");
    }
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(cleaned, &consumed);
        if (consumed != cleaned.size() || value <= 0) {
            throw std::runtime_error("Invalid scenario ID token: " + cleaned);
        }
        return value;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid scenario ID token: " + cleaned);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Scenario ID token is out of range: " + cleaned);
    }
}

std::filesystem::path split_path(
    const std::filesystem::path& output_dir,
    const std::string& landscape,
    unsigned int seed,
    std::size_t train_count,
    std::size_t test_count,
    const std::string& suffix) {
    std::ostringstream name;
    name << landscape << "_seed" << seed
         << "_train" << train_count
         << "_test" << test_count
         << "_" << suffix << ".csv";
    return output_dir / name.str();
}

}  // namespace

ScenarioSplit generate_train_test_split(
    const std::vector<int>& available_ids,
    unsigned int seed,
    std::size_t train_count,
    std::size_t test_count) {
    std::vector<int> unique_ids = available_ids;
    std::sort(unique_ids.begin(), unique_ids.end());
    unique_ids.erase(std::unique(unique_ids.begin(), unique_ids.end()), unique_ids.end());

    if (unique_ids.size() != available_ids.size()) {
        throw std::runtime_error("Available scenario ID list contains duplicates.");
    }
    if (train_count + test_count > unique_ids.size()) {
        throw std::runtime_error("Requested train/test split is larger than the available scenario ID set.");
    }

    std::mt19937 generator(seed);
    std::shuffle(unique_ids.begin(), unique_ids.end(), generator);

    ScenarioSplit split;
    split.train_ids.assign(unique_ids.begin(), unique_ids.begin() + static_cast<std::ptrdiff_t>(train_count));
    split.test_ids.assign(
        unique_ids.begin() + static_cast<std::ptrdiff_t>(train_count),
        unique_ids.begin() + static_cast<std::ptrdiff_t>(train_count + test_count));

    std::sort(split.train_ids.begin(), split.train_ids.end());
    std::sort(split.test_ids.begin(), split.test_ids.end());

    std::unordered_set<int> train_set(split.train_ids.begin(), split.train_ids.end());
    for (const int id : split.test_ids) {
        if (train_set.find(id) != train_set.end()) {
            throw std::runtime_error("Generated train/test split is not disjoint.");
        }
    }

    return split;
}

void save_scenario_ids(const std::filesystem::path& output_path, const std::vector<int>& ids) {
    ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open scenario ID output file: " + output_path.string());
    }
    for (const int id : ids) {
        out << id << "\n";
    }
}

std::vector<int> load_scenario_ids(const std::filesystem::path& input_path) {
    std::ifstream in(input_path);
    if (!in) {
        throw std::runtime_error("Could not open scenario ID file: " + input_path.string());
    }

    std::vector<int> ids;
    std::string line;
    while (std::getline(in, line)) {
        std::stringstream stream(line);
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (trim(token).empty()) {
                continue;
            }
            ids.push_back(parse_positive_int(token));
        }
    }
    return ids;
}

void save_train_test_split(
    const std::filesystem::path& output_dir,
    const std::string& landscape,
    unsigned int seed,
    std::size_t train_count,
    std::size_t test_count,
    const ScenarioSplit& split) {
    save_scenario_ids(split_path(output_dir, landscape, seed, train_count, test_count, "train"), split.train_ids);
    save_scenario_ids(split_path(output_dir, landscape, seed, train_count, test_count, "test"), split.test_ids);
}

}  // namespace firebreak::io

