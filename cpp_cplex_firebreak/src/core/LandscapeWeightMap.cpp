#include "core/LandscapeWeightMap.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "opt/IndexMapper.hpp"

namespace firebreak::core {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::vector<int> sorted_unique_or_throw(
    std::vector<int> values,
    const std::string& context) {
    for (const int value : values) {
        if (value <= 0) {
            throw std::runtime_error(context + " contains a nonpositive original cell ID.");
        }
    }
    std::sort(values.begin(), values.end());
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] == values[i - 1]) {
            throw std::runtime_error(context + " contains duplicate original cell ID " +
                                     std::to_string(values[i]) + ".");
        }
    }
    return values;
}

std::string stable_double_string(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("Cannot serialize a nonfinite landscape weight.");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::scientific << std::setprecision(17) << value;
    return out.str();
}

void fnv_append(std::uint64_t& hash, const std::string& text) {
    for (const unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
    }
}

std::string deterministic_hash_for_final_weights(const LandscapeWeightMap& weight_map) {
    std::uint64_t hash = kFnvOffsetBasis;
    const auto ids = sorted_weighted_cell_ids(weight_map);
    for (const int cell_id : ids) {
        const auto weight_it = weight_map.weight_by_original_cell_id.find(cell_id);
        if (weight_it == weight_map.weight_by_original_cell_id.end()) {
            throw std::runtime_error("Landscape weight hash found a missing final weight.");
        }
        fnv_append(hash, std::to_string(cell_id));
        fnv_append(hash, ",");
        fnv_append(hash, stable_double_string(weight_it->second));
        fnv_append(hash, "\n");
    }

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, ',')) {
        fields.push_back(field);
    }
    if (!line.empty() && line.back() == ',') {
        fields.emplace_back();
    }
    return fields;
}

int parse_int_strict(const std::string& value, const std::string& column) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error("Malformed integer.");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer in landscape weight column '" + column +
                                 "': " + value + ".");
    }
}

double parse_double_strict(const std::string& value, const std::string& column) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error("Malformed double.");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid numeric value in landscape weight column '" + column +
                                 "': " + value + ".");
    }
}

std::size_t required_column(
    const std::vector<std::string>& header,
    const std::string& column) {
    const auto it = std::find(header.begin(), header.end(), column);
    if (it == header.end()) {
        throw std::runtime_error("Landscape weight CSV is missing required column '" +
                                 column + "'.");
    }
    return static_cast<std::size_t>(std::distance(header.begin(), it));
}

}  // namespace

LandscapeWeightMap make_landscape_weight_map(
    const std::string& profile,
    std::uint64_t seed,
    bool normalized,
    const std::vector<LandscapeWeightRecord>& records) {
    if (profile.empty()) {
        throw std::runtime_error("Landscape weight profile cannot be empty.");
    }

    LandscapeWeightMap weight_map;
    weight_map.profile = profile;
    weight_map.seed = seed;
    weight_map.normalized = normalized;

    std::unordered_set<int> seen;
    for (const auto& record : records) {
        if (record.original_cell_id <= 0) {
            throw std::runtime_error("Landscape weight record has a nonpositive original cell ID.");
        }
        if (!seen.insert(record.original_cell_id).second) {
            throw std::runtime_error(
                "Duplicate landscape weight record for original cell ID " +
                std::to_string(record.original_cell_id) + ".");
        }
        if (!std::isfinite(record.weight) || record.weight <= 0.0) {
            throw std::runtime_error(
                "Landscape final weights must be finite and strictly positive.");
        }
        if (!std::isfinite(record.raw_weight)) {
            throw std::runtime_error("Landscape raw weights must be finite.");
        }
        if (record.cluster_id < 0) {
            throw std::runtime_error("Landscape cluster IDs must be nonnegative.");
        }
        weight_map.raw_weight_by_original_cell_id[record.original_cell_id] =
            record.raw_weight;
        weight_map.weight_by_original_cell_id[record.original_cell_id] =
            record.weight;
        weight_map.cluster_id_by_original_cell_id[record.original_cell_id] =
            record.cluster_id;
    }

    recompute_landscape_weight_map_statistics_and_hash(weight_map);
    validate_landscape_weight_map(weight_map);
    return weight_map;
}

LandscapeWeightMap make_homogeneous_weight_map(
    const std::vector<int>& original_cell_ids) {
    const auto ids = sorted_unique_or_throw(original_cell_ids, "Homogeneous weight map input");
    std::vector<LandscapeWeightRecord> records;
    records.reserve(ids.size());
    for (const int cell_id : ids) {
        records.push_back(LandscapeWeightRecord{cell_id, 1.0, 1.0, 0});
    }
    return make_landscape_weight_map("homogeneous", 0, false, records);
}

void recompute_landscape_weight_map_statistics_and_hash(
    LandscapeWeightMap& weight_map) {
    if (weight_map.profile.empty()) {
        throw std::runtime_error("Landscape weight profile cannot be empty.");
    }

    const auto ids = sorted_weighted_cell_ids(weight_map);
    double raw_total = 0.0;
    weight_map.total_weight = 0.0;
    weight_map.minimum_weight = ids.empty()
        ? 0.0
        : std::numeric_limits<double>::infinity();
    weight_map.maximum_weight = ids.empty()
        ? 0.0
        : -std::numeric_limits<double>::infinity();

    for (const int cell_id : ids) {
        const auto weight_it = weight_map.weight_by_original_cell_id.find(cell_id);
        if (weight_it == weight_map.weight_by_original_cell_id.end() ||
            !std::isfinite(weight_it->second) ||
            weight_it->second <= 0.0) {
            throw std::runtime_error(
                "Landscape final weights must be finite and strictly positive.");
        }
        const double weight = weight_it->second;
        weight_map.total_weight += weight;
        weight_map.minimum_weight = std::min(weight_map.minimum_weight, weight);
        weight_map.maximum_weight = std::max(weight_map.maximum_weight, weight);

        const auto raw_it = weight_map.raw_weight_by_original_cell_id.find(cell_id);
        if (raw_it != weight_map.raw_weight_by_original_cell_id.end()) {
            if (!std::isfinite(raw_it->second)) {
                throw std::runtime_error("Landscape raw weights must be finite.");
            }
            raw_total += raw_it->second;
        } else {
            raw_total += weight;
        }

        const auto cluster_it = weight_map.cluster_id_by_original_cell_id.find(cell_id);
        if (cluster_it != weight_map.cluster_id_by_original_cell_id.end() &&
            cluster_it->second < 0) {
            throw std::runtime_error("Landscape cluster IDs must be nonnegative.");
        }
    }

    if (ids.empty()) {
        weight_map.raw_mean = 0.0;
        weight_map.normalized_mean = 0.0;
    } else {
        const double count = static_cast<double>(ids.size());
        weight_map.raw_mean = raw_total / count;
        weight_map.normalized_mean = weight_map.total_weight / count;
    }
    weight_map.deterministic_hash = deterministic_hash_for_final_weights(weight_map);
}

void validate_landscape_weight_map(
    const LandscapeWeightMap& weight_map,
    const std::vector<int>& expected_original_cell_ids) {
    if (weight_map.profile.empty()) {
        throw std::runtime_error("Landscape weight profile cannot be empty.");
    }

    const auto actual_ids = sorted_weighted_cell_ids(weight_map);
    for (const int cell_id : actual_ids) {
        const auto weight_it = weight_map.weight_by_original_cell_id.find(cell_id);
        if (weight_it == weight_map.weight_by_original_cell_id.end() ||
            !std::isfinite(weight_it->second) ||
            weight_it->second <= 0.0) {
            throw std::runtime_error(
                "Landscape final weights must be finite and strictly positive.");
        }
        const auto raw_it = weight_map.raw_weight_by_original_cell_id.find(cell_id);
        if (raw_it != weight_map.raw_weight_by_original_cell_id.end() &&
            !std::isfinite(raw_it->second)) {
            throw std::runtime_error("Landscape raw weights must be finite.");
        }
        const auto cluster_it = weight_map.cluster_id_by_original_cell_id.find(cell_id);
        if (cluster_it != weight_map.cluster_id_by_original_cell_id.end() &&
            cluster_it->second < 0) {
            throw std::runtime_error("Landscape cluster IDs must be nonnegative.");
        }
    }

    if (!expected_original_cell_ids.empty()) {
        const auto expected_ids = sorted_unique_or_throw(
            expected_original_cell_ids,
            "Expected landscape weight universe");
        std::size_t actual_pos = 0;
        std::size_t expected_pos = 0;
        while (actual_pos < actual_ids.size() || expected_pos < expected_ids.size()) {
            if (actual_pos >= actual_ids.size()) {
                throw std::runtime_error(
                    "Landscape weight map is missing required original cell ID " +
                    std::to_string(expected_ids[expected_pos]) + ".");
            }
            if (expected_pos >= expected_ids.size()) {
                throw std::runtime_error(
                    "Landscape weight map contains unexpected original cell ID " +
                    std::to_string(actual_ids[actual_pos]) + ".");
            }
            if (actual_ids[actual_pos] == expected_ids[expected_pos]) {
                ++actual_pos;
                ++expected_pos;
                continue;
            }
            if (actual_ids[actual_pos] < expected_ids[expected_pos]) {
                throw std::runtime_error(
                    "Landscape weight map contains unexpected original cell ID " +
                    std::to_string(actual_ids[actual_pos]) + ".");
            }
            throw std::runtime_error(
                "Landscape weight map is missing required original cell ID " +
                std::to_string(expected_ids[expected_pos]) + ".");
        }
    }

    LandscapeWeightMap copy = weight_map;
    recompute_landscape_weight_map_statistics_and_hash(copy);
    if (copy.deterministic_hash != weight_map.deterministic_hash) {
        throw std::runtime_error("Landscape weight map deterministic hash is stale.");
    }
}

std::vector<int> sorted_weighted_cell_ids(const LandscapeWeightMap& weight_map) {
    std::vector<int> ids;
    ids.reserve(weight_map.weight_by_original_cell_id.size());
    for (const auto& [cell_id, weight] : weight_map.weight_by_original_cell_id) {
        (void)weight;
        if (cell_id <= 0) {
            throw std::runtime_error("Landscape weight map contains a nonpositive original cell ID.");
        }
        ids.push_back(cell_id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<double> build_compact_weight_vector(
    const LandscapeWeightMap& weight_map,
    const opt::IndexMapper& index_mapper) {
    validate_landscape_weight_map(weight_map);
    std::vector<double> compact_weights(static_cast<std::size_t>(index_mapper.size()), 0.0);
    for (int compact_index = 0; compact_index < index_mapper.size(); ++compact_index) {
        const int original_cell_id = index_mapper.to_node(compact_index);
        const auto weight_it = weight_map.weight_by_original_cell_id.find(original_cell_id);
        if (weight_it == weight_map.weight_by_original_cell_id.end()) {
            throw std::runtime_error(
                "Landscape weight map is missing compact node " +
                std::to_string(compact_index) +
                " original cell ID " + std::to_string(original_cell_id) + ".");
        }
        compact_weights[static_cast<std::size_t>(compact_index)] = weight_it->second;
    }
    return compact_weights;
}

void write_landscape_weight_map_csv(
    const LandscapeWeightMap& weight_map,
    const std::filesystem::path& output_path) {
    validate_landscape_weight_map(weight_map);
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error(
            "Could not open landscape weight CSV for writing: " + output_path.string());
    }

    out.imbue(std::locale::classic());
    out << "cell_id,raw_weight,normalized_weight,cluster_id\n";
    for (const int cell_id : sorted_weighted_cell_ids(weight_map)) {
        const auto weight_it = weight_map.weight_by_original_cell_id.find(cell_id);
        const auto raw_it = weight_map.raw_weight_by_original_cell_id.find(cell_id);
        const auto cluster_it = weight_map.cluster_id_by_original_cell_id.find(cell_id);
        out << cell_id << ","
            << stable_double_string(raw_it == weight_map.raw_weight_by_original_cell_id.end()
                   ? weight_it->second
                   : raw_it->second)
            << ","
            << stable_double_string(weight_it->second)
            << ","
            << (cluster_it == weight_map.cluster_id_by_original_cell_id.end()
                   ? 0
                   : cluster_it->second)
            << "\n";
    }
}

LandscapeWeightMap load_landscape_weight_map_csv(
    const std::filesystem::path& input_path,
    const std::vector<int>& expected_original_cell_ids,
    const std::string& profile) {
    if (profile.empty()) {
        throw std::runtime_error("Landscape weight profile cannot be empty.");
    }
    std::ifstream in(input_path);
    if (!in) {
        throw std::runtime_error(
            "Could not open landscape weight CSV for reading: " + input_path.string());
    }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        throw std::runtime_error("Landscape weight CSV is empty: " + input_path.string());
    }
    const auto header = split_csv_line(header_line);
    const std::size_t cell_id_col = required_column(header, "cell_id");
    const std::size_t raw_col = required_column(header, "raw_weight");
    const std::size_t weight_col = required_column(header, "normalized_weight");
    const std::size_t cluster_col = required_column(header, "cluster_id");

    std::vector<LandscapeWeightRecord> records;
    std::string line;
    int line_number = 1;
    while (std::getline(in, line)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        const auto fields = split_csv_line(line);
        const std::size_t required_size =
            std::max(std::max(cell_id_col, raw_col), std::max(weight_col, cluster_col)) + 1;
        if (fields.size() < required_size) {
            throw std::runtime_error(
                "Landscape weight CSV row " + std::to_string(line_number) +
                " has too few columns.");
        }
        LandscapeWeightRecord record;
        record.original_cell_id = parse_int_strict(fields[cell_id_col], "cell_id");
        record.raw_weight = parse_double_strict(fields[raw_col], "raw_weight");
        record.weight = parse_double_strict(fields[weight_col], "normalized_weight");
        record.cluster_id = parse_int_strict(fields[cluster_col], "cluster_id");
        records.push_back(record);
    }

    auto weight_map = make_landscape_weight_map(profile, 0, true, records);
    validate_landscape_weight_map(weight_map, expected_original_cell_ids);
    return weight_map;
}

}  // namespace firebreak::core
