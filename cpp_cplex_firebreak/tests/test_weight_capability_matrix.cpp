#include <cassert>
#include <iostream>
#include <string>

#include "experiments/WeightedMethodCapability.hpp"

namespace {

using firebreak::experiments::classify_weighted_method;
using firebreak::experiments::weighted_method_capability;
using firebreak::experiments::WeightedMethodCapabilityQuery;

bool supported(const std::string& method, const std::string& profile) {
    return weighted_method_capability(classify_weighted_method(method, profile)).supported;
}

void test_label_classification() {
    const auto combi = classify_weighted_method(
        "FPP-Branch-Benders-Combinatorial-CVaR-EtaDesc", "clustered");
    assert(combi.combinatorial_benders);
    assert(combi.objective == "cvar");
    assert(!combi.restricted_candidate);

    const auto proj = classify_weighted_method(
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp-RootCuts", "heterogeneous");
    assert(proj.projected_llbi);
    assert(proj.objective == "mean_cvar");

    const auto dpv = classify_weighted_method("DPV-SAA", "homogeneous");
    assert(dpv.objective == "expected");
    assert(!dpv.combinatorial_benders && !dpv.projected_llbi);

    const auto llbi = classify_weighted_method(
        "FPP-Branch-Benders-LLBI-RootCuts", "homogeneous");
    assert(llbi.standard_llbi);
    assert(!llbi.coverage_llbi && !llbi.path_llbi && !llbi.projected_llbi);
}

void test_all_profiles_supported_for_standard_methods() {
    for (const std::string method :
         {"FPP-SAA", "FPP-Branch-Benders-RootCuts", "DPV-SAA", "DPV-Branch-Benders",
          "Static-DPV", "Greedy-DPV2", "FPP-SAA-CVaR", "FPP-SAA-MeanCVaR"}) {
        for (const std::string profile :
             {"homogeneous", "heterogeneous", "clustered"}) {
            assert(supported(method, profile));
        }
    }
}

void test_unknown_profile_and_objective_rejected() {
    assert(!supported("FPP-SAA", "bogus"));
    WeightedMethodCapabilityQuery q;
    q.method = "FPP-SAA";
    q.weight_profile = "homogeneous";
    q.objective = "bogus";
    assert(!weighted_method_capability(q).supported);
}

void test_restricted_combinatorial_rejected() {
    WeightedMethodCapabilityQuery q;
    q.method = "FPP-Restricted-Branch-Benders-Combinatorial";
    q.weight_profile = "homogeneous";
    q.objective = "expected";
    q.restricted_candidate = true;
    q.combinatorial_benders = true;
    const auto cap = weighted_method_capability(q);
    assert(!cap.supported);
    assert(cap.unsupported_reason.find("Restricted-candidate combinatorial") !=
           std::string::npos);
}

void test_nonhomogeneous_combinatorial_llbi_and_dominance_rejected() {
    // Non-homogeneous combinatorial + LLBI is rejected (mirrors solver guard).
    assert(!supported("FPP-Branch-Benders-Combinatorial-CoverageLLBI", "clustered"));
    assert(!supported("FPP-Branch-Benders-Combinatorial-CoverageLLBI", "heterogeneous"));
    // Homogeneous combinatorial + LLBI is allowed (guard is non-homogeneous only).
    assert(supported("FPP-Branch-Benders-Combinatorial-CoverageLLBI", "homogeneous"));

    // Non-homogeneous combinatorial + dominance rejected; homogeneous allowed.
    assert(!supported("FPP-Branch-Benders-Combinatorial-DominancePreprocess", "clustered"));
    assert(supported("FPP-Branch-Benders-Combinatorial-DominancePreprocess", "homogeneous"));

    // Plain combinatorial (no LLBI/dominance) is supported on all profiles.
    assert(supported("FPP-Branch-Benders-Combinatorial", "clustered"));
    assert(supported("FPP-Branch-Benders-Combinatorial-CVaR-EtaDesc", "heterogeneous"));
}

}  // namespace

int main() {
    test_label_classification();
    test_all_profiles_supported_for_standard_methods();
    test_unknown_profile_and_objective_rejected();
    test_restricted_combinatorial_rejected();
    test_nonhomogeneous_combinatorial_llbi_and_dominance_rejected();
    std::cout << "All weighted capability matrix tests passed.\n";
    return 0;
}
