#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "core/CanonicalLandscape.hpp"
#include "core/LandscapeWeightGenerator.hpp"
#include "experiments/WeightMapRegistry.hpp"

namespace {

namespace fs = std::filesystem;

template <typename Fn>
void expect_throw(Fn&& fn, const std::string& fragment) {
    bool threw = false;
    try {
        fn();
    } catch (const std::runtime_error& exc) {
        threw = true;
        const std::string message = exc.what();
        assert(message.find(fragment) != std::string::npos);
    }
    assert(threw);
}

firebreak::core::LandscapeCellUniverse make_universe(int rows, int cols) {
    firebreak::core::LandscapeCellUniverse universe;
    universe.source = "test";
    universe.rows = rows;
    universe.cols = cols;
    for (int cell_id = 1; cell_id <= rows * cols; ++cell_id) {
        const int zero_based = cell_id - 1;
        universe.cells.push_back(firebreak::core::WeightedLandscapeCell{
            cell_id, zero_based / cols + 1, zero_based % cols + 1});
    }
    return universe;
}

fs::path fresh_root(const std::string& name) {
    const char* base = std::getenv("FIREBREAK_TEST_TMP");
    fs::path root = base ? fs::path(base) : fs::temp_directory_path();
    root /= "phase8a_registry";
    root /= name;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    return root;
}

firebreak::experiments::WeightMapRegistryRequest make_request(
    const fs::path& root,
    const firebreak::core::CanonicalLandscapeIdentity& identity,
    const firebreak::core::LandscapeCellUniverse& universe,
    const std::string& profile,
    int replicate) {
    firebreak::experiments::WeightMapRegistryRequest request;
    request.registry_root = root;
    request.identity = identity;
    request.universe = universe;
    request.weight_profile = profile;
    request.weight_replicate = replicate;
    request.global_weight_seed = 4242;
    request.config.profile = profile;
    request.config.heterogeneous_min = 0.5;
    request.config.heterogeneous_max = 1.5;
    request.allow_generate = true;
    return request;
}

void test_homogeneous_idempotence_and_worker_load() {
    const auto universe = make_universe(6, 6);
    const auto identity = firebreak::core::make_canonical_landscape_identity("new6x6", universe);
    const auto root = fresh_root("homogeneous");

    firebreak::experiments::WeightMapRegistry registry;
    const auto first =
        registry.ensure(make_request(root, identity, universe, "homogeneous", 0));
    assert(fs::exists(root / first.weight_map_path));
    assert(fs::exists(root / identity.canonical_landscape_id / "homogeneous" /
                      "replicate_0" / "metadata.json"));
    assert(first.cell_count == 36);
    assert(!first.weight_map_hash.empty());
    assert(first.normalization_mode == "raw" || first.normalization_mode == "mean_one");

    // Idempotent: re-ensure returns identical path and hash without error.
    const auto second =
        registry.ensure(make_request(root, identity, universe, "homogeneous", 0));
    assert(second.weight_map_path == first.weight_map_path);
    assert(second.weight_map_hash == first.weight_map_hash);
    assert(second.weight_generation_seed == first.weight_generation_seed);

    // Read-only worker load returns the same entry.
    const auto loaded =
        registry.load(root, identity.canonical_landscape_id, "homogeneous", 0);
    assert(loaded.weight_map_hash == first.weight_map_hash);
    assert(loaded.source_universe_hash == identity.universe_hash);

    // Worker load of a non-existent entry fails (never regenerates).
    expect_throw(
        [&] {
            registry.load(root, identity.canonical_landscape_id, "homogeneous", 7);
        },
        "not found");
}

void test_worker_contract_no_regeneration() {
    const auto universe = make_universe(5, 5);
    const auto identity = firebreak::core::make_canonical_landscape_identity("new5x5", universe);
    const auto root = fresh_root("worker_contract");
    firebreak::experiments::WeightMapRegistry registry;

    auto request = make_request(root, identity, universe, "heterogeneous", 0);
    request.allow_generate = false;  // worker contract: missing map must fail, not generate
    expect_throw([&] { registry.ensure(request); }, "generation is not allowed");
    assert(!fs::exists(root / identity.canonical_landscape_id));
}

void test_parameter_mismatch_rejected() {
    const auto universe = make_universe(5, 5);
    const auto identity = firebreak::core::make_canonical_landscape_identity("new5x5", universe);
    const auto root = fresh_root("mismatch");
    firebreak::experiments::WeightMapRegistry registry;

    registry.ensure(make_request(root, identity, universe, "heterogeneous", 0));

    // Same logical key, different generation parameters -> hard error, no overwrite.
    auto changed = make_request(root, identity, universe, "heterogeneous", 0);
    changed.config.heterogeneous_max = 3.0;
    expect_throw([&] { registry.ensure(changed); }, "does not match the requested");
}

void test_distinct_replicates_distinct_hashes() {
    const auto universe = make_universe(5, 5);
    const auto identity = firebreak::core::make_canonical_landscape_identity("new5x5", universe);
    const auto root = fresh_root("replicates");
    firebreak::experiments::WeightMapRegistry registry;

    const auto r0 = registry.ensure(make_request(root, identity, universe, "heterogeneous", 0));
    const auto r1 = registry.ensure(make_request(root, identity, universe, "heterogeneous", 1));
    assert(r0.weight_generation_seed != r1.weight_generation_seed);
    assert(r0.weight_map_hash != r1.weight_map_hash);
    assert(r0.weight_map_path != r1.weight_map_path);
}

void test_clustered_entry_and_load_weight_map() {
    const auto universe = make_universe(8, 8);
    const auto identity = firebreak::core::make_canonical_landscape_identity("new8x8", universe);
    const auto root = fresh_root("clustered");
    firebreak::experiments::WeightMapRegistry registry;

    auto request = make_request(root, identity, universe, "clustered", 0);
    request.config.cluster_count = 2;
    request.config.cluster_fraction = 0.25;
    request.config.background_min = 0.5;
    request.config.background_max = 1.0;
    request.config.cluster_min = 2.0;
    request.config.cluster_max = 4.0;
    const auto entry = registry.ensure(request);
    assert(entry.cluster_count == 2);
    assert(entry.cluster_multiplier == 4.0);
    assert(entry.background_multiplier == 1.0);

    const auto map = registry.load_weight_map(root, entry);
    assert(map.deterministic_hash == entry.weight_map_hash);
    assert(static_cast<int>(map.weight_by_original_cell_id.size()) == 64);
}

}  // namespace

int main() {
    test_homogeneous_idempotence_and_worker_load();
    test_worker_contract_no_regeneration();
    test_parameter_mismatch_rejected();
    test_distinct_replicates_distinct_hashes();
    test_clustered_entry_and_load_weight_map();
    std::cout << "All weight-map registry tests passed.\n";
    return 0;
}
