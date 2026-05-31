#include "io/ScenarioFileUtils.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

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

int parse_positive_scenario_id(const std::string& token) {
    const std::string cleaned = trim(token);
    if (cleaned.empty()) {
        throw std::runtime_error("Invalid empty scenario ID token.");
    }
    std::size_t consumed = 0;
    const int id = std::stoi(cleaned, &consumed);
    if (consumed != cleaned.size() || id <= 0) {
        throw std::runtime_error("Invalid scenario ID token: " + cleaned);
    }
    return id;
}

void append_range(std::vector<int>& ids, int start, int end, const std::string& token) {
    if (start > end) {
        throw std::runtime_error("Invalid descending scenario ID range: " + token);
    }
    for (int id = start; id <= end; ++id) {
        ids.push_back(id);
    }
}

}  // namespace

int ScenarioInventory::count() const {
    return static_cast<int>(files.size());
}

int ScenarioInventory::min_id() const {
    if (files.empty()) {
        return 0;
    }
    return files.front().scenario_id;
}

int ScenarioInventory::max_id() const {
    if (files.empty()) {
        return 0;
    }
    return files.back().scenario_id;
}

std::vector<int> ScenarioInventory::ids() const {
    std::vector<int> out;
    out.reserve(files.size());
    for (const auto& file : files) {
        out.push_back(file.scenario_id);
    }
    return out;
}

bool ScenarioInventory::contains(int scenario_id) const {
    return std::any_of(files.begin(), files.end(), [scenario_id](const ScenarioFile& file) {
        return file.scenario_id == scenario_id;
    });
}

const ScenarioFile& ScenarioInventory::file_for(int scenario_id) const {
    const auto it = std::find_if(files.begin(), files.end(), [scenario_id](const ScenarioFile& file) {
        return file.scenario_id == scenario_id;
    });
    if (it == files.end()) {
        throw std::runtime_error("Requested scenario ID was not found in the message inventory.");
    }
    return *it;
}

ScenarioInventory detect_message_files(const fs::path& results_path) {
    const fs::path messages_dir = results_path / "Messages";
    if (!fs::is_directory(messages_dir)) {
        throw std::runtime_error("Missing Messages directory: " + messages_dir.string());
    }

    const std::regex pattern(R"(^MessagesFile([0-9]+)\.csv$)");
    ScenarioInventory inventory;
    inventory.messages_dir = messages_dir;

    for (const auto& entry : fs::directory_iterator(messages_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        std::smatch match;
        if (!std::regex_match(filename, match, pattern)) {
            continue;
        }
        inventory.files.push_back(ScenarioFile{
            std::stoi(match[1].str()),
            entry.path(),
            filename,
        });
    }

    std::sort(inventory.files.begin(), inventory.files.end(), [](const ScenarioFile& a, const ScenarioFile& b) {
        return a.scenario_id < b.scenario_id;
    });

    if (inventory.files.empty()) {
        throw std::runtime_error("No MessagesFile*.csv files found in " + messages_dir.string());
    }
    return inventory;
}

std::vector<int> parse_scenario_id_list(const std::string& value) {
    std::vector<int> ids;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        if (token.empty()) {
            continue;
        }

        const auto dash_pos = token.find('-');
        const auto colon_pos = token.find(':');
        const bool has_dash = dash_pos != std::string::npos;
        const bool has_colon = colon_pos != std::string::npos;
        if (has_dash && has_colon) {
            throw std::runtime_error("Scenario ID token cannot mix range separators: " + token);
        }

        if (has_dash || has_colon) {
            const char delimiter = has_dash ? '-' : ':';
            const auto delimiter_pos = has_dash ? dash_pos : colon_pos;
            if (token.find(delimiter, delimiter_pos + 1) != std::string::npos) {
                throw std::runtime_error("Invalid scenario ID range token: " + token);
            }
            const int start = parse_positive_scenario_id(token.substr(0, delimiter_pos));
            const int end = parse_positive_scenario_id(token.substr(delimiter_pos + 1));
            append_range(ids, start, end, token);
        } else {
            ids.push_back(parse_positive_scenario_id(token));
        }
    }
    if (ids.empty()) {
        throw std::runtime_error("No scenario IDs were provided.");
    }
    return ids;
}

void validate_scenario_ids(const ScenarioInventory& inventory, const std::vector<int>& requested_ids) {
    std::unordered_set<int> seen;
    for (const int id : requested_ids) {
        if (id <= 0) {
            throw std::runtime_error("Scenario IDs must be positive.");
        }
        if (!inventory.contains(id)) {
            std::ostringstream message;
            message << "Scenario ID " << id << " is not available. Detected "
                    << inventory.count() << " message files with min ID "
                    << inventory.min_id() << " and max ID " << inventory.max_id() << ".";
            throw std::runtime_error(message.str());
        }
        if (!seen.insert(id).second) {
            throw std::runtime_error("Duplicate scenario ID requested: " + std::to_string(id));
        }
    }
}

}  // namespace firebreak::io
