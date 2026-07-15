#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace firebreak::opt {
class IndexMapper;
}

namespace firebreak::core {

struct LandscapeWeightRecord {
    int original_cell_id = 0;
    double raw_weight = 0.0;
    double weight = 0.0;
    int cluster_id = 0;
};

struct LandscapeWeightMap {
    std::string profile = "homogeneous";
    std::uint64_t seed = 0;
    bool normalized = false;
    std::unordered_map<int, double> raw_weight_by_original_cell_id;
    std::unordered_map<int, double> weight_by_original_cell_id;
    std::unordered_map<int, int> cluster_id_by_original_cell_id;
    double raw_mean = 0.0;
    double normalized_mean = 0.0;
    double minimum_weight = 0.0;
    double maximum_weight = 0.0;
    double total_weight = 0.0;
    double normalization_factor = 1.0;
    std::string deterministic_hash = "fnv1a64:cbf29ce484222325";
};

LandscapeWeightMap make_landscape_weight_map(
    const std::string& profile,
    std::uint64_t seed,
    bool normalized,
    const std::vector<LandscapeWeightRecord>& records);

LandscapeWeightMap make_homogeneous_weight_map(
    const std::vector<int>& original_cell_ids);

void recompute_landscape_weight_map_statistics_and_hash(
    LandscapeWeightMap& weight_map);

void validate_landscape_weight_map(
    const LandscapeWeightMap& weight_map,
    const std::vector<int>& expected_original_cell_ids = {});

std::vector<int> sorted_weighted_cell_ids(const LandscapeWeightMap& weight_map);

std::vector<double> build_compact_weight_vector(
    const LandscapeWeightMap& weight_map,
    const opt::IndexMapper& index_mapper);

void write_landscape_weight_map_csv(
    const LandscapeWeightMap& weight_map,
    const std::filesystem::path& output_path);

LandscapeWeightMap load_landscape_weight_map_csv(
    const std::filesystem::path& input_path,
    const std::vector<int>& expected_original_cell_ids = {},
    const std::string& profile = "loaded");

}  // namespace firebreak::core
