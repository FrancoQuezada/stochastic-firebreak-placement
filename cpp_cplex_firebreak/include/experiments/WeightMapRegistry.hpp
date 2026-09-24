#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "core/CanonicalLandscape.hpp"
#include "core/LandscapeWeightGenerator.hpp"
#include "core/LandscapeWeightMap.hpp"

namespace firebreak::experiments {

// One logical weight-map entry in the registry. The logical identity is
// (canonical_landscape_id, weight_profile, weight_replicate, weight_generator_version).
struct WeightMapRegistryEntry {
    std::string canonical_landscape_id;
    std::string weight_profile;
    int weight_replicate = 0;
    std::uint64_t weight_generation_seed = 0;
    int weight_generator_version = 1;
    std::string weight_map_path;   // relative to the registry root
    std::string weight_map_hash;
    std::string source_universe_hash;
    int cell_count = 0;
    std::string normalization_mode = "mean_one";
    double mean_weight = 0.0;
    double minimum_weight = 0.0;
    double maximum_weight = 0.0;
    int cluster_count = 0;
    double cluster_fraction = 0.0;
    double cluster_multiplier = 0.0;
    double background_multiplier = 0.0;
};

struct WeightMapRegistryRequest {
    std::filesystem::path registry_root;
    core::CanonicalLandscapeIdentity identity;
    core::LandscapeCellUniverse universe;  // the full physical universe
    std::string weight_profile = "homogeneous";
    int weight_replicate = 0;
    std::uint64_t global_weight_seed = 0;
    // Generation parameters. `profile` and `seed` are overwritten by the registry from
    // weight_profile and the deterministically derived seed; the remaining fields
    // (heterogeneous / clustered knobs, normalize) are honored.
    core::LandscapeWeightGenerationConfig config;
    // When false, a missing map is a hard error (the read-only worker contract).
    bool allow_generate = false;
};

class WeightMapRegistry {
public:
    // Idempotently ensure the logical entry exists. If present, validate it (and reject
    // mismatched parameters / corrupted content). If absent and allow_generate, generate
    // it atomically. If absent and generation is not allowed, throw.
    WeightMapRegistryEntry ensure(const WeightMapRegistryRequest& request) const;

    // Read-only worker entry point: load and validate an existing entry, never generate.
    WeightMapRegistryEntry load(
        const std::filesystem::path& registry_root,
        const std::string& canonical_landscape_id,
        const std::string& weight_profile,
        int weight_replicate,
        int weight_generator_version = 1) const;

    // Load the canonical weight map CSV for an entry (over the full physical universe).
    core::LandscapeWeightMap load_weight_map(
        const std::filesystem::path& registry_root,
        const WeightMapRegistryEntry& entry) const;

    // Deterministic relative path of an entry's directory, e.g.
    // "<canonical>/<profile>/replicate_<r>".
    static std::filesystem::path entry_relative_dir(
        const std::string& canonical_landscape_id,
        const std::string& weight_profile,
        int weight_replicate);
};

}  // namespace firebreak::experiments
