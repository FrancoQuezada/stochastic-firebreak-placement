#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

#include "benders/BendersCoefficientCandidateScorer.hpp"
#include "benders/RestrictedCandidateManager.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-12);
}

firebreak::benders::BendersCut cut(
    int scenario_id,
    std::vector<std::pair<int, double>> coefficients) {
    firebreak::benders::BendersCut out;
    out.scenario_id = scenario_id;
    out.coefficients_by_compact_index = std::move(coefficients);
    return out;
}

void test_weighted_coefficients_are_not_double_weighted() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreInactiveCandidates(
        2,
        {10, 20},
        {0, 1},
        {
            cut(1, {{10, -6.0}, {20, -2.0}}),
            cut(2, {{10, -2.0}, {20, -10.0}}),
        },
        std::map<int, double>{{1, 0.25}, {2, 0.75}},
        "weighted-hash");

    assert(summary.weighted);
    assert(summary.weight_map_hash == "weighted-hash");
    assert(summary.cuts_used == 2);
    assert(summary.scores[0].first == 1);
    assert_close(summary.scores[0].second, 8.0);
    assert(summary.scores[1].first == 0);
    assert_close(summary.scores[1].second, 3.0);
}

void test_weighted_and_homogeneous_cut_rankings_can_differ() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto homogeneous = scorer.scoreInactiveCandidates(
        2,
        {10, 20},
        {0, 1},
        {cut(1, {{10, -4.0}, {20, -2.0}})});
    const auto weighted = scorer.scoreInactiveCandidates(
        2,
        {10, 20},
        {0, 1},
        {cut(1, {{10, -1.0}, {20, -8.0}})},
        {},
        "weighted-hash");

    assert(homogeneous.scores[0].first == 0);
    assert(weighted.scores[0].first == 1);
}

void test_hash_metadata_and_deterministic_activation() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    firebreak::benders::RestrictedCandidateManager manager(3, 1, {0});
    const auto summary = scorer.scoreInactiveCandidates(
        3,
        {0, 1, 2},
        manager.inactiveCandidates(),
        {cut(7, {{1, -5.0}, {2, -5.0}})},
        {},
        "hash-b");

    assert(summary.weight_map_hash == "hash-b");
    const auto activated = manager.activateTopK(summary.scores, 1);
    assert((activated == std::vector<int>{1}));
}

}  // namespace

int main() {
    test_weighted_coefficients_are_not_double_weighted();
    test_weighted_and_homogeneous_cut_rankings_can_differ();
    test_hash_metadata_and_deterministic_activation();
    std::cout << "All weighted Benders-coefficient candidate scorer tests passed.\n";
    return 0;
}
