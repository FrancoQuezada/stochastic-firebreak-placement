#include <cassert>
#include <iostream>
#include <map>
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

void test_cut_pool_stores_full_space_cuts() {
    firebreak::benders::RestrictedCandidateCutPool pool;
    const auto cut = make_cut(4, 3.0, {{10, -1.0}, {11, 0.0}, {12, -2.0}});

    assert(pool.addCut(cut, 0, "restricted", 2));
    assert(pool.size() == 1);
    assert(!pool.empty());

    const auto& records = pool.records();
    assert(records[0].pool_index == 0);
    assert(records[0].scenario_id == 4);
    assert(records[0].round_index == 0);
    assert(records[0].stage_name == "restricted");
    assert(records[0].active_candidate_count == 2);
    assert(records[0].cut.coefficients_by_compact_index.size() == 3);
}

void test_preserves_coefficients_for_inactive_variables() {
    firebreak::benders::RestrictedCandidateCutPool pool;
    const auto cut = make_cut(1, 2.0, {{0, -3.0}, {1, 1.0}, {2, -0.25}});
    pool.addCut(cut, 0, "restricted", 1);

    const auto cuts = pool.cuts();
    assert(cuts.size() == 1);
    assert((cuts[0].coefficients_by_compact_index == std::vector<std::pair<int, double>>{
        {0, -3.0},
        {1, 1.0},
        {2, -0.25},
    }));
}

void test_duplicate_behavior() {
    firebreak::benders::RestrictedCandidateCutPool pool;
    const auto cut = make_cut(1, 2.0, {{2, -0.25}, {0, -3.0}});
    const auto duplicate_with_reordered_coefficients =
        make_cut(1, 2.0, {{0, -3.0}, {2, -0.25}});

    assert(pool.addCut(cut, 0, "restricted", 1));
    assert(!pool.addCut(duplicate_with_reordered_coefficients, 1, "full", 3));
    assert(pool.size() == 1);
    assert(pool.duplicateCutsSkipped() == 1);
}

void test_distinct_cuts_and_counts() {
    firebreak::benders::RestrictedCandidateCutPool pool;
    pool.addCut(make_cut(1, 2.0, {{0, -3.0}}), 0, "restricted", 1);
    pool.addCut(make_cut(1, 2.5, {{0, -3.0}}), 1, "burn_frequency_round_1", 2);
    pool.addCut(make_cut(2, 2.0, {{0, -3.0}}), 1, "burn_frequency_round_1", 2);

    assert(pool.size() == 3);
    assert((pool.cutsByRound() == std::map<int, int>{{0, 1}, {1, 2}}));
    assert((pool.cutsByScenario() == std::map<int, int>{{1, 2}, {2, 1}}));
}

}  // namespace

int main() {
    test_cut_pool_stores_full_space_cuts();
    test_preserves_coefficients_for_inactive_variables();
    test_duplicate_behavior();
    test_distinct_cuts_and_counts();

    std::cout << "All restricted candidate cut pool tests passed.\n";
    return 0;
}
