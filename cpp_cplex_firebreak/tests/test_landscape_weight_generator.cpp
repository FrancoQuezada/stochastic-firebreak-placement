#include <cassert>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/LandscapeWeightGenerator.hpp"
#include "core/LandscapeWeightMap.hpp"

namespace {

namespace fs = std::filesystem;

void assert_close(double actual, double expected, double tolerance = 1.0e-9) {
    assert(std::fabs(actual - expected) <= tolerance);
}

template <typename Fn>
void expect_throw(Fn&& fn, const std::string& expected_message_fragment) {
    bool threw = false;
    try {
        fn();
    } catch (const std::runtime_error& exc) {
        threw = true;
        const std::string message = exc.what();
        assert(message.find(expected_message_fragment) != std::string::npos);
    }
    assert(threw);
}

firebreak::core::LandscapeCellUniverse rectangular_universe(
    int rows,
    int cols,
    const std::set<int>& missing_ids = {}) {
    firebreak::core::LandscapeCellUniverse universe;
    universe.source = "synthetic";
    universe.rows = rows;
    universe.cols = cols;
    for (int row = 1; row <= rows; ++row) {
        for (int col = 1; col <= cols; ++col) {
            const int id = (row - 1) * cols + col;
            if (missing_ids.find(id) != missing_ids.end()) {
                continue;
            }
            universe.cells.push_back(firebreak::core::WeightedLandscapeCell{id, row, col});
        }
    }
    return universe;
}

fs::path temp_path(const std::string& filename) {
    return fs::temp_directory_path() / filename;
}

std::unordered_map<int, firebreak::core::WeightedLandscapeCell> cells_by_id(
    const firebreak::core::LandscapeCellUniverse& universe) {
    std::unordered_map<int, firebreak::core::WeightedLandscapeCell> out;
    for (const auto& cell : universe.cells) {
        out[cell.original_cell_id] = cell;
    }
    return out;
}

int chebyshev_distance(
    const firebreak::core::WeightedLandscapeCell& a,
    const firebreak::core::WeightedLandscapeCell& b) {
    return std::max(std::abs(a.row - b.row), std::abs(a.column - b.column));
}

std::vector<int> cluster_ids(const firebreak::core::LandscapeWeightMap& map, int cluster_id) {
    std::vector<int> ids;
    for (const auto& [cell_id, value] : map.cluster_id_by_original_cell_id) {
        if (value == cluster_id) {
            ids.push_back(cell_id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void assert_cluster_connected(
    const firebreak::core::LandscapeCellUniverse& universe,
    const firebreak::core::LandscapeWeightMap& map,
    int cluster_id) {
    const auto ids = cluster_ids(map, cluster_id);
    assert(!ids.empty());
    const auto by_id = cells_by_id(universe);
    std::unordered_set<int> cluster_set(ids.begin(), ids.end());
    std::unordered_set<int> visited;
    std::queue<int> q;
    q.push(ids.front());
    visited.insert(ids.front());
    while (!q.empty()) {
        const int current_id = q.front();
        q.pop();
        const auto current = by_id.at(current_id);
        for (const int neighbor_id : cluster_set) {
            if (visited.find(neighbor_id) != visited.end()) {
                continue;
            }
            if (chebyshev_distance(current, by_id.at(neighbor_id)) == 1) {
                visited.insert(neighbor_id);
                q.push(neighbor_id);
            }
        }
    }
    assert(visited.size() == cluster_set.size());
}

void test_homogeneous_generation() {
    const auto universe = rectangular_universe(3, 3);
    firebreak::core::LandscapeWeightGenerationConfig config;
    config.profile = "homogeneous";
    config.seed = 123;
    config.normalize = true;
    firebreak::core::LandscapeWeightGenerationMetadata metadata;
    const auto generated = firebreak::core::generate_landscape_weight_map(universe, config, &metadata);
    const auto phase1 = firebreak::core::make_homogeneous_weight_map({1, 2, 3, 4, 5, 6, 7, 8, 9});
    assert(generated.profile == "homogeneous");
    assert(generated.deterministic_hash == phase1.deterministic_hash);
    for (const int id : firebreak::core::sorted_weighted_cell_ids(generated)) {
        assert_close(generated.raw_weight_by_original_cell_id.at(id), 1.0);
        assert_close(generated.weight_by_original_cell_id.at(id), 1.0);
        assert(generated.cluster_id_by_original_cell_id.at(id) == 0);
    }
    assert_close(generated.normalized_mean, 1.0);
    assert_close(metadata.normalization_factor, 1.0);

    config.seed = 999;
    const auto repeated = firebreak::core::generate_landscape_weight_map(universe, config);
    assert(repeated.deterministic_hash == generated.deterministic_hash);
}

void test_heterogeneous_determinism_and_ranges() {
    const auto universe = rectangular_universe(4, 4);
    firebreak::core::LandscapeWeightGenerationConfig config;
    config.profile = "heterogeneous";
    config.seed = 77;
    config.heterogeneous_min = 0.25;
    config.heterogeneous_max = 2.0;
    config.normalize = true;
    const auto a = firebreak::core::generate_landscape_weight_map(universe, config);
    const auto b = firebreak::core::generate_landscape_weight_map(universe, config);
    assert(a.deterministic_hash == b.deterministic_hash);
    assert_close(a.normalized_mean, 1.0, 1.0e-12);
    for (const int id : firebreak::core::sorted_weighted_cell_ids(a)) {
        const double raw = a.raw_weight_by_original_cell_id.at(id);
        assert(raw >= config.heterogeneous_min);
        assert(raw <= config.heterogeneous_max);
        assert(a.cluster_id_by_original_cell_id.at(id) == 0);
        assert_close(raw, b.raw_weight_by_original_cell_id.at(id));
        assert_close(a.weight_by_original_cell_id.at(id), b.weight_by_original_cell_id.at(id));
    }

    config.seed = 78;
    const auto c = firebreak::core::generate_landscape_weight_map(universe, config);
    bool any_changed = false;
    for (const int id : firebreak::core::sorted_weighted_cell_ids(a)) {
        if (std::fabs(a.raw_weight_by_original_cell_id.at(id) -
                      c.raw_weight_by_original_cell_id.at(id)) > 1.0e-12) {
            any_changed = true;
            break;
        }
    }
    assert(any_changed);

    config.normalize = false;
    const auto unnormalized = firebreak::core::generate_landscape_weight_map(universe, config);
    for (const int id : firebreak::core::sorted_weighted_cell_ids(unnormalized)) {
        assert_close(
            unnormalized.raw_weight_by_original_cell_id.at(id),
            unnormalized.weight_by_original_cell_id.at(id));
    }
}

void test_heterogeneous_invalid_config() {
    const auto universe = rectangular_universe(2, 2);
    firebreak::core::LandscapeWeightGenerationConfig config;
    config.profile = "heterogeneous";
    config.heterogeneous_min = 0.0;
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(universe, config); },
                 "weight_min");
    config.heterogeneous_min = -1.0;
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(universe, config); },
                 "weight_min");
    config.heterogeneous_min = 2.0;
    config.heterogeneous_max = 1.0;
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(universe, config); },
                 "less than or equal");
    config.heterogeneous_min = std::numeric_limits<double>::quiet_NaN();
    config.heterogeneous_max = 1.0;
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(universe, config); },
                 "weight_min");
    config.heterogeneous_min = 0.5;
    config.heterogeneous_max = std::numeric_limits<double>::infinity();
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(universe, config); },
                 "weight_max");
    config.heterogeneous_max = 1.5;
    firebreak::core::LandscapeCellUniverse empty;
    empty.source = "empty";
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(empty, config); },
                 "cannot be empty");
}

void test_cluster_count_coverage_connectivity_and_ranges() {
    const auto universe = rectangular_universe(6, 6);
    firebreak::core::LandscapeWeightGenerationConfig config;
    config.profile = "clustered";
    config.seed = 12345;
    config.cluster_count = 3;
    config.cluster_fraction = 0.25;
    config.cluster_min_separation = 2;
    config.background_min = 0.5;
    config.background_max = 1.0;
    config.cluster_min = 2.0;
    config.cluster_max = 4.0;
    config.normalize = true;
    firebreak::core::LandscapeWeightGenerationMetadata metadata;
    const auto map = firebreak::core::generate_landscape_weight_map(universe, config, &metadata);

    assert(metadata.clustered_cell_count == 9);
    assert((metadata.cluster_sizes == std::vector<int>{3, 3, 3}));
    assert(metadata.cluster_seed_ids.size() == 3);
    std::set<int> clusters;
    int clustered_cells = 0;
    double max_background_raw = -1.0;
    double min_cluster_raw = 999.0;
    for (const int id : firebreak::core::sorted_weighted_cell_ids(map)) {
        const int cluster_id = map.cluster_id_by_original_cell_id.at(id);
        const double raw = map.raw_weight_by_original_cell_id.at(id);
        if (cluster_id == 0) {
            assert(raw >= config.background_min);
            assert(raw <= config.background_max);
            max_background_raw = std::max(max_background_raw, raw);
        } else {
            clusters.insert(cluster_id);
            ++clustered_cells;
            assert(raw >= config.cluster_min);
            assert(raw <= config.cluster_max);
            min_cluster_raw = std::min(min_cluster_raw, raw);
        }
    }
    assert((clusters == std::set<int>{1, 2, 3}));
    assert(clustered_cells == metadata.clustered_cell_count);
    assert(min_cluster_raw > max_background_raw);
    assert_close(map.normalized_mean, 1.0, 1.0e-12);

    const auto by_id = cells_by_id(universe);
    for (std::size_t i = 0; i < metadata.cluster_seed_ids.size(); ++i) {
        for (std::size_t j = i + 1; j < metadata.cluster_seed_ids.size(); ++j) {
            assert(
                chebyshev_distance(
                    by_id.at(metadata.cluster_seed_ids[i]),
                    by_id.at(metadata.cluster_seed_ids[j])) >= config.cluster_min_separation);
        }
    }
    for (int cluster_id = 1; cluster_id <= config.cluster_count; ++cluster_id) {
        assert(static_cast<int>(cluster_ids(map, cluster_id).size()) ==
               metadata.cluster_sizes[static_cast<std::size_t>(cluster_id - 1)]);
        assert_cluster_connected(universe, map, cluster_id);
    }
}

void test_cluster_determinism_and_impossible_configs() {
    const auto universe = rectangular_universe(6, 6);
    firebreak::core::LandscapeWeightGenerationConfig config;
    config.profile = "clustered";
    config.seed = 42;
    config.cluster_count = 2;
    config.cluster_fraction = 0.2;
    const auto a = firebreak::core::generate_landscape_weight_map(universe, config);
    const auto b = firebreak::core::generate_landscape_weight_map(universe, config);
    assert(a.deterministic_hash == b.deterministic_hash);
    for (const int id : firebreak::core::sorted_weighted_cell_ids(a)) {
        assert(a.cluster_id_by_original_cell_id.at(id) == b.cluster_id_by_original_cell_id.at(id));
    }

    config.seed = 43;
    const auto c = firebreak::core::generate_landscape_weight_map(universe, config);
    assert(c.deterministic_hash != a.deterministic_hash);

    config.cluster_count = 10;
    config.cluster_fraction = 0.1;
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(universe, config); },
                 "fewer clustered cells");

    config.cluster_count = 2;
    config.cluster_fraction = 0.2;
    config.cluster_min_separation = 100;
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(universe, config); },
                 "Could not generate exact connected clusters");
}

void test_irregular_landscape_mask() {
    const auto universe = rectangular_universe(3, 3, {2, 4, 6, 8});
    firebreak::core::LandscapeWeightGenerationConfig config;
    config.profile = "clustered";
    config.seed = 5;
    config.cluster_count = 1;
    config.cluster_fraction = 1.0;
    const auto map = firebreak::core::generate_landscape_weight_map(universe, config);
    assert(map.weight_by_original_cell_id.size() == universe.cells.size());
    for (const int missing : {2, 4, 6, 8}) {
        assert(map.weight_by_original_cell_id.find(missing) == map.weight_by_original_cell_id.end());
    }
    assert_cluster_connected(universe, map, 1);

    const auto impossible = rectangular_universe(3, 3, {2, 4, 5, 6, 8});
    expect_throw([&] { (void)firebreak::core::generate_landscape_weight_map(impossible, config); },
                 "Could not generate exact connected clusters");
}

void test_serialization_round_trip() {
    const auto universe = rectangular_universe(4, 4);
    const std::vector<std::string> profiles = {"homogeneous", "heterogeneous", "clustered"};
    for (const auto& profile : profiles) {
        firebreak::core::LandscapeWeightGenerationConfig config;
        config.profile = profile;
        config.seed = 91;
        config.cluster_count = 2;
        config.cluster_fraction = 0.25;
        const auto map = firebreak::core::generate_landscape_weight_map(universe, config);
        const fs::path path = temp_path("firebreak_generator_" + profile + ".csv");
        firebreak::core::write_landscape_weight_map_csv(map, path);
        const auto loaded = firebreak::core::load_landscape_weight_map_csv(
            path,
            firebreak::core::sorted_weighted_cell_ids(map),
            profile);
        assert(loaded.deterministic_hash == map.deterministic_hash);
        assert_close(loaded.raw_mean, map.raw_mean);
        assert_close(loaded.normalized_mean, map.normalized_mean);
        assert_close(loaded.minimum_weight, map.minimum_weight);
        assert_close(loaded.maximum_weight, map.maximum_weight);
        assert_close(loaded.total_weight, map.total_weight);
        for (const int id : firebreak::core::sorted_weighted_cell_ids(map)) {
            assert_close(
                loaded.raw_weight_by_original_cell_id.at(id),
                map.raw_weight_by_original_cell_id.at(id));
            assert_close(
                loaded.weight_by_original_cell_id.at(id),
                map.weight_by_original_cell_id.at(id));
            assert(
                loaded.cluster_id_by_original_cell_id.at(id) ==
                map.cluster_id_by_original_cell_id.at(id));
        }
        fs::remove(path);
    }
}

}  // namespace

int main() {
    test_homogeneous_generation();
    test_heterogeneous_determinism_and_ranges();
    test_heterogeneous_invalid_config();
    test_cluster_count_coverage_connectivity_and_ranges();
    test_cluster_determinism_and_impossible_configs();
    test_irregular_landscape_mask();
    test_serialization_round_trip();
    std::cout << "All landscape weight generator tests passed.\n";
    return 0;
}
