#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "benders/RestrictedCandidateManager.hpp"

namespace {

using firebreak::benders::RestrictedCandidateManager;
using firebreak::benders::makeInitialActiveSetFromList;
using firebreak::benders::makeInitialActiveSetFromScores;

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-12);
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

void test_valid_construction() {
    const RestrictedCandidateManager manager(6, 2, {4, 1, 3});
    assert(manager.candidateCount() == 6);
    assert(manager.budget() == 2);
    assert(manager.activeCount() == 3);
    assert(manager.inactiveCount() == 3);
    assert((manager.activeCandidates() == std::vector<int>{1, 3, 4}));
    assert((manager.inactiveCandidates() == std::vector<int>{0, 2, 5}));
}

void test_invalid_n() {
    assert_throws(
        [] {
            RestrictedCandidateManager manager(0, 0, {});
            (void)manager;
        },
        "candidate_count <= 0");
}

void test_invalid_budget() {
    assert_throws(
        [] {
            RestrictedCandidateManager manager(5, -1, {});
            (void)manager;
        },
        "budget < 0");
    assert_throws(
        [] {
            RestrictedCandidateManager manager(5, 6, {0, 1, 2, 3, 4});
            (void)manager;
        },
        "budget > n");
}

void test_initial_active_smaller_than_budget_fails() {
    assert_throws(
        [] {
            RestrictedCandidateManager manager(5, 3, {0, 1});
            (void)manager;
        },
        "initial active size < budget");
}

void test_duplicate_ids_fail() {
    assert_throws(
        [] {
            RestrictedCandidateManager manager(5, 2, {0, 1, 1});
            (void)manager;
        },
        "duplicate initial ids");
}

void test_out_of_range_ids_fail() {
    assert_throws(
        [] {
            RestrictedCandidateManager manager(5, 1, {0, 5});
            (void)manager;
        },
        "out-of-range initial id");
    assert_throws(
        [] {
            RestrictedCandidateManager manager(5, 1, {-1});
            (void)manager;
        },
        "negative initial id");
}

void test_zero_budget_allows_empty_active_set() {
    const RestrictedCandidateManager manager(5, 0, {});
    assert(manager.candidateCount() == 5);
    assert(manager.budget() == 0);
    assert(manager.activeCount() == 0);
    assert(manager.inactiveCount() == 5);
    assert(manager.activeCandidates().empty());
    assert((manager.inactiveCandidates() == std::vector<int>{0, 1, 2, 3, 4}));
}

void test_active_inactive_counts() {
    const RestrictedCandidateManager manager(8, 2, {0, 2, 6});
    assert(manager.activeCount() == 3);
    assert(manager.inactiveCount() == 5);
    assert(manager.isActive(2));
    assert(!manager.isActive(5));
    assert(manager.isInactive(5));
    assert(!manager.isInactive(6));
}

void test_active_inactive_sorted_lists() {
    const RestrictedCandidateManager manager(7, 2, {6, 2, 4});
    assert((manager.activeCandidates() == std::vector<int>{2, 4, 6}));
    assert((manager.inactiveCandidates() == std::vector<int>{0, 1, 3, 5}));
}

void test_upper_bounds_correctness() {
    const RestrictedCandidateManager manager(5, 2, {1, 3});
    assert((manager.activeMaskAsIntVector() == std::vector<int>{0, 1, 0, 1, 0}));
    assert((manager.upperBounds() == std::vector<double>{0.0, 1.0, 0.0, 1.0, 0.0}));
}

void test_contains_all_selected() {
    const RestrictedCandidateManager manager(6, 2, {1, 3, 5});
    assert(manager.containsAllSelected({1, 5}));
    assert(manager.containsAllSelected({}));
    assert(!manager.containsAllSelected({1, 2}));
}

void test_activate_candidates_activates_only_inactive() {
    RestrictedCandidateManager manager(6, 2, {0, 2});
    const auto activated = manager.activateCandidates({2, 4, 5});
    assert((activated == std::vector<int>{4, 5}));
    assert((manager.activeCandidates() == std::vector<int>{0, 2, 4, 5}));
    assert((manager.inactiveCandidates() == std::vector<int>{1, 3}));
}

void test_activate_candidates_ignores_already_active_candidates() {
    RestrictedCandidateManager manager(5, 2, {1, 3});
    const auto activated = manager.activateCandidates({1, 1, 3});
    assert(activated.empty());
    assert((manager.activeCandidates() == std::vector<int>{1, 3}));
    assert(manager.activationHistory().empty());
}

void test_activate_top_k_ranking_by_score_descending() {
    RestrictedCandidateManager manager(6, 2, {0, 5});
    const auto activated = manager.activateTopK(
        {
            {1, 3.0},
            {2, 8.0},
            {3, 4.0},
            {4, 7.0},
            {5, 100.0},
        },
        2);
    assert((activated == std::vector<int>{2, 4}));
    assert((manager.activeCandidates() == std::vector<int>{0, 2, 4, 5}));
}

void test_activate_top_k_deterministic_tie_breaking_by_id() {
    RestrictedCandidateManager manager(7, 2, {0, 6});
    const auto activated = manager.activateTopK(
        {
            {4, 10.0},
            {2, 10.0},
            {3, 9.0},
            {5, 10.0},
        },
        2);
    assert((activated == std::vector<int>{2, 4}));
}

void test_activate_top_k_rejects_nonpositive_k() {
    RestrictedCandidateManager manager(5, 2, {0, 1});
    assert_throws(
        [&manager] {
            (void)manager.activateTopK({{2, 1.0}}, 0);
        },
        "activateTopK k == 0");
    assert_throws(
        [&manager] {
            (void)manager.activateTopK({{2, 1.0}}, -1);
        },
        "activateTopK k < 0");
}

void test_activate_all() {
    RestrictedCandidateManager manager(5, 2, {1, 4});
    manager.activateAll();
    assert((manager.activeCandidates() == std::vector<int>{0, 1, 2, 3, 4}));
    assert(manager.inactiveCandidates().empty());
    assert(manager.activeCount() == 5);
}

void test_all_active() {
    RestrictedCandidateManager manager(3, 2, {0, 2});
    assert(!manager.allActive());
    manager.activateAll();
    assert(manager.allActive());
}

void test_active_fraction() {
    RestrictedCandidateManager manager(8, 2, {1, 2});
    assert_close(manager.activeFraction(), 0.25);
    manager.activateCandidates({4, 6});
    assert_close(manager.activeFraction(), 0.5);
}

void test_activation_history() {
    RestrictedCandidateManager manager(7, 2, {0, 1});
    assert(manager.activationHistory().empty());

    const auto first = manager.activateCandidates({1, 4, 5});
    assert((first == std::vector<int>{4, 5}));
    assert(manager.activationHistory().size() == 1);
    assert(manager.activationHistory()[0].round == 0);
    assert((manager.activationHistory()[0].activated == std::vector<int>{4, 5}));
    assert(manager.activationHistory()[0].activeCountAfter == 4);

    const auto empty = manager.activateCandidates({0, 1, 4});
    assert(empty.empty());
    assert(manager.activationHistory().size() == 1);

    const auto second = manager.activateTopK({{6, 5.0}, {2, 5.0}, {3, 4.0}}, 1);
    assert((second == std::vector<int>{2}));
    assert(manager.activationHistory().size() == 2);
    assert(manager.activationHistory()[1].round == 1);
    assert((manager.activationHistory()[1].activated == std::vector<int>{2}));
    assert(manager.activationHistory()[1].activeCountAfter == 5);

    manager.activateAll();
    assert(manager.activationHistory().size() == 3);
    assert(manager.activationHistory()[2].round == 2);
    assert((manager.activationHistory()[2].activated == std::vector<int>{3, 6}));
    assert(manager.activationHistory()[2].activeCountAfter == 7);

    manager.activateAll();
    assert(manager.activationHistory().size() == 3);
}

void test_deactivate_candidates_deactivates_only_active() {
    RestrictedCandidateManager manager(6, 2, {0, 1, 2, 4});
    const auto deactivated = manager.deactivateCandidates({1, 3, 4});
    assert((deactivated == std::vector<int>{1, 4}));
    assert((manager.activeCandidates() == std::vector<int>{0, 2}));
    assert((manager.inactiveCandidates() == std::vector<int>{1, 3, 4, 5}));
    assert(manager.deactivationHistory().size() == 1);
    assert((manager.deactivationHistory()[0].deactivated == std::vector<int>{1, 4}));
    assert(manager.deactivationHistory()[0].activeCountAfter == 2);
}

void test_deactivate_rejects_invalid_and_budget_violation() {
    RestrictedCandidateManager manager(5, 2, {0, 1, 2});
    assert_throws(
        [&manager] {
            (void)manager.deactivateCandidates({5});
        },
        "deactivate invalid candidate");
    assert_throws(
        [&manager] {
            (void)manager.deactivateCandidates({0, 1});
        },
        "deactivate below budget");
}

void test_deactivate_bottom_k_uses_lowest_scores_and_zero_missing() {
    RestrictedCandidateManager manager(6, 1, {0, 1, 2, 3, 4});
    const auto deactivated = manager.deactivateBottomK(
        {
            {0, 5.0},
            {1, -2.0},
            {2, 3.0},
        },
        3,
        {1});
    assert((deactivated == std::vector<int>{2, 3, 4}));
    assert((manager.activeCandidates() == std::vector<int>{0, 1}));
}

void test_deactivate_bottom_k_tie_breaks_by_id() {
    RestrictedCandidateManager manager(5, 1, {0, 1, 2, 3});
    const auto deactivated = manager.deactivateBottomK(
        {
            {3, 0.0},
            {1, 0.0},
            {2, 0.0},
        },
        2,
        {0});
    assert((deactivated == std::vector<int>{1, 2}));
}

void test_explicit_list_helper_sorts_and_validates() {
    const auto active = makeInitialActiveSetFromList(6, 2, {5, 1, 3});
    assert((active == std::vector<int>{1, 3, 5}));

    assert_throws(
        [] {
            (void)makeInitialActiveSetFromList(6, 2, {1, 1, 3});
        },
        "explicit helper duplicate");
    assert_throws(
        [] {
            (void)makeInitialActiveSetFromList(6, 2, {1, 6});
        },
        "explicit helper out of range");
}

void test_top_score_helper_chooses_correct_candidates() {
    const auto active = makeInitialActiveSetFromScores(
        6,
        2,
        {
            {0, 1.0},
            {1, 8.0},
            {2, 3.0},
            {3, 5.0},
            {4, 9.0},
        },
        3);
    assert((active == std::vector<int>{1, 3, 4}));
}

void test_top_score_helper_tie_breaking() {
    const auto active = makeInitialActiveSetFromScores(
        5,
        2,
        {
            {3, 10.0},
            {2, 10.0},
            {1, 9.0},
            {4, 1.0},
        },
        2);
    assert((active == std::vector<int>{2, 3}));
}

void test_top_score_helper_fails_when_initial_size_smaller_than_budget() {
    assert_throws(
        [] {
            (void)makeInitialActiveSetFromScores(5, 3, {{0, 10.0}, {1, 9.0}, {2, 8.0}}, 2);
        },
        "top-score helper initial size < budget");
}

void test_top_score_helper_fails_when_not_enough_scored_candidates_exist() {
    assert_throws(
        [] {
            (void)makeInitialActiveSetFromScores(5, 2, {{0, 10.0}, {1, 9.0}}, 3);
        },
        "top-score helper not enough scored candidates");
}

}  // namespace

int main() {
    test_valid_construction();
    test_invalid_n();
    test_invalid_budget();
    test_initial_active_smaller_than_budget_fails();
    test_duplicate_ids_fail();
    test_out_of_range_ids_fail();
    test_zero_budget_allows_empty_active_set();
    test_active_inactive_counts();
    test_active_inactive_sorted_lists();
    test_upper_bounds_correctness();
    test_contains_all_selected();
    test_activate_candidates_activates_only_inactive();
    test_activate_candidates_ignores_already_active_candidates();
    test_activate_top_k_ranking_by_score_descending();
    test_activate_top_k_deterministic_tie_breaking_by_id();
    test_activate_top_k_rejects_nonpositive_k();
    test_activate_all();
    test_all_active();
    test_active_fraction();
    test_activation_history();
    test_deactivate_candidates_deactivates_only_active();
    test_deactivate_rejects_invalid_and_budget_violation();
    test_deactivate_bottom_k_uses_lowest_scores_and_zero_missing();
    test_deactivate_bottom_k_tie_breaks_by_id();
    test_explicit_list_helper_sorts_and_validates();
    test_top_score_helper_chooses_correct_candidates();
    test_top_score_helper_tie_breaking();
    test_top_score_helper_fails_when_initial_size_smaller_than_budget();
    test_top_score_helper_fails_when_not_enough_scored_candidates_exist();

    std::cout << "All restricted candidate manager tests passed.\n";
    return 0;
}
