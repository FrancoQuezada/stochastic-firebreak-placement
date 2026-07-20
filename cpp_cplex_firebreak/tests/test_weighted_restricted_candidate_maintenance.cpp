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

RestrictedCandidateMaintenanceOptions maintenance_options() {
    RestrictedCandidateMaintenanceOptions opts;
    opts.min_active_size = 2;
    opts.max_active_size = 3;
    opts.deactivation_batch_size = 2;
    opts.deactivation_min_age = 1;
    opts.reactivation_cooldown_rounds = 1;
    opts.protect_selected_candidates = true;
    return opts;
}

void test_weighted_benders_scores_drive_deactivation_order() {
    RestrictedCandidateManager manager(5, 1, {0, 1, 2, 3});
    RestrictedCandidateMaintenanceTracker tracker(
        5,
        manager.activeCandidates(),
        "fnv1a64:weighted-map");
    tracker.completeRound(manager);

    RestrictedCandidateMaintenanceDecision decision;
    decision.weighted = true;
    decision.weight_map_hash = "fnv1a64:weighted-map";
    const auto deactivated = tracker.selectDeactivationCandidates(
        manager,
        {
            {0, 10.0},
            {1, 1.0},
            {2, 1.0},
            {3, 4.0},
        },
        {},
        {},
        maintenance_options(),
        2,
        decision);

    assert((deactivated == std::vector<int>{1, 2}));
    assert(decision.weight_map_hash == "fnv1a64:weighted-map");
    assert(decision.deactivation_candidate_count == 4);
    assert(decision.deactivation_scores_considered[0].first == 1);
    assert(decision.deactivation_scores_considered[1].first == 2);
}

void test_incumbent_and_new_activation_are_protected() {
    RestrictedCandidateManager manager(5, 1, {0, 1, 2, 3});
    RestrictedCandidateMaintenanceTracker tracker(5, manager.activeCandidates(), "hash-a");
    tracker.completeRound(manager);

    RestrictedCandidateMaintenanceDecision decision;
    const auto deactivated = tracker.selectDeactivationCandidates(
        manager,
        {{0, -100.0}, {1, -90.0}, {2, -80.0}, {3, -70.0}},
        {0},
        {1},
        maintenance_options(),
        2,
        decision);

    assert((deactivated == std::vector<int>{2, 3}));
    assert((decision.selected_candidates_protected == std::vector<int>{0}));
    assert(decision.protected_selected_count == 1);
    assert(decision.protected_newly_activated_count == 1);
}

void test_hash_mismatch_rejected_and_reactivation_preserves_state() {
    RestrictedCandidateManager manager(4, 1, {0, 1});
    RestrictedCandidateMaintenanceTracker tracker(4, manager.activeCandidates(), "hash-a");
    tracker.completeRound(manager);
    assert_throws(
        [&] { tracker.setWeightMapHash("hash-b"); },
        "tracker hash mismatch");

    tracker.recordDeactivated({1});
    const auto deactivated = manager.deactivateCandidates({1});
    assert((deactivated == std::vector<int>{1}));
    tracker.completeRound(manager);
    const auto activated = manager.activateCandidates({1});
    tracker.recordActivated(activated);

    assert((activated == std::vector<int>{1}));
    assert(manager.isActive(1));
    assert(tracker.deactivationCount(1) == 1);
    assert(tracker.activationCount(1) == 1);
    assert(tracker.stateChangeCount(1) == 2);

    RestrictedCandidateMaintenanceDecision decision;
    decision.weight_map_hash = "hash-b";
    assert_throws(
        [&] {
            (void)tracker.selectDeactivationCandidates(
                manager,
                {{0, 0.0}, {1, 0.0}},
                {},
                {},
                maintenance_options(),
                1,
                decision);
        },
        "decision hash mismatch");
}

}  // namespace

int main() {
    test_weighted_benders_scores_drive_deactivation_order();
    test_incumbent_and_new_activation_are_protected();
    test_hash_mismatch_rejected_and_reactivation_preserves_state();
    std::cout << "All weighted restricted candidate maintenance tests passed.\n";
    return 0;
}
