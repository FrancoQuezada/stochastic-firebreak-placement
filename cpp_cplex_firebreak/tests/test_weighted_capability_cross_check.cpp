// Cross-checks that experiments::weighted_method_capability's rejection reasons stay in
// sync with the real solver guards in src/benders/FppCombinatorialBenders.cpp. Those
// guards are exercised end-to-end (with CPLEX) by the existing
// test_weighted_fpp_combinatorial_* suite; this test only guards against the capability
// matrix drifting away from the guard wording/semantics without a corresponding update
// here, since the two are maintained in different files.
#include <cassert>
#include <iostream>
#include <string>

#include "experiments/WeightedMethodCapability.hpp"

namespace {

using firebreak::experiments::classify_weighted_method;
using firebreak::experiments::weighted_method_capability;

// Verbatim guard messages from src/benders/FppCombinatorialBenders.cpp (Phase 6C1/6C2A/
// 6C2B/6C2C), reduced to the invariant substrings shared by all four phase variants.
constexpr const char* kLlbiGuardKeyPhrase = "does not combine with LLBI";
constexpr const char* kDominanceGuardKeyPhrase = "global dominance disabled";

void test_llbi_guard_reason_matches_solver_wording() {
    const auto query = classify_weighted_method(
        "FPP-Branch-Benders-Combinatorial-CoverageLLBI", "heterogeneous");
    const auto capability = weighted_method_capability(query);
    assert(!capability.supported);
    assert(capability.unsupported_reason.find(kLlbiGuardKeyPhrase) != std::string::npos);
}

void test_dominance_guard_reason_matches_solver_wording() {
    const auto query = classify_weighted_method(
        "FPP-Branch-Benders-Combinatorial-DominancePreprocess", "clustered");
    const auto capability = weighted_method_capability(query);
    assert(!capability.supported);
    assert(capability.unsupported_reason.find(kDominanceGuardKeyPhrase) != std::string::npos);
}

// The guard is non-homogeneous-only: homogeneous combinatorial+LLBI/dominance rows are
// real, supported production rows (see docs/METHODS_AND_OPTIONS_SUMMARY.md) and must not
// be filtered out of manifests.
void test_homogeneous_combinatorial_llbi_and_dominance_remain_supported() {
    assert(weighted_method_capability(
               classify_weighted_method(
                   "FPP-Branch-Benders-Combinatorial-CoverageLLBI", "homogeneous"))
               .supported);
    assert(weighted_method_capability(
               classify_weighted_method(
                   "FPP-Branch-Benders-Combinatorial-DominancePreprocess", "homogeneous"))
               .supported);
}

}  // namespace

int main() {
    test_llbi_guard_reason_matches_solver_wording();
    test_dominance_guard_reason_matches_solver_wording();
    test_homogeneous_combinatorial_llbi_and_dominance_remain_supported();
    std::cout << "All capability-matrix / solver-guard cross-check tests passed.\n";
    return 0;
}
