#include <cassert>
#include <iostream>
#include <vector>

#include "benders/CandidateBoundController.hpp"
#include "benders/RestrictedCandidateManager.hpp"
#include "benders/FppRestrictedCandidateBranchBendersSolver.hpp"

namespace {

void test_active_set_bounds_are_structural_and_binary() {
    firebreak::benders::RestrictedCandidateManager manager(5, 2, {0, 3});
    firebreak::benders::CandidateBoundController bounds(5);

    bounds.apply(manager);
    assert((bounds.upperBounds() == std::vector<double>{1.0, 0.0, 0.0, 1.0, 0.0}));
    assert(bounds.activeUpperBoundCount() == 2);
    assert(bounds.inactiveUpperBoundCount() == 3);

    manager.activateCandidates({1, 4});
    bounds.apply(manager);
    assert((bounds.upperBounds() == std::vector<double>{1.0, 1.0, 0.0, 1.0, 1.0}));

    manager.activateAll();
    bounds.apply(manager);
    assert((bounds.upperBounds() == std::vector<double>{1.0, 1.0, 1.0, 1.0, 1.0}));
}

void test_bound_diagnostics_default_to_no_permanent_pruning() {
    firebreak::benders::FppRestrictedCandidateBranchBendersResult result;
    result.candidate_bounds_enabled = true;
    result.candidate_bounds_weighted = true;
    result.candidate_bound_type = "active-set-upper-bound";
    result.candidate_bound_map_hash = "fnv1a64:weighted";
    result.candidates_evaluated_by_bound = 5;
    result.candidates_not_pruned_due_to_safety = 5;

    assert(result.candidate_bounds_enabled);
    assert(result.candidate_bounds_weighted);
    assert(result.candidate_bound_type == "active-set-upper-bound");
    assert(result.candidates_permanently_pruned == 0);
    assert(!result.early_exactness_certificate_used);
    assert(!result.unvalidated_bound_rejected);
}

void test_heuristic_status_is_not_global_optimality() {
    firebreak::benders::FppRestrictedCandidateBranchBendersResult result;
    result.restricted_candidate_exact_mode = false;
    result.heuristic_mode_enabled = true;
    result.stopped_before_full_activation = true;
    result.global_optimality_certified = false;
    result.final_lower_bound_is_global = false;
    result.restricted_bound_is_global = false;
    result.full_activation_avoided = true;

    assert(result.heuristic_mode_enabled);
    assert(!result.global_optimality_certified);
    assert(!result.final_lower_bound_is_global);
    assert(!result.restricted_bound_is_global);
    assert(result.full_activation_avoided);
}

}  // namespace

int main() {
    test_active_set_bounds_are_structural_and_binary();
    test_bound_diagnostics_default_to_no_permanent_pruning();
    test_heuristic_status_is_not_global_optimality();
    std::cout << "All weighted restricted candidate-bound tests passed.\n";
    return 0;
}
