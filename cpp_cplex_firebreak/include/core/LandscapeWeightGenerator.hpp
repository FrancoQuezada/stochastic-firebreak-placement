#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/LandscapeWeightMap.hpp"

namespace firebreak::core {

struct WeightedLandscapeCell {
    int original_cell_id = 0;
    int row = 0;
    int column = 0;
};

struct LandscapeCellUniverse {
    std::string source;
    int rows = 0;
    int cols = 0;
    std::vector<WeightedLandscapeCell> cells;
};

struct LandscapeWeightGenerationConfig {
    std::string profile = "homogeneous";
    std::uint64_t seed = 0;
    bool normalize = true;

    double heterogeneous_min = 0.5;
    double heterogeneous_max = 1.5;

    int cluster_count = 3;
    double cluster_fraction = 0.15;
    double background_min = 0.5;
    double background_max = 1.0;
    double cluster_min = 2.0;
    double cluster_max = 4.0;
    int cluster_min_separation = 0;
};

struct LandscapeWeightGenerationMetadata {
    std::string generation_universe_source;
    int cell_count = 0;
    int clustered_cell_count = 0;
    std::vector<int> cluster_seed_ids;
    std::vector<int> cluster_sizes;
    double normalization_factor = 1.0;
    int generator_version = 1;
};

std::string normalize_landscape_weight_profile_name(const std::string& profile);

void validate_landscape_cell_universe(const LandscapeCellUniverse& universe);

LandscapeWeightGenerationConfig validate_landscape_weight_generation_config(
    const LandscapeWeightGenerationConfig& config);

LandscapeWeightMap normalize_landscape_weight_records(
    const std::string& profile,
    std::uint64_t seed,
    bool normalize,
    std::vector<LandscapeWeightRecord> records,
    double* normalization_factor = nullptr);

LandscapeWeightMap generate_landscape_weight_map(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    LandscapeWeightGenerationMetadata* metadata = nullptr);

LandscapeWeightMap generate_homogeneous_weight_map(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    LandscapeWeightGenerationMetadata* metadata = nullptr);

LandscapeWeightMap generate_heterogeneous_weight_map(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    LandscapeWeightGenerationMetadata* metadata = nullptr);

LandscapeWeightMap generate_clustered_weight_map(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    LandscapeWeightGenerationMetadata* metadata = nullptr);

void write_landscape_weight_generation_metadata_json(
    const std::filesystem::path& output_path,
    const std::string& landscape,
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    const LandscapeWeightMap& weight_map,
    const LandscapeWeightGenerationMetadata& metadata);

}  // namespace firebreak::core
