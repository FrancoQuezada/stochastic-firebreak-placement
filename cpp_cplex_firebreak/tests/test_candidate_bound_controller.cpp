#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "benders/CandidateBoundController.hpp"
#include "benders/RestrictedCandidateManager.hpp"

namespace {

using firebreak::benders::CandidateBoundController;
using firebreak::benders::RestrictedCandidateManager;

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
    const CandidateBoundController controller(5);
    assert(controller.candidateCount() == 5);
    assert((controller.upperBounds() == std::vector<double>{0.0, 0.0, 0.0, 0.0, 0.0}));
    assert(controller.activeUpperBoundCount() == 0);
    assert(controller.inactiveUpperBoundCount() == 5);
}

void test_invalid_construction_with_nonpositive_n() {
    assert_throws(
        [] {
            CandidateBoundController controller(0);
            (void)controller;
        },
        "candidate count 0");
    assert_throws(
        [] {
            CandidateBoundController controller(-3);
            (void)controller;
        },
        "candidate count negative");
}

void test_apply_manager_state() {
    const RestrictedCandidateManager manager(6, 2, {1, 4});
    CandidateBoundController controller(6);
    controller.apply(manager);

    assert((controller.upperBounds() == std::vector<double>{0.0, 1.0, 0.0, 0.0, 1.0, 0.0}));
    assert(controller.activeUpperBoundCount() == 2);
    assert(controller.inactiveUpperBoundCount() == 4);
}

void test_active_candidates_get_upper_bound_one() {
    const RestrictedCandidateManager manager(5, 2, {0, 2, 4});
    CandidateBoundController controller(5);
    controller.apply(manager);

    for (const int candidate : manager.activeCandidates()) {
        assert(controller.upperBound(candidate) == 1.0);
    }
}

void test_inactive_candidates_get_upper_bound_zero() {
    const RestrictedCandidateManager manager(5, 2, {0, 2});
    CandidateBoundController controller(5);
    controller.apply(manager);

    for (const int candidate : manager.inactiveCandidates()) {
        assert(controller.upperBound(candidate) == 0.0);
    }
}

void test_update_after_activate_candidates() {
    RestrictedCandidateManager manager(6, 2, {0, 2});
    CandidateBoundController controller(6);
    controller.apply(manager);
    assert((controller.indicesWithUpperBoundOne() == std::vector<int>{0, 2}));

    manager.activateCandidates({3, 5});
    controller.apply(manager);
    assert((controller.indicesWithUpperBoundOne() == std::vector<int>{0, 2, 3, 5}));
    assert((controller.indicesWithUpperBoundZero() == std::vector<int>{1, 4}));
}

void test_update_after_activate_top_k() {
    RestrictedCandidateManager manager(7, 2, {0, 6});
    CandidateBoundController controller(7);

    manager.activateTopK({{4, 8.0}, {2, 8.0}, {3, 4.0}, {1, 10.0}}, 2);
    controller.apply(manager);

    assert((controller.indicesWithUpperBoundOne() == std::vector<int>{0, 1, 2, 6}));
    assert((controller.indicesWithUpperBoundZero() == std::vector<int>{3, 4, 5}));
}

void test_update_after_activate_all() {
    RestrictedCandidateManager manager(4, 2, {1, 3});
    CandidateBoundController controller(4);

    manager.activateAll();
    controller.apply(manager);

    assert((controller.upperBounds() == std::vector<double>{1.0, 1.0, 1.0, 1.0}));
    assert(controller.activeUpperBoundCount() == 4);
    assert(controller.inactiveUpperBoundCount() == 0);
}

void test_consistency_with_manager() {
    RestrictedCandidateManager manager(5, 2, {1, 3});
    CandidateBoundController controller(5);

    assert(!controller.isConsistentWith(manager));
    controller.apply(manager);
    assert(controller.isConsistentWith(manager));

    manager.activateCandidates({4});
    assert(!controller.isConsistentWith(manager));
    controller.apply(manager);
    assert(controller.isConsistentWith(manager));
}

void test_inconsistency_detection_after_different_active_mask() {
    const RestrictedCandidateManager manager(5, 2, {1, 3});
    CandidateBoundController controller(5);
    controller.apply(manager);
    assert(controller.isConsistentWith(manager));

    controller.setUpperBoundsFromActiveMask({1, 0, 0, 0, 1});
    assert(!controller.isConsistentWith(manager));
}

void test_invalid_active_mask_size_fails() {
    CandidateBoundController controller(4);
    assert_throws(
        [&controller] {
            controller.setUpperBoundsFromActiveMask({1, 0, 1});
        },
        "active mask too small");
    assert_throws(
        [&controller] {
            controller.setUpperBoundsFromActiveMask({1, 0, 1, 0, 1});
        },
        "active mask too large");
}

void test_invalid_active_mask_entries_fail() {
    CandidateBoundController controller(4);
    assert_throws(
        [&controller] {
            controller.setUpperBoundsFromActiveMask({1, 0, 2, 0});
        },
        "active mask entry 2");
    assert_throws(
        [&controller] {
            controller.setUpperBoundsFromActiveMask({1, -1, 0, 0});
        },
        "active mask entry negative");
}

void test_upper_bound_out_of_range_fails() {
    CandidateBoundController controller(4);
    assert_throws(
        [&controller] {
            (void)controller.upperBound(-1);
        },
        "upperBound negative index");
    assert_throws(
        [&controller] {
            (void)controller.upperBound(4);
        },
        "upperBound index == n");
}

void test_indices_with_upper_bound_one_sorted() {
    CandidateBoundController controller(6);
    controller.setUpperBoundsFromActiveMask({0, 1, 0, 1, 1, 0});
    assert((controller.indicesWithUpperBoundOne() == std::vector<int>{1, 3, 4}));
}

void test_indices_with_upper_bound_zero_sorted() {
    CandidateBoundController controller(6);
    controller.setUpperBoundsFromActiveMask({0, 1, 0, 1, 1, 0});
    assert((controller.indicesWithUpperBoundZero() == std::vector<int>{0, 2, 5}));
}

}  // namespace

int main() {
    test_valid_construction();
    test_invalid_construction_with_nonpositive_n();
    test_apply_manager_state();
    test_active_candidates_get_upper_bound_one();
    test_inactive_candidates_get_upper_bound_zero();
    test_update_after_activate_candidates();
    test_update_after_activate_top_k();
    test_update_after_activate_all();
    test_consistency_with_manager();
    test_inconsistency_detection_after_different_active_mask();
    test_invalid_active_mask_size_fails();
    test_invalid_active_mask_entries_fail();
    test_upper_bound_out_of_range_fails();
    test_indices_with_upper_bound_one_sorted();
    test_indices_with_upper_bound_zero_sorted();

    std::cout << "All candidate bound controller tests passed.\n";
    return 0;
}
