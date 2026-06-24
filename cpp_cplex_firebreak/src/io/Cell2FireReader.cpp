#include "io/Cell2FireReader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "io/ScenarioFileUtils.hpp"

namespace firebreak::io {

namespace fs = std::filesystem;

namespace {

struct ExpectedFolderSize {
    bool known = false;
    int rows = 0;
    int cols = 0;
    int n_cells = 0;
};

struct RunLogMetadata {
    bool has_n_cells = false;
    int n_cells = 0;
    bool has_dimensions = false;
    int rows = 0;
    int cols = 0;
    std::string instance_tif;
};

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

std::string expected_new_message_filename(int scenario_id) {
    std::ostringstream out;
    out << "MessagesFile";
    out.width(5);
    out.fill('0');
    out << scenario_id << ".csv";
    return out.str();
}

ExpectedFolderSize expected_size_from_folder_name(const fs::path& path) {
    std::string name = path.filename().string();
    const std::string suffix = "_reburn";
    if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
        name = name.substr(0, name.size() - suffix.size());
    }

    const std::regex pattern(R"(^([0-9]+)x([0-9]+)$)");
    std::smatch match;
    ExpectedFolderSize expected;
    if (!std::regex_match(name, match, pattern)) {
        return expected;
    }

    expected.rows = std::stoi(match[1].str());
    expected.cols = std::stoi(match[2].str());
    expected.n_cells = expected.rows * expected.cols;
    expected.known = true;
    return expected;
}

RunLogMetadata read_run_log_metadata(const fs::path& run_log_path) {
    RunLogMetadata metadata;
    std::ifstream input(run_log_path);
    if (!input) {
        return metadata;
    }

    const std::regex cached_cells_pattern(R"(Instance data cached \(([0-9]+) cells\))");
    const std::regex number_cells_pattern(R"(Number of cells:\s*([0-9]+))");
    const std::regex tif_dimensions_pattern(R"(([0-9]+)x([0-9]+)\.tif)");

    std::string line;
    while (std::getline(input, line)) {
        std::smatch match;
        if (!metadata.has_n_cells && std::regex_search(line, match, cached_cells_pattern)) {
            metadata.n_cells = std::stoi(match[1].str());
            metadata.has_n_cells = true;
        }
        if (!metadata.has_n_cells && std::regex_search(line, match, number_cells_pattern)) {
            metadata.n_cells = std::stoi(match[1].str());
            metadata.has_n_cells = true;
        }
        if (line.find("InstanceTif:") != std::string::npos ||
            line.find("Reading instance TIF:") != std::string::npos) {
            metadata.instance_tif = trim(line);
            if (std::regex_search(line, match, tif_dimensions_pattern)) {
                metadata.rows = std::stoi(match[1].str());
                metadata.cols = std::stoi(match[2].str());
                metadata.has_dimensions = true;
            }
        }
    }

    return metadata;
}

void fill_all_nodes_available(Cell2FireReader::ForestInfo& info) {
    info.available_nodes.clear();
    if (!info.has_size || info.n_cells <= 0) {
        return;
    }
    info.available_nodes.reserve(static_cast<std::size_t>(info.n_cells));
    for (int node = 1; node <= info.n_cells; ++node) {
        info.available_nodes.push_back(node);
    }
    info.available_known = true;
}

void warn_if_size_mismatch(
    const ExpectedFolderSize& expected,
    const RunLogMetadata& run_log,
    const Cell2FireReader::ForestInfo& info,
    std::vector<std::string>& warnings) {
    if (run_log.has_dimensions && run_log.has_n_cells &&
        run_log.rows * run_log.cols != run_log.n_cells) {
        warnings.push_back(
            "Metadata inconsistency: run.log dimensions " +
            std::to_string(run_log.rows) + "x" + std::to_string(run_log.cols) +
            " imply " + std::to_string(run_log.rows * run_log.cols) +
            " cells, but run.log reports " + std::to_string(run_log.n_cells) + " cells.");
    }
    if (!expected.known || !info.has_size) {
        return;
    }
    if (expected.n_cells != info.n_cells) {
        warnings.push_back(
            "Metadata inconsistency: folder name suggests " +
            std::to_string(expected.rows) + "x" + std::to_string(expected.cols) +
            " (" + std::to_string(expected.n_cells) +
            " cells), but inferred forest size is " + std::to_string(info.n_cells) +
            " cells.");
    }
    if (run_log.has_dimensions &&
        (expected.rows != run_log.rows || expected.cols != run_log.cols)) {
        warnings.push_back(
            "Metadata inconsistency: folder name suggests " +
            std::to_string(expected.rows) + "x" + std::to_string(expected.cols) +
            ", but run.log references instance_" + std::to_string(run_log.rows) +
            "x" + std::to_string(run_log.cols) + ".tif.");
    }
}

}  // namespace

std::string layout_name(Cell2FireLayout layout) {
    switch (layout) {
        case Cell2FireLayout::Legacy:
            return "legacy";
        case Cell2FireLayout::NewInstances:
            return "new_instances";
    }
    return "unknown";
}

Cell2FireReader::ForestInfo Cell2FireReader::read_forest_info(
    const fs::path& forest_path,
    const fs::path& results_path,
    Cell2FireLayout layout,
    std::vector<std::string>& warnings) const {
    const fs::path fuels_file = forest_path / "fuels.asc";
    std::ifstream input(fuels_file);
    if (!input) {
        warnings.push_back("Could not open fuels.asc; attempting fallback forest-size inference.");
    } else {
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

        const auto expected = expected_size_from_folder_name(results_path);
        const auto run_log = read_run_log_metadata(results_path / "run.log");
        warn_if_size_mismatch(expected, run_log, info, warnings);
        return info;
    }

    ForestInfo info;
    const auto expected = expected_size_from_folder_name(results_path);
    const auto run_log = read_run_log_metadata(results_path / "run.log");
    if (run_log.has_dimensions) {
        info.rows = run_log.rows;
        info.cols = run_log.cols;
        info.n_cells = run_log.rows * run_log.cols;
        info.has_size = true;
        warnings.push_back("Forest size inferred from run.log instance TIF metadata.");
    } else if (run_log.has_n_cells) {
        info.n_cells = run_log.n_cells;
        info.has_size = true;
        warnings.push_back("Forest size inferred from run.log cell count.");
    } else if (expected.known) {
        info.rows = expected.rows;
        info.cols = expected.cols;
        info.n_cells = expected.n_cells;
        info.has_size = true;
        warnings.push_back(
            "Forest size inferred from folder name because fuels.asc and run.log size metadata were unavailable.");
    }

    if (layout == Cell2FireLayout::NewInstances && info.has_size && !info.available_known) {
        fill_all_nodes_available(info);
        warnings.push_back("new_instances layout assumes cell IDs 1..NCells are eligible firebreak nodes.");
    }

    warn_if_size_mismatch(expected, run_log, info, warnings);
    return info;
}

Cell2FireLayout Cell2FireReader::detect_layout(const fs::path& results_path) const {
    const fs::path legacy_file = results_path / "IgnitionsHistory" / "ignitions_log.csv";
    const fs::path new_file = results_path / "ignition_and_weather_log.csv";
    if (fs::is_regular_file(legacy_file)) {
        return Cell2FireLayout::Legacy;
    }
    if (fs::is_regular_file(new_file)) {
        return Cell2FireLayout::NewInstances;
    }
    throw std::runtime_error(
        "No ignition metadata found. Expected either " + legacy_file.string() +
        " or " + new_file.string() + ".");
}

IgnitionMetadata Cell2FireReader::read_ignition_metadata(
    const fs::path& results_path,
    std::vector<std::string>& warnings) const {
    const fs::path ignition_file = results_path / "IgnitionsHistory" / "ignitions_log.csv";
    if (fs::is_regular_file(ignition_file)) {
        return read_legacy_ignitions(ignition_file, warnings);
    }
    const fs::path new_ignition_file = results_path / "ignition_and_weather_log.csv";
    if (fs::is_regular_file(new_ignition_file)) {
        return read_new_instance_ignitions(new_ignition_file, warnings);
    }
    throw std::runtime_error(
        "No ignition metadata found. Expected either " + ignition_file.string() +
        " or " + new_ignition_file.string() + ".");
}

IgnitionMetadata Cell2FireReader::read_legacy_ignitions(
    const fs::path& ignition_file,
    std::vector<std::string>& warnings) const {
    std::ifstream input(ignition_file);
    if (!input) {
        throw std::runtime_error("Could not open ignition history: " + ignition_file.string());
    }

    IgnitionMetadata metadata;
    metadata.layout = Cell2FireLayout::Legacy;
    metadata.source_path = ignition_file;
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
            metadata.records[row_number] = IgnitionRecord{row_number, numbers[0], ""};
        } else {
            two_column_seen = true;
            metadata.records[numbers[0]] = IgnitionRecord{numbers[0], numbers[1], ""};
        }
    }

    if (one_column_seen && !two_column_seen) {
        warnings.push_back(
            "IgnitionsHistory/ignitions_log.csv has one numeric column; assuming line number is the scenario ID.");
    } else if (one_column_seen && two_column_seen) {
        warnings.push_back(
            "Ignition file mixes one-column and two-column rows; interpreted one-column rows by line number.");
    }

    return metadata;
}

IgnitionMetadata Cell2FireReader::read_new_instance_ignitions(
    const fs::path& ignition_file,
    std::vector<std::string>& warnings) const {
    std::ifstream input(ignition_file);
    if (!input) {
        throw std::runtime_error("Could not open ignition metadata: " + ignition_file.string());
    }

    IgnitionMetadata metadata;
    metadata.layout = Cell2FireLayout::NewInstances;
    metadata.source_path = ignition_file;

    std::string line;
    int simulation_col = -1;
    int ignition_col = -1;
    int weather_col = -1;
    int line_number = 0;
    bool header_seen = false;

    while (std::getline(input, line)) {
        ++line_number;
        if (trim(line).empty()) {
            continue;
        }
        const auto cells = split_csv_simple(line);
        if (!header_seen) {
            for (std::size_t i = 0; i < cells.size(); ++i) {
                const std::string header = lower_copy(cells[i]);
                if (header == "simulation") {
                    simulation_col = static_cast<int>(i);
                } else if (header == "ignition") {
                    ignition_col = static_cast<int>(i);
                } else if (header == "weather") {
                    weather_col = static_cast<int>(i);
                }
            }
            if (simulation_col < 0 || ignition_col < 0) {
                throw std::runtime_error(
                    "ignition_and_weather_log.csv must contain simulation and ignition columns: " +
                    ignition_file.string());
            }
            header_seen = true;
            continue;
        }

        const int required_col = std::max(simulation_col, ignition_col);
        if (static_cast<int>(cells.size()) <= required_col) {
            throw std::runtime_error(
                "Malformed ignition_and_weather_log.csv row " + std::to_string(line_number) +
                ": missing simulation or ignition value.");
        }
        int scenario_id = 0;
        int ignition_node = 0;
        if (!parse_int_strict(cells[static_cast<std::size_t>(simulation_col)], scenario_id) ||
            !parse_int_strict(cells[static_cast<std::size_t>(ignition_col)], ignition_node)) {
            throw std::runtime_error(
                "Malformed ignition_and_weather_log.csv row " + std::to_string(line_number) +
                ": simulation and ignition must be integers.");
        }
        if (scenario_id <= 0 || ignition_node <= 0) {
            throw std::runtime_error(
                "Malformed ignition_and_weather_log.csv row " + std::to_string(line_number) +
                ": simulation and ignition must be positive.");
        }
        std::string weather;
        if (weather_col >= 0 && static_cast<int>(cells.size()) > weather_col) {
            weather = cells[static_cast<std::size_t>(weather_col)];
        }
        const auto inserted = metadata.records.emplace(
            scenario_id,
            IgnitionRecord{scenario_id, ignition_node, weather});
        if (!inserted.second) {
            throw std::runtime_error(
                "Duplicate simulation ID in ignition_and_weather_log.csv: " +
                std::to_string(scenario_id));
        }
    }

    if (!header_seen) {
        throw std::runtime_error("ignition_and_weather_log.csv is empty: " + ignition_file.string());
    }
    if (metadata.records.empty()) {
        warnings.push_back("ignition_and_weather_log.csv contained no ignition records.");
    }

    return metadata;
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
    const auto ignition_metadata = read_ignition_metadata(results_path, warnings);
    const auto forest_info = read_forest_info(forest_path, results_path, ignition_metadata.layout, warnings);

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
        const auto ignition_it = ignition_metadata.records.find(scenario_id);
        if (ignition_it == ignition_metadata.records.end()) {
            throw std::runtime_error("No ignition node found for scenario ID " + std::to_string(scenario_id) + ".");
        }

        const auto& scenario_file = inventory.file_for(scenario_id);
        if (ignition_metadata.layout == Cell2FireLayout::NewInstances) {
            const std::string expected_filename = expected_new_message_filename(scenario_id);
            if (scenario_file.filename != expected_filename) {
                throw std::runtime_error(
                    "New-instances message filename mismatch for simulation " +
                    std::to_string(scenario_id) + ": expected " + expected_filename +
                    ", found " + scenario_file.filename + ".");
            }
        }

        core::Scenario scenario;
        scenario.scenario_id = scenario_id;
        scenario.message_filename = scenario_file.filename;
        scenario.ignition_node = ignition_it->second.ignition_node;
        scenario.weather_metadata = ignition_it->second.weather;
        scenario.propagation_graph = read_message_graph(scenario_file.path, warnings);
        instance.scenarios.push_back(std::move(scenario));
    }

    if (instance.has_forest_size) {
        int max_observed_node = 0;
        int max_ignition_node = 0;
        for (const auto& scenario : instance.scenarios) {
            max_observed_node = std::max(max_observed_node, scenario.graph().max_node_id_observed());
            max_ignition_node = std::max(max_ignition_node, scenario.ignition_node);
        }
        const int max_node = std::max(max_observed_node, max_ignition_node);
        if (max_node > instance.n_cells) {
            warnings.push_back(
                "Metadata inconsistency: loaded scenarios contain node ID " +
                std::to_string(max_node) + ", exceeding inferred NCells " +
                std::to_string(instance.n_cells) + ".");
        }
    }

    return instance;
}

}  // namespace firebreak::io
