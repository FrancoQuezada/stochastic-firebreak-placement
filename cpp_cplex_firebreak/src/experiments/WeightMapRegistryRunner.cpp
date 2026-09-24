#include "experiments/WeightMapRegistryRunner.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

#include "core/CanonicalLandscape.hpp"
#include "experiments/LandscapeUniverseBuilder.hpp"
#include "experiments/WeightMapRegistry.hpp"
#include "io/PathUtils.hpp"

namespace firebreak::experiments {

int WeightMapRegistryRunner::run(const WeightMapRegistryRunnerOptions& options) const {
    if (options.instance_id.empty()) {
        throw std::runtime_error("--instance-id is required.");
    }
    if (options.forest_path.empty() || options.results_path.empty()) {
        throw std::runtime_error("--forest-path and --results-path are required.");
    }
    if (options.registry_root.empty()) {
        throw std::runtime_error("--weight-registry is required.");
    }

    const auto forest_path = firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = firebreak::io::resolve_input_path(options.results_path.string());
    const auto registry_root = firebreak::io::resolve_output_path(options.registry_root.string());

    std::vector<std::string> warnings;
    const auto universe = load_landscape_cell_universe(forest_path, results_path, warnings);
    const auto identity = core::make_canonical_landscape_identity(options.instance_id, universe);

    WeightMapRegistryRequest request;
    request.registry_root = registry_root;
    request.identity = identity;
    request.universe = universe;
    request.weight_profile = options.weight_profile;
    request.weight_replicate = options.weight_replicate;
    request.global_weight_seed = options.global_weight_seed;
    request.config = options.config;
    request.allow_generate = true;

    WeightMapRegistry registry;
    const auto entry = registry.ensure(request);

    std::cout << "Instance: " << options.instance_id << "\n";
    std::cout << "Landscape family: " << identity.landscape_family << "\n";
    std::cout << "Canonical landscape id: " << entry.canonical_landscape_id << "\n";
    std::cout << "Paired landscape id: " << identity.paired_landscape_id << "\n";
    std::cout << "Grid: " << identity.grid_rows << "x" << identity.grid_cols << "\n";
    std::cout << "Cell count: " << entry.cell_count << "\n";
    std::cout << "Universe hash: " << entry.source_universe_hash << "\n";
    std::cout << "Weight profile: " << entry.weight_profile << "\n";
    std::cout << "Weight replicate: " << entry.weight_replicate << "\n";
    std::cout << "Generation seed: " << entry.weight_generation_seed << "\n";
    std::cout << "Generator version: " << entry.weight_generator_version << "\n";
    std::cout << "Weight map hash: " << entry.weight_map_hash << "\n";
    std::cout << "Normalization: " << entry.normalization_mode << "\n";
    std::cout << "Mean weight: " << entry.mean_weight << "\n";
    std::cout << "Min weight: " << entry.minimum_weight << "\n";
    std::cout << "Max weight: " << entry.maximum_weight << "\n";
    std::cout << "Cluster count: " << entry.cluster_count << "\n";
    std::cout << "Registry root: " << firebreak::io::path_to_string(registry_root) << "\n";
    std::cout << "Weight map path: " << entry.weight_map_path << "\n";
    for (const auto& warning : warnings) {
        std::cout << "Warning: " << warning << "\n";
    }
    return 0;
}

}  // namespace firebreak::experiments
