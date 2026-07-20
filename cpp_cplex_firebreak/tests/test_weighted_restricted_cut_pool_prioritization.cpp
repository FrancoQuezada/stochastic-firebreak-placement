#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "benders/RestrictedCandidateCutPool.hpp"

namespace {

firebreak::benders::BendersCut make_cut(
    int scenario_id,
    double rhs,
    std::vector<std::pair<int, double>> coefficients) {
    firebreak::benders::BendersCut cut;
    cut.scenario_id = scenario_id;
    cut.rhs_constant = rhs;
    cut.coefficients_by_compact_index = std::move(coefficients);
    return cut;
}

template <typename Fn>
void assert_throws(Fn fn, const std::string& label) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        std::cerr << "Expected exception was not thrown: " << label << "\n";
    }
    assert(threw);
}

void test_weighted_coefficients_are_part_of_identity() {
    firebreak::benders::RestrictedCandidateCutPool pool;
    pool.setWeightMapHash("fnv1a64:weighted-map");

    const auto low_weight_cut =
        make_cut(7, 3.0, {{10, -1.0}, {11, -2.0}});
    const auto high_weight_cut_same_support =
        make_cut(7, 3.0, {{11, -20.0}, {10, -10.0}});
    const auto duplicate_reordered =
        make_cut(7, 3.0, {{11, -2.0}, {10, -1.0}});

    assert(pool.addCut(low_weight_cut, 0, "restricted", 1));
    assert(pool.addCut(high_weight_cut_same_support, 1, "maintenance", 2));
    assert(!pool.addCut(duplicate_reordered, 2, "full", 2));

    assert(pool.size() == 2);
    assert(pool.duplicateCutsSkipped() == 1);
    assert(pool.peakSize() == 2);
    assert(pool.evictions() == 0);
    assert(pool.reinstantiations() == 0);
    assert(pool.records()[0].weight_map_hash == "fnv1a64:weighted-map");
    assert(pool.records()[1].weight_map_hash == "fnv1a64:weighted-map");
}

void test_hash_mismatch_rejected_after_records() {
    firebreak::benders::RestrictedCandidateCutPool pool;
    pool.setWeightMapHash("hash-a");
    assert(pool.addCut(make_cut(1, 1.0, {{0, -1.0}}), 0, "restricted", 1));

    assert_throws(
        [&] { pool.setWeightMapHash("hash-b"); },
        "cut pool hash mismatch");
}

}  // namespace

int main() {
    test_weighted_coefficients_are_part_of_identity();
    test_hash_mismatch_rejected_after_records();
    std::cout << "All weighted restricted cut-pool prioritization tests passed.\n";
    return 0;
}
