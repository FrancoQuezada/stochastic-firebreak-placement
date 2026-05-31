#include "io/Cell2FireReader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "io/ScenarioFileUtils.hpp"

namespace firebreak::io {

namespace fs = std::filesystem;

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

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> split_csv_simple(const std::string& line) {
    std::vector<std::string> cells;
    std::stringstream stream(line);
    std::string token;
    while (std::getline(stream, token, ',')) {
        cells.push_back(trim(token));
    }
    return cells;
}

bool parse_int_strict(const std::string& value, int& out) {
    try {
        std::size_t consumed = 0;
        out = std::stoi(trim(value), &consumed);
        return consumed == trim(value).size();
    } catch (...) {
        return false;
    }
}

bool parse_double_strict(const std::string& value, double& out) {
    try {
        std::size_t consumed = 0;
        const std::string cleaned = trim(value);
        out = std::stod(cleaned, &consumed);
        return consumed == cleaned.size();
    } catch (...) {
        return false;
    }
}

bool looks_non_fuel(const std::string& fuel_type) {
    const std::string value = lower_copy(fuel_type);
    return value.find("non") != std::string::npos ||
           value.find("water") != std::string::npos ||
           value == "nf" ||
           value == "nodata";
}

std::unordered_set<std::string> read_non_fuel_values(const fs::path& lookup_file) {
    std::unordered_set<std::string> non_fuel;
    std::ifstream input(lookup_file);
    if (!input) {
        return non_fuel;
    }

    std::string line;
    bool first = true;
    while (std::getline(input, line)) {
        if (first) {
            first = false;
            continue;
        }
        const auto cells = split_csv_simple(line);
        if (cells.size() < 4) {
            continue;
        }
        if (looks_non_fuel(cells[3])) {
            non_fuel.insert(cells[0]);
        }
    }
    return non_fuel;
}

}  // namespace

Cell2FireReader::ForestInfo Cell2FireReader::read_forest_info(
    const fs::path& forest_path,
    std::vector<std::string>& warnings) const {
    const fs::path fuels_file = forest_path / "fuels.asc";
    std::ifstream input(fuels_file);
    if (!input) {
        warnings.push_back("Could not open fuels.asc; forest size is unknown.");
        return {};
    }

    ForestInfo info;
    std::string line;
    std::vector<std::string> grid_tokens;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::stringstream stream(line);
        std::string key;
        stream >> key;
        const std::string lower_key = lower_copy(key);
        if (lower_key == "ncols") {
            stream >> info.cols;
            continue;
        }
        if (lower_key == "nrows") {
            stream >> info.rows;
            continue;
        }
        if (line_number <= 6) {
            continue;
        }
        std::stringstream grid_stream(line);
        std::string token;
        while (grid_stream >> token) {
            grid_tokens.push_back(token);
        }
    }

    if (info.rows > 0 && info.cols > 0) {
        info.n_cells = info.rows * info.cols;
        info.has_size = true;
    } else {
        warnings.push_back("Could not parse nrows/ncols from fuels.asc.");
    }

    const auto non_fuel = read_non_fuel_values(forest_path / "fbp_lookup_table.csv");
    if (!non_fuel.empty() && info.has_size && static_cast<int>(grid_tokens.size()) >= info.n_cells) {
        for (int index = 0; index < info.n_cells; ++index) {
            const std::string token = trim(grid_tokens[static_cast<std::size_t>(index)]);
            if (non_fuel.find(token) == non_fuel.end()) {
                info.available_nodes.push_back(index + 1);
            }
        }
        info.available_known = true;
    } else if (non_fuel.empty()) {
        warnings.push_back("Could not infer non-fuel values from fbp_lookup_table.csv; available nodes unknown.");
    } else {
        warnings.push_back("Forest grid token count was smaller than NCells; available nodes unknown.");
    }

    return info;
}

std::unordered_map<int, int> Cell2FireReader::read_ignitions(
    const fs::path& results_path,
    std::vector<std::string>& warnings) const {
    const fs::path ignition_file = results_path / "IgnitionsHistory" / "ignitions_log.csv";
    std::ifstream input(ignition_file);
    if (!input) {
        throw std::runtime_error("Could not open ignition history: " + ignition_file.string());
    }

    std::unordered_map<int, int> ignition_by_scenario;
    std::string line;
    int row_number = 0;
    bool one_column_seen = false;
    bool two_column_seen = false;

    while (std::getline(input, line)) {
        if (trim(line).empty()) {
            continue;
        }
        ++row_number;
        const auto cells = split_csv_simple(line);
        std::vector<int> numbers;
        for (const auto& cell : cells) {
            int value = 0;
            if (parse_int_strict(cell, value)) {
                numbers.push_back(value);
            }
        }
        if (numbers.empty()) {
            continue;
        }
        if (numbers.size() == 1) {
            one_column_seen = true;
            ignition_by_scenario[row_number] = numbers[0];
        } else {
            two_column_seen = true;
            ignition_by_scenario[numbers[0]] = numbers[1];
        }
    }

    if (one_column_seen && !two_column_seen) {
        warnings.push_back(
            "IgnitionsHistory/ignitions_log.csv has one numeric column; assuming line number is the scenario ID.");
    } else if (one_column_seen && two_column_seen) {
        warnings.push_back(
            "Ignition file mixes one-column and two-column rows; interpreted one-column rows by line number.");
    }

    return ignition_by_scenario;
}

core::Graph Cell2FireReader::read_message_graph(
    const fs::path& message_path,
    std::vector<std::string>& warnings) const {
    std::ifstream input(message_path);
    if (!input) {
        throw std::runtime_error("Could not open message file: " + message_path.string());
    }

    core::Graph graph;
    std::string line;
    int skipped_rows = 0;
    int parsed_rows = 0;
    while (std::getline(input, line)) {
        if (trim(line).empty()) {
            continue;
        }
        const auto cells = split_csv_simple(line);
        if (cells.size() < 2) {
            ++skipped_rows;
            continue;
        }

        int u = 0;
        int v = 0;
        if (!parse_int_strict(cells[0], u) || !parse_int_strict(cells[1], v)) {
            ++skipped_rows;
            continue;
        }

        double time = 0.0;
        double ros = 0.0;
        bool has_time = false;
        bool has_ros = false;
        if (cells.size() >= 3) {
            has_time = parse_double_strict(cells[2], time);
        }
        if (cells.size() >= 4) {
            has_ros = parse_double_strict(cells[3], ros);
        }
        graph.add_edge(u, v, time, ros, has_time, has_ros);
        ++parsed_rows;
    }

    if (parsed_rows == 0) {
        warnings.push_back("No arcs parsed from " + message_path.filename().string() + ".");
    }
    if (skipped_rows > 0) {
        warnings.push_back(
            "Skipped " + std::to_string(skipped_rows) + " non-arc rows in " + message_path.filename().string() + ".");
    }

    return graph;
}

core::Instance Cell2FireReader::load_instance(
    const std::string& landscape_name,
    const fs::path& forest_path,
    const fs::path& results_path,
    const std::vector<int>& scenario_ids,
    std::vector<std::string>& warnings) const {
    const auto inventory = detect_message_files(results_path);
    validate_scenario_ids(inventory, scenario_ids);
    const auto ignitions = read_ignitions(results_path, warnings);
    const auto forest_info = read_forest_info(forest_path, warnings);

    core::Instance instance;
    instance.landscape_name = landscape_name;
    instance.forest_path = forest_path;
    instance.results_path = results_path;
    instance.rows = forest_info.rows;
    instance.cols = forest_info.cols;
    instance.n_cells = forest_info.n_cells;
    instance.has_forest_size = forest_info.has_size;
    instance.available_nodes_known = forest_info.available_known;
    instance.available_nodes = forest_info.available_nodes;

    for (const int scenario_id : scenario_ids) {
        const auto ignition_it = ignitions.find(scenario_id);
        if (ignition_it == ignitions.end()) {
            throw std::runtime_error("No ignition node found for scenario ID " + std::to_string(scenario_id) + ".");
        }

        const auto& scenario_file = inventory.file_for(scenario_id);
        core::Scenario scenario;
        scenario.scenario_id = scenario_id;
        scenario.message_filename = scenario_file.filename;
        scenario.ignition_node = ignition_it->second;
        scenario.propagation_graph = read_message_graph(scenario_file.path, warnings);
        instance.scenarios.push_back(std::move(scenario));
    }

    return instance;
}

}  // namespace firebreak::io

