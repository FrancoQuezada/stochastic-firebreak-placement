#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/Instance.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "core/Scenario.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/FppWeightedLossUtils.hpp"

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

fs::path scratch_dir() {
    const char* base = std::getenv("FIREBREAK_TEST_TMP");
    fs::path root = base ? fs::path(base) : fs::temp_directory_path();
    root /= "phase8b_subset_attachment";
    fs::create_directories(root);
    return root;
}

// Synthetic instance whose compact universe is exactly {10,20,30,40,50,60} (all six
// original IDs are eligible, matching Phase 8A's synthetic-instance test convention).
firebreak::opt::OptimizationInstance make_instance() {
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
    return builder.build(instance, 0.5, false);
}

firebreak::core::LandscapeWeightMap make_map(const std::vector<int>& ids) {
    std::vector<firebreak::core::LandscapeWeightRecord> records;
    records.reserve(ids.size());
    for (const int id : ids) {
        const double weight = static_cast<double>(id) / 10.0;
        records.push_back(firebreak::core::LandscapeWeightRecord{id, weight, weight, 0});
    }
    return firebreak::core::make_landscape_weight_map("test", 0, false, records);
}

void test_superset_canonical_map_attaches_successfully() {
    // Canonical map covers 8 physical cells; the instance's compact universe (6 cells)
    // is a strict subset of it.
    const auto map = make_map({10, 20, 30, 40, 50, 60, 70, 80});
    const auto csv_path = scratch_dir() / "superset_weights.csv";
    firebreak::core::write_landscape_weight_map_csv(map, csv_path);

    auto opt = make_instance();
    firebreak::solver::WeightMapAttachmentDiagnostics diagnostics;
    firebreak::solver::attach_weight_map_to_optimization_instance(opt, csv_path, "", &diagnostics);

    assert(opt.compact_cell_weights.size() == 6);
    assert(std::fabs(opt.compact_cell_weights[0] - 1.0) < 1e-9);  // node 10 -> weight 1.0

    assert(diagnostics.canonical_cell_count == 8);
    assert(diagnostics.instance_cell_count == 6);
    assert(diagnostics.mapped_instance_cell_count == 6);
    assert(diagnostics.missing_instance_cell_count == 0);
    assert(diagnostics.unused_canonical_cell_count == 2);
    assert(diagnostics.duplicate_instance_original_id_count == 0);
    assert(diagnostics.mapping_method == "original_cell_id");
}

void test_missing_instance_cell_rejected() {
    // Canonical map is missing original ID 60, which IS part of the instance's compact
    // universe: this must be a hard error, not a silent drop.
    const auto map = make_map({10, 20, 30, 40, 50});
    const auto csv_path = scratch_dir() / "missing_cell_weights.csv";
    firebreak::core::write_landscape_weight_map_csv(map, csv_path);

    auto opt = make_instance();
    expect_throw(
        [&] {
            firebreak::solver::attach_weight_map_to_optimization_instance(opt, csv_path);
        },
        "missing");
}

void test_hash_mismatch_rejected() {
    const auto map = make_map({10, 20, 30, 40, 50, 60});
    const auto csv_path = scratch_dir() / "hash_check_weights.csv";
    firebreak::core::write_landscape_weight_map_csv(map, csv_path);

    auto opt = make_instance();
    expect_throw(
        [&] {
            firebreak::solver::attach_weight_map_to_optimization_instance(
                opt, csv_path, "fnv1a64:0000000000000000");
        },
        "hash mismatch");

    // The correct hash succeeds.
    auto opt2 = make_instance();
    firebreak::solver::attach_weight_map_to_optimization_instance(
        opt2, csv_path, map.deterministic_hash);
    assert(opt2.compact_cell_weights.size() == 6);
}

void test_empty_weight_map_file_is_a_noop() {
    auto opt = make_instance();
    firebreak::solver::WeightMapAttachmentDiagnostics diagnostics;
    firebreak::solver::attach_weight_map_to_optimization_instance(opt, "", "", &diagnostics);
    assert(diagnostics.canonical_cell_count == 0);
    assert(opt.cell_weight_map.profile == "homogeneous");
}

}  // namespace

int main() {
    test_superset_canonical_map_attaches_successfully();
    test_missing_instance_cell_rejected();
    test_hash_mismatch_rejected();
    test_empty_weight_map_file_is_a_noop();
    std::cout << "All subset-tolerant weight map attachment tests passed.\n";
    return 0;
}
