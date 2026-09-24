#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/RestrictedCandidateCutPool.hpp"
#include "benders/RestrictedCandidateManager.hpp"

namespace {

firebreak::benders::BendersCut make_cut() {
    firebreak::benders::BendersCut cut;
    cut.scenario_id = 3;
    cut.rhs_constant = 12.0;
    cut.coefficients_by_compact_index = {
        {100, -2.0},
        {101, -7.5},
        {102, -13.0},
    };
    return cut;
}

double coefficient_for(const firebreak::benders::BendersCut& cut, int compact_index) {
    for (const auto& [node, coefficient] : cut.coefficients_by_compact_index) {
        if (node == compact_index) {
            return coefficient;
        }
    }
    return 0.0;
}

void test_cut_survives_deactivation_and_reactivation() {
    firebreak::benders::RestrictedCandidateManager manager(3, 1, {0, 1});
    firebreak::benders::RestrictedCandidateCutPool pool;
    pool.setWeightMapHash("fnv1a64:weighted-map");
    const auto cut = make_cut();
    assert(pool.addCut(cut, 0, "restricted", manager.activeCount()));

    const auto deactivated = manager.deactivateCandidates({1});
    assert((deactivated == std::vector<int>{1}));
    assert(pool.records()[0].cut.coefficients_by_compact_index.size() == 3);
    assert(std::fabs(coefficient_for(pool.records()[0].cut, 101) + 7.5) <= 1.0e-12);

    const auto reactivated = manager.activateCandidates({1});
    assert((reactivated == std::vector<int>{1}));
    const auto cuts = pool.cuts();
    assert(cuts.size() == 1);
    assert(std::fabs(coefficient_for(cuts[0], 101) + 7.5) <= 1.0e-12);
}

void test_cut_generated_before_activation_has_future_coefficient() {
    firebreak::benders::RestrictedCandidateManager manager(3, 1, {0});
    firebreak::benders::RestrictedCandidateCutPool pool;
    pool.setWeightMapHash("fnv1a64:weighted-map");
    assert(pool.addCut(make_cut(), 0, "restricted", manager.activeCount()));
    assert(manager.isInactive(2));

    const auto activated = manager.activateCandidates({2});
    assert((activated == std::vector<int>{2}));
    const auto cuts = pool.cuts();
    assert(std::fabs(coefficient_for(cuts[0], 102) + 13.0) <= 1.0e-12);
}

}  // namespace

int main() {
    test_cut_survives_deactivation_and_reactivation();
    test_cut_generated_before_activation_has_future_coefficient();
    std::cout << "All weighted restricted cut reactivation tests passed.\n";
    return 0;
}
