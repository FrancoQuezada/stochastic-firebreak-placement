#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "core/LandscapeWeightMap.hpp"
#include "experiments/PairedInstanceWeightMapping.hpp"
#include "opt/IndexMapper.hpp"

namespace {

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

// Canonical map over original IDs 1..6, each cell weighted by cell_id (positive, finite).
firebreak::core::LandscapeWeightMap make_map(const std::vector<int>& ids) {
    std::vector<firebreak::core::LandscapeWeightRecord> records;
    for (const int id : ids) {
        const double w = static_cast<double>(id);
        records.push_back(firebreak::core::LandscapeWeightRecord{id, w, w, 0});
    }
    return firebreak::core::make_landscape_weight_map("test", 0, false, records);
}

firebreak::opt::IndexMapper mapper_for(const std::vector<int>& nodes) {
    firebreak::opt::IndexMapper mapper;
    mapper.build_from_nodes(nodes);
    return mapper;
}

void test_reduced_mapping_matches_original_ids() {
    const auto map = make_map({1, 2, 3, 4, 5, 6});
    const auto reduced = mapper_for({1, 2, 3, 4});
    const auto result = firebreak::experiments::map_weight_map_to_instance(map, reduced);
    assert(result.mapped_count == 4);
    assert(result.missing_count == 0);
    assert(result.mapping_method == "original_cell_id");
    // Compact index i corresponds to sorted node i+1 with weight i+1.
    for (int i = 0; i < reduced.size(); ++i) {
        assert(std::fabs(result.compact_weights[static_cast<std::size_t>(i)] -
                         static_cast<double>(reduced.to_node(i))) < 1e-12);
    }
}

void test_reburn_shared_equality_and_reburn_only_cells() {
    const auto map = make_map({1, 2, 3, 4, 5, 6});
    const std::vector<int> reduced_ids = {1, 2, 3, 4};
    const std::vector<int> reburn_ids = {1, 2, 3, 4, 5, 6};  // 5,6 are reburn-only
    const auto report =
        firebreak::experiments::compare_reduced_reburn(map, reduced_ids, reburn_ids);
    assert(report.canonical_cell_count == 6);
    assert(report.reduced_cell_count == 4);
    assert(report.reburn_cell_count == 6);
    assert(report.shared_cell_count == 4);
    assert(report.reduced_mapped_count == 4);
    assert(report.reburn_mapped_count == 6);  // reburn-only cells 5,6 have canonical weights
    assert(report.reduced_missing_count == 0);
    assert(report.reburn_missing_count == 0);
    assert(report.duplicate_original_id_count == 0);
    assert(report.shared_weight_mismatch_count == 0);  // mandatory invariant
}

void test_missing_id_fails() {
    const auto map = make_map({1, 2, 3});
    const auto mapper = mapper_for({1, 2, 4});  // 4 absent from map
    expect_throw(
        [&] { firebreak::experiments::map_weight_map_to_instance(map, mapper); },
        "missing");
}

void test_duplicate_id_fails() {
    const auto map = make_map({1, 2, 3, 4});
    // A raw list with a duplicate original ID is rejected.
    expect_throw(
        [&] {
            firebreak::experiments::compare_reduced_reburn(map, {1, 2, 2}, {1, 2, 3});
        },
        "duplicate");
}

void test_coordinate_fallback_exact_and_ambiguous() {
    const auto map = make_map({1, 2, 3, 4});
    // Distinct coordinates per original ID.
    const std::vector<std::pair<int, std::pair<int, int>>> coords = {
        {1, {1, 1}}, {2, {1, 2}}, {3, {2, 1}}, {4, {2, 2}}};
    const std::vector<std::pair<int, std::pair<int, int>>> instance_cells = {
        {0, {1, 1}}, {1, {1, 2}}, {2, {2, 1}}, {3, {2, 2}}};
    const auto result = firebreak::experiments::map_weight_map_to_instance_by_coordinate(
        map, coords, instance_cells);
    assert(result.mapping_method == "coordinate");
    assert(result.mapped_count == 4);
    assert(std::fabs(result.compact_weights[0] - 1.0) < 1e-12);
    assert(std::fabs(result.compact_weights[3] - 4.0) < 1e-12);

    // Ambiguous coordinate table (two IDs share a coordinate) is rejected.
    const std::vector<std::pair<int, std::pair<int, int>>> ambiguous = {
        {1, {1, 1}}, {2, {1, 1}}, {3, {2, 1}}, {4, {2, 2}}};
    expect_throw(
        [&] {
            firebreak::experiments::map_weight_map_to_instance_by_coordinate(
                map, ambiguous, instance_cells);
        },
        "duplicate coordinate");

    // A missing instance coordinate is rejected.
    const std::vector<std::pair<int, std::pair<int, int>>> missing_cell = {
        {0, {9, 9}}, {1, {1, 2}}, {2, {2, 1}}, {3, {2, 2}}};
    expect_throw(
        [&] {
            firebreak::experiments::map_weight_map_to_instance_by_coordinate(
                map, coords, missing_cell);
        },
        "could not map");
}

}  // namespace

int main() {
    test_reduced_mapping_matches_original_ids();
    test_reburn_shared_equality_and_reburn_only_cells();
    test_missing_id_fails();
    test_duplicate_id_fails();
    test_coordinate_fallback_exact_and_ambiguous();
    std::cout << "All paired instance mapping tests passed.\n";
    return 0;
}
