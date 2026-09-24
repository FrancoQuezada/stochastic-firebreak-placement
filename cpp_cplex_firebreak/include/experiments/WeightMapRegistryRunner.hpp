#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "core/LandscapeWeightGenerator.hpp"

namespace firebreak::experiments {

// Pre-generation stage for the canonical weight-map registry (CLI: ensure-weight-map).
// Builds the physical universe, derives the canonical landscape identity and generation
// seed, and idempotently ensures the registry entry exists. Intended to be run once,
// single-process, before workers launch.
struct WeightMapRegistryRunnerOptions {
    std::string instance_id;   // e.g. new20x20 (family/pairing derived from this)
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::filesystem::path registry_root = "weight_maps";
    std::string weight_profile = "homogeneous";
    int weight_replicate = 0;
    std::uint64_t global_weight_seed = 0;
    core::LandscapeWeightGenerationConfig config;
};

class WeightMapRegistryRunner {
public:
    int run(const WeightMapRegistryRunnerOptions& options) const;
};

}  // namespace firebreak::experiments
