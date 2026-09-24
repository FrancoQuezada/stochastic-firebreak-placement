#include "experiments/WeightMapGenerationRunner.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "core/LandscapeWeightMap.hpp"
#include "experiments/LandscapeUniverseBuilder.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"

namespace firebreak::experiments {

namespace {

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

int count_clusters(const core::LandscapeWeightMap& weight_map) {
    int max_cluster = 0;
    for (const auto& [cell_id, cluster_id] : weight_map.cluster_id_by_original_cell_id) {
        (void)cell_id;
        max_cluster = std::max(max_cluster, cluster_id);
    }
    return max_cluster;
}

}  // namespace

int WeightMapGenerationRunner::run(const WeightMapGenerationOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.output_csv_path.empty()) {
        throw std::runtime_error("--output-csv is required.");
    }
    if (options.output_json_path.empty()) {
        throw std::runtime_error("--output-json is required.");
    }

    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());
    const auto output_csv_path = firebreak::io::resolve_output_path(options.output_csv_path.string());
    const auto output_json_path = firebreak::io::resolve_output_path(options.output_json_path.string());

    std::vector<std::string> warnings;
    firebreak::io::Cell2FireReader reader;
    auto layout = firebreak::io::Cell2FireLayout::Legacy;
    try {
        layout = reader.detect_layout(results_path);
    } catch (const std::runtime_error& exc) {
        warnings.push_back(std::string("Could not detect Cell2Fire result layout for metadata checks: ") + exc.what());
    }
    const auto forest_info = reader.read_forest_info(forest_path, results_path, layout, warnings);
    const auto universe = build_landscape_cell_universe(forest_info);

    core::LandscapeWeightGenerationMetadata metadata;
    const auto config = core::validate_landscape_weight_generation_config(options.config);
    const auto weight_map = core::generate_landscape_weight_map(universe, config, &metadata);
    core::write_landscape_weight_map_csv(weight_map, output_csv_path);
    core::write_landscape_weight_generation_metadata_json(
        output_json_path,
        options.landscape,
        universe,
        config,
        weight_map,
        metadata);

    std::cout << "Landscape: " << options.landscape << "\n";
    std::cout << "Profile: " << weight_map.profile << "\n";
    std::cout << "Seed: " << weight_map.seed << "\n";
    std::cout << "Cell count: " << metadata.cell_count << "\n";
    std::cout << "Universe source: " << metadata.generation_universe_source << "\n";
    std::cout << "Raw mean: " << weight_map.raw_mean << "\n";
    std::cout << "Final mean: " << weight_map.normalized_mean << "\n";
    std::cout << "Minimum weight: " << weight_map.minimum_weight << "\n";
    std::cout << "Maximum weight: " << weight_map.maximum_weight << "\n";
    std::cout << "Total weight: " << weight_map.total_weight << "\n";
    std::cout << "Normalization factor: " << metadata.normalization_factor << "\n";
    std::cout << "Cluster count: " << count_clusters(weight_map) << "\n";
    std::cout << "Clustered cells: " << metadata.clustered_cell_count << "\n";
    std::cout << "Cluster seed IDs:";
    for (const int seed_id : metadata.cluster_seed_ids) {
        std::cout << " " << seed_id;
    }
    std::cout << "\n";
    std::cout << "Cluster sizes:";
    for (const int size : metadata.cluster_sizes) {
        std::cout << " " << size;
    }
    std::cout << "\n";
    std::cout << "Hash: " << weight_map.deterministic_hash << "\n";
    std::cout << "Wrote CSV: " << firebreak::io::path_to_string(output_csv_path) << "\n";
    std::cout << "Wrote JSON: " << firebreak::io::path_to_string(output_json_path) << "\n";
    for (const auto& warning : warnings) {
        std::cout << "Warning: " << warning << "\n";
    }
    return 0;
}

}  // namespace firebreak::experiments
