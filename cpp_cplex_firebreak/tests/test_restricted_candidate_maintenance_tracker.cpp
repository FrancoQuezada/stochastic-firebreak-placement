#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "benders/RestrictedCandidateMaintenanceTracker.hpp"

namespace {

using firebreak::benders::RestrictedCandidateMaintenanceDecision;
using firebreak::benders::RestrictedCandidateMaintenanceOptions;
using firebreak::benders::RestrictedCandidateMaintenanceTracker;
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

RestrictedCandidateMaintenanceOptions options() {
    RestrictedCandidateMaintenanceOptions opts;
    opts.min_active_size = 2;
    opts.max_active_size = 4;
    opts.deactivation_batch_size = 2;
    opts.deactivation_min_age = 1;
    opts.reactivation_cooldown_rounds = 1;
    opts.protect_selected_candidates = true;
    return opts;
}

void test_selected_and_newly_activated_are_protected() {
    RestrictedCandidateManager manager(6, 1, {0, 1, 2, 3});
    RestrictedCandidateMaintenanceTracker tracker(6, manager.activeCandidates());
    tracker.completeRound(manager);

    RestrictedCandidateMaintenanceDecision decision;
    const auto deactivation = tracker.selectDeactivationCandidates(
        manager,
        {
            {0, -5.0},
            {1, -4.0},
            {2, -3.0},
            {3, -2.0},
        },
        {0},
        {1},
        options(),
        2,
        decision);

    assert((deactivation == std::vector<int>{2, 3}));
    assert(decision.protected_selected_count == 1);
    assert(decision.protected_newly_activated_count == 1);
    assert(decision.deactivation_candidate_count == 2);
}

void test_min_active_age_is_enforced() {
    RestrictedCandidateManager manager(5, 1, {0, 1, 2});
    RestrictedCandidateMaintenanceTracker tracker(5, manager.activeCandidates());

    RestrictedCandidateMaintenanceDecision decision;
    const auto deactivation = tracker.selectDeactivationCandidates(
        manager,
        {{0, -1.0}, {1, -2.0}, {2, -3.0}},
        {},
        {},
        options(),
        2,
        decision);

    assert(deactivation.empty());
    assert(decision.protected_min_age_count == 3);
}

void test_min_active_size_is_enforced() {
    RestrictedCandidateManager manager(5, 2, {0, 1, 2});
    RestrictedCandidateMaintenanceTracker tracker(5, manager.activeCandidates());
    tracker.completeRound(manager);

    auto opts = options();
    opts.min_active_size = 3;

    RestrictedCandidateMaintenanceDecision decision;
    const auto deactivation = tracker.selectDeactivationCandidates(
        manager,
        {{0, -1.0}, {1, -2.0}, {2, -3.0}},
        {},
        {},
        opts,
        2,
        decision);

    assert(deactivation.empty());
}

void test_reactivation_cooldown_blocks_activation_scores() {
    RestrictedCandidateManager manager(5, 1, {0, 1, 2});
    RestrictedCandidateMaintenanceTracker tracker(5, manager.activeCandidates());
    tracker.completeRound(manager);
    tracker.recordDeactivated({2});
    manager.deactivateCandidates({2});
    tracker.completeRound(manager);

    RestrictedCandidateMaintenanceDecision decision;
    const auto filtered = tracker.filterActivationScores(
        {{2, 10.0}, {3, 8.0}},
        1,
        decision);

    assert((filtered == std::vector<std::pair<int, double>>{{3, 8.0}}));
    assert(decision.reactivation_blocked_by_cooldown_count == 1);
    assert(decision.protected_cooldown_count == 1);
}

void test_initial_candidates_can_be_deactivated_after_age() {
    RestrictedCandidateManager manager(5, 1, {0, 1, 2});
    RestrictedCandidateMaintenanceTracker tracker(5, manager.activeCandidates());
    tracker.completeRound(manager);

    RestrictedCandidateMaintenanceDecision decision;
    const auto deactivation = tracker.selectDeactivationCandidates(
        manager,
        {{0, -5.0}, {1, 1.0}, {2, 2.0}},
        {},
        {},
        options(),
        1,
        decision);

    assert((deactivation == std::vector<int>{0}));
}

void test_tail_protected_candidates_are_not_deactivated() {
    RestrictedCandidateManager manager(6, 1, {0, 1, 2, 3});
    RestrictedCandidateMaintenanceTracker tracker(6, manager.activeCandidates());
    tracker.completeRound(manager);

    RestrictedCandidateMaintenanceDecision decision;
    const auto deactivation = tracker.selectDeactivationCandidates(
        manager,
        {
            {0, -5.0},
            {1, -4.0},
            {2, -3.0},
            {3, -2.0},
        },
        {},
        {},
        {0, 1},
        options(),
        2,
        decision);

    assert((deactivation == std::vector<int>{2, 3}));
    assert((decision.tail_protected_candidates == std::vector<int>{0, 1}));
    assert(decision.protected_tail_count == 2);
    assert(decision.deactivation_candidate_count == 2);
}

void test_state_change_counts_are_tracked() {
    RestrictedCandidateManager manager(5, 1, {0, 1});
    RestrictedCandidateMaintenanceTracker tracker(5, manager.activeCandidates());

    tracker.recordActivated({2});
    manager.activateCandidates({2});
    tracker.recordDeactivated({1});
    manager.deactivateCandidates({1});
    tracker.completeRound(manager);

    assert(tracker.activationCount(2) == 1);
    assert(tracker.deactivationCount(1) == 1);
    assert(tracker.stateChangeCount(1) == 1);
    assert(tracker.stateChangeCount(2) == 1);
    assert(tracker.maxStateChangeCount() == 1);
    assert(tracker.averageStateChangeCount() > 0.0);
}

void test_invalid_inputs_fail() {
    assert_throws(
        [] {
            RestrictedCandidateMaintenanceTracker tracker(0, {});
            (void)tracker;
        },
        "invalid tracker candidate count");

    RestrictedCandidateManager manager(3, 1, {0, 1});
    RestrictedCandidateMaintenanceTracker tracker(3, manager.activeCandidates());
    assert_throws(
        [&tracker] {
            RestrictedCandidateMaintenanceDecision decision;
            (void)tracker.filterActivationScores({{3, 1.0}}, 1, decision);
        },
        "invalid activation score candidate");
}

}  // namespace

int main() {
    test_selected_and_newly_activated_are_protected();
    test_min_active_age_is_enforced();
    test_min_active_size_is_enforced();
    test_reactivation_cooldown_blocks_activation_scores();
    test_initial_candidates_can_be_deactivated_after_age();
    test_tail_protected_candidates_are_not_deactivated();
    test_state_change_counts_are_tracked();
    test_invalid_inputs_fail();

    std::cout << "All restricted candidate maintenance tracker tests passed.\n";
    return 0;
}
