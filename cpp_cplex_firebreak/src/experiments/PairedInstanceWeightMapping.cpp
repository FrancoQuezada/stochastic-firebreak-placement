#include "experiments/PairedInstanceWeightMapping.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace firebreak::experiments {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void fnv_append(std::uint64_t& hash, const std::string& text) {
    for (const unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
    }
    hash ^= static_cast<std::uint64_t>('\x1f');
    hash *= kFnvPrime;
}

std::string fnv_to_hex(std::uint64_t hash) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string stable_double_string(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::scientific << std::setprecision(17) << value;
    return out.str();
}

double weight_for_original_id(const core::LandscapeWeightMap& map, int original_id) {
    const auto it = map.weight_by_original_cell_id.find(original_id);
    if (it == map.weight_by_original_cell_id.end()) {
        return std::nan("");
    }
    return it->second;
}

}  // namespace

InstanceWeightMapping map_weight_map_to_instance(
    const core::LandscapeWeightMap& map,
    const opt::IndexMapper& index_mapper) {
    core::validate_landscape_weight_map(map);
    InstanceWeightMapping result;
    const int size = index_mapper.size();
    result.compact_weights.assign(static_cast<std::size_t>(size), 0.0);

    std::uint64_t hash = kFnvOffsetBasis;
    for (int compact_index = 0; compact_index < size; ++compact_index) {
        const int original_id = index_mapper.to_node(compact_index);
        const double weight = weight_for_original_id(map, original_id);
        if (std::isnan(weight)) {
            ++result.missing_count;
            result.missing_original_ids.push_back(original_id);
            continue;
        }
        ++result.mapped_count;
        result.compact_weights[static_cast<std::size_t>(compact_index)] = weight;
        fnv_append(hash, std::to_string(compact_index));
        fnv_append(hash, std::to_string(original_id));
        fnv_append(hash, stable_double_string(weight));
    }
    result.mapping_hash = fnv_to_hex(hash);

    if (result.missing_count > 0) {
        throw std::runtime_error(
            "Weight map is missing " + std::to_string(result.missing_count) +
            " instance original cell ID(s), starting with " +
            std::to_string(result.missing_original_ids.front()) + ".");
    }
    return result;
}

InstanceWeightMapping map_weight_map_to_instance_by_coordinate(
    const core::LandscapeWeightMap& map,
    const std::vector<std::pair<int, std::pair<int, int>>>& coordinate_of_original_id,
    const std::vector<std::pair<int, std::pair<int, int>>>& instance_cells) {
    core::validate_landscape_weight_map(map);

    // Build (row,col) -> original_id from the map's cells, rejecting duplicate coords.
    std::unordered_map<long long, int> original_id_by_coord;
    original_id_by_coord.reserve(coordinate_of_original_id.size());
    for (const auto& [original_id, coord] : coordinate_of_original_id) {
        if (map.weight_by_original_cell_id.find(original_id) ==
            map.weight_by_original_cell_id.end()) {
            throw std::runtime_error(
                "Coordinate fallback references original cell ID " +
                std::to_string(original_id) + " that is absent from the weight map.");
        }
        const long long key =
            (static_cast<long long>(coord.first) << 32) ^
            static_cast<unsigned int>(coord.second);
        if (!original_id_by_coord.emplace(key, original_id).second) {
            throw std::runtime_error(
                "Coordinate fallback rejected: duplicate coordinate (row " +
                std::to_string(coord.first) + ", col " + std::to_string(coord.second) +
                ") maps to more than one original cell ID.");
        }
    }

    // Reject duplicate coordinates within the instance as well.
    std::unordered_set<long long> seen_instance_coords;
    seen_instance_coords.reserve(instance_cells.size());

    InstanceWeightMapping result;
    result.mapping_method = "coordinate";
    result.compact_weights.assign(instance_cells.size(), 0.0);

    std::uint64_t hash = kFnvOffsetBasis;
    for (const auto& [compact_index, coord] : instance_cells) {
        if (compact_index < 0 ||
            compact_index >= static_cast<int>(instance_cells.size())) {
            throw std::runtime_error("Coordinate fallback got an out-of-range compact index.");
        }
        const long long key =
            (static_cast<long long>(coord.first) << 32) ^
            static_cast<unsigned int>(coord.second);
        if (!seen_instance_coords.insert(key).second) {
            throw std::runtime_error(
                "Coordinate fallback rejected: instance has a duplicate coordinate (row " +
                std::to_string(coord.first) + ", col " + std::to_string(coord.second) + ").");
        }
        const auto it = original_id_by_coord.find(key);
        if (it == original_id_by_coord.end()) {
            ++result.missing_count;
            continue;
        }
        const double weight = weight_for_original_id(map, it->second);
        ++result.mapped_count;
        result.compact_weights[static_cast<std::size_t>(compact_index)] = weight;
        fnv_append(hash, std::to_string(compact_index));
        fnv_append(hash, std::to_string(it->second));
        fnv_append(hash, stable_double_string(weight));
    }
    result.mapping_hash = fnv_to_hex(hash);

    if (result.missing_count > 0) {
        throw std::runtime_error(
            "Coordinate fallback could not map " + std::to_string(result.missing_count) +
            " instance cell(s) to a coordinate in the weight map.");
    }
    return result;
}

ReducedReburnMappingReport compare_reduced_reburn(
    const core::LandscapeWeightMap& map,
    const std::vector<int>& reduced_original_ids,
    const std::vector<int>& reburn_original_ids) {
    core::validate_landscape_weight_map(map);
    ReducedReburnMappingReport report;
    report.canonical_cell_count = static_cast<int>(map.weight_by_original_cell_id.size());
    report.reduced_cell_count = static_cast<int>(reduced_original_ids.size());
    report.reburn_cell_count = static_cast<int>(reburn_original_ids.size());

    auto tally = [&](const std::vector<int>& ids, int& mapped, int& missing,
                     std::unordered_set<int>& unique_ids) {
        for (const int id : ids) {
            if (!unique_ids.insert(id).second) {
                ++report.duplicate_original_id_count;
                continue;
            }
            if (map.weight_by_original_cell_id.find(id) !=
                map.weight_by_original_cell_id.end()) {
                ++mapped;
            } else {
                ++missing;
            }
        }
    };

    std::unordered_set<int> reduced_ids;
    std::unordered_set<int> reburn_ids;
    reduced_ids.reserve(reduced_original_ids.size());
    reburn_ids.reserve(reburn_original_ids.size());
    tally(reduced_original_ids, report.reduced_mapped_count, report.reduced_missing_count,
          reduced_ids);
    tally(reburn_original_ids, report.reburn_mapped_count, report.reburn_missing_count,
          reburn_ids);

    if (report.duplicate_original_id_count > 0) {
        throw std::runtime_error(
            "Reduced/reburn mapping found " +
            std::to_string(report.duplicate_original_id_count) +
            " duplicate original cell ID(s).");
    }

    // Shared-cell weight equality. Both members read the same canonical map keyed by
    // original ID, so equality holds by construction; we assert it explicitly.
    std::uint64_t hash = kFnvOffsetBasis;
    std::vector<int> shared;
    shared.reserve(std::min(reduced_ids.size(), reburn_ids.size()));
    for (const int id : reduced_ids) {
        if (reburn_ids.find(id) != reburn_ids.end()) {
            shared.push_back(id);
        }
    }
    std::sort(shared.begin(), shared.end());
    report.shared_cell_count = static_cast<int>(shared.size());
    for (const int id : shared) {
        const double reduced_weight = weight_for_original_id(map, id);
        const double reburn_weight = weight_for_original_id(map, id);
        if (std::isnan(reduced_weight) || std::isnan(reburn_weight) ||
            reduced_weight != reburn_weight) {
            ++report.shared_weight_mismatch_count;
        }
        fnv_append(hash, std::to_string(id));
        fnv_append(hash, stable_double_string(reduced_weight));
    }
    report.mapping_hash = fnv_to_hex(hash);

    if (report.shared_weight_mismatch_count != 0) {
        throw std::runtime_error(
            "Reduced/reburn shared-weight invariant violated: " +
            std::to_string(report.shared_weight_mismatch_count) + " mismatch(es).");
    }
    return report;
}

}  // namespace firebreak::experiments
