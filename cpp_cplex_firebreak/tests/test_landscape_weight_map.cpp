#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Instance.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "opt/IndexMapper.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"

namespace {

namespace fs = std::filesystem;

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-9);
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

fs::path temp_path(const std::string& filename) {
    return fs::temp_directory_path() / filename;
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path);
    assert(out);
    out << text;
}

firebreak::core::LandscapeWeightMap make_nonuniform_map(
    const std::vector<firebreak::core::LandscapeWeightRecord>& records) {
    return firebreak::core::make_landscape_weight_map(
        "loaded",
        42,
        true,
        records);
}

void test_homogeneous_construction() {
    const auto map = firebreak::core::make_homogeneous_weight_map({30, 10, 20});
    assert(map.profile == "homogeneous");
    assert(map.seed == 0);
    assert(!map.normalized);
    assert(map.weight_by_original_cell_id.size() == 3);
    assert((firebreak::core::sorted_weighted_cell_ids(map) == std::vector<int>{10, 20, 30}));
    for (const int id : {10, 20, 30}) {
        assert_close(map.weight_by_original_cell_id.at(id), 1.0);
        assert_close(map.raw_weight_by_original_cell_id.at(id), 1.0);
        assert(map.cluster_id_by_original_cell_id.at(id) == 0);
    }
    assert_close(map.raw_mean, 1.0);
    assert_close(map.normalized_mean, 1.0);
    assert_close(map.minimum_weight, 1.0);
    assert_close(map.maximum_weight, 1.0);
    assert_close(map.total_weight, 3.0);
    assert(!map.deterministic_hash.empty());

    const auto repeated = firebreak::core::make_homogeneous_weight_map({10, 20, 30});
    assert(map.deterministic_hash == repeated.deterministic_hash);
}

void test_duplicate_input_ids() {
    expect_throw(
        [] {
            (void)firebreak::core::make_homogeneous_weight_map({1, 2, 2});
        },
        "duplicate original cell ID");

    expect_throw(
        [] {
            (void)make_nonuniform_map({
                {1, 1.0, 1.0, 0},
                {1, 2.0, 2.0, 0},
            });
        },
        "Duplicate landscape weight record");
}

void test_invalid_weights_on_load() {
    const fs::path path = temp_path("firebreak_invalid_weights.csv");
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"0.0", "finite and strictly positive"},
        {"-1.0", "finite and strictly positive"},
        {"nan", "finite and strictly positive"},
        {"inf", "finite and strictly positive"},
        {"-inf", "finite and strictly positive"},
        {"bad", "Invalid numeric value"},
    };
    for (const auto& [value, message] : cases) {
        write_text(
            path,
            "cell_id,raw_weight,normalized_weight,cluster_id\n"
            "1,1.0," + value + ",0\n");
        expect_throw(
            [&] {
                (void)firebreak::core::load_landscape_weight_map_csv(path, {1});
            },
            message);
    }
    fs::remove(path);
}

void test_missing_and_extra_required_ids() {
    const auto map = make_nonuniform_map({
        {1, 1.0, 1.0, 0},
        {2, 2.0, 2.0, 0},
    });
    expect_throw(
        [&] {
            firebreak::core::validate_landscape_weight_map(map, {1, 2, 3});
        },
        "missing required original cell ID 3");
    expect_throw(
        [&] {
            firebreak::core::validate_landscape_weight_map(map, {1});
        },
        "unexpected original cell ID 2");
}

void test_round_trip() {
    const auto original = make_nonuniform_map({
        {3, 7.0, 1.75, 2},
        {1, 2.0, 0.5, 0},
        {8, 3.0, 0.75, 1},
    });
    const fs::path path = temp_path("firebreak_weight_roundtrip.csv");
    firebreak::core::write_landscape_weight_map_csv(original, path);
    const auto loaded = firebreak::core::load_landscape_weight_map_csv(path, {1, 3, 8});

    assert((firebreak::core::sorted_weighted_cell_ids(loaded) == std::vector<int>{1, 3, 8}));
    for (const int id : {1, 3, 8}) {
        assert_close(
            loaded.weight_by_original_cell_id.at(id),
            original.weight_by_original_cell_id.at(id));
        assert_close(
            loaded.raw_weight_by_original_cell_id.at(id),
            original.raw_weight_by_original_cell_id.at(id));
        assert(
            loaded.cluster_id_by_original_cell_id.at(id) ==
            original.cluster_id_by_original_cell_id.at(id));
    }
    assert_close(loaded.raw_mean, original.raw_mean);
    assert_close(loaded.normalized_mean, original.normalized_mean);
    assert_close(loaded.minimum_weight, original.minimum_weight);
    assert_close(loaded.maximum_weight, original.maximum_weight);
    assert_close(loaded.total_weight, original.total_weight);
    assert(loaded.deterministic_hash == original.deterministic_hash);
    fs::remove(path);
}

void test_hash_stability() {
    const auto a = make_nonuniform_map({
        {10, 1.0, 0.5, 0},
        {20, 2.0, 1.0, 1},
    });
    const auto b = make_nonuniform_map({
        {20, 2.0, 1.0, 1},
        {10, 1.0, 0.5, 0},
    });
    assert(a.deterministic_hash == b.deterministic_hash);

    auto changed = b;
    changed.weight_by_original_cell_id[20] = 1.25;
    firebreak::core::recompute_landscape_weight_map_statistics_and_hash(changed);
    assert(changed.deterministic_hash != b.deterministic_hash);
}

void test_compact_index_mapping() {
    firebreak::opt::IndexMapper mapper;
    mapper.build_from_nodes({500, 100, 300});
    const auto map = make_nonuniform_map({
        {500, 5.0, 2.5, 1},
        {100, 1.0, 0.5, 0},
        {300, 3.0, 1.5, 0},
    });
    const auto weights = firebreak::core::build_compact_weight_vector(map, mapper);
    assert(weights.size() == 3);
    assert(mapper.to_node(0) == 100);
    assert(mapper.to_node(1) == 300);
    assert(mapper.to_node(2) == 500);
    assert_close(weights[0], 0.5);
    assert_close(weights[1], 1.5);
    assert_close(weights[2], 2.5);

    const auto missing = make_nonuniform_map({
        {100, 1.0, 0.5, 0},
        {300, 3.0, 1.5, 0},
    });
    expect_throw(
        [&] {
            (void)firebreak::core::build_compact_weight_vector(missing, mapper);
        },
        "missing compact node");
}

void test_malformed_csv_inputs() {
    const fs::path path = temp_path("firebreak_malformed_weights.csv");
    write_text(path, "cell_id,raw_weight,cluster_id\n1,1.0,0\n");
    expect_throw(
        [&] {
            (void)firebreak::core::load_landscape_weight_map_csv(path);
        },
        "missing required column 'normalized_weight'");

    write_text(path, "cell_id,raw_weight,normalized_weight,cluster_id\n1,1.0,1.0,-1\n");
    expect_throw(
        [&] {
            (void)firebreak::core::load_landscape_weight_map_csv(path);
        },
        "cluster IDs must be nonnegative");

    write_text(path, "cell_id,raw_weight,normalized_weight,cluster_id\n1,1.0,1.0,0\n1,1.0,1.0,0\n");
    expect_throw(
        [&] {
            (void)firebreak::core::load_landscape_weight_map_csv(path);
        },
        "Duplicate landscape weight record");
    fs::remove(path);
}

void test_optimization_instance_builder_defaults_to_homogeneous_weights() {
    firebreak::core::Instance instance;
    instance.landscape_name = "synthetic";
    instance.n_cells = 6;
    instance.has_forest_size = true;
    instance.available_nodes_known = true;
    instance.available_nodes = {10, 20, 30, 40, 50, 60};

    firebreak::core::Scenario scenario;
    scenario.scenario_id = 1;
    scenario.ignition_node = 10;
    scenario.propagation_graph.add_edge(10, 30);
    scenario.propagation_graph.add_edge(30, 60);
    instance.scenarios.push_back(scenario);

    firebreak::opt::OptimizationInstanceBuilder builder;
    const auto opt = builder.build(instance, 0.5, false);
    assert(opt.cell_weight_map.profile == "homogeneous");
    assert(opt.compact_cell_weights.size() == opt.num_mapped_nodes());
    for (const double weight : opt.compact_cell_weights) {
        assert_close(weight, 1.0);
    }
    firebreak::core::validate_landscape_weight_map(
        opt.cell_weight_map,
        opt.node_mapper.original_nodes());
}

}  // namespace

int main() {
    test_homogeneous_construction();
    test_duplicate_input_ids();
    test_invalid_weights_on_load();
    test_missing_and_extra_required_ids();
    test_round_trip();
    test_hash_stability();
    test_compact_index_mapping();
    test_malformed_csv_inputs();
    test_optimization_instance_builder_defaults_to_homogeneous_weights();
    std::cout << "All landscape weight map tests passed.\n";
    return 0;
}
