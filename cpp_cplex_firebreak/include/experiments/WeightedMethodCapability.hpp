#pragma once

#include <string>

namespace firebreak::experiments {

// Machine-readable feature decomposition of a method + objective + strengthening
// combination. This is the central source of truth for which weighted combinations are
// supported. The solver's own runtime guards remain authoritative at execution; the
// capability matrix mirrors them so unsupported rows are filtered out of manifests.
struct WeightedMethodCapabilityQuery {
    std::string method;          // canonical/label form, informational
    std::string weight_profile;  // homogeneous | heterogeneous | clustered
    std::string objective;       // expected | cvar | mean_cvar

    bool restricted_candidate = false;
    bool combinatorial_benders = false;
    bool standard_llbi = false;
    bool coverage_llbi = false;
    bool path_llbi = false;
    bool projected_llbi = false;
    bool global_dominance = false;
    bool conditional_zero_benefit_fixing = false;
    bool paired_reburn = false;
};

struct WeightedMethodCapability {
    bool supported = true;
    std::string unsupported_reason;
};

// Classify a method label (e.g. "FPP-Branch-Benders-Combinatorial-CVaR-EtaDesc" or
// "DPV-SAA") plus an objective into a capability query with the feature flags derived
// from the label tokens. Objective may be given explicitly; when empty it is inferred
// from the label.
WeightedMethodCapabilityQuery classify_weighted_method(
    const std::string& method,
    const std::string& weight_profile,
    const std::string& objective = "");

// Return whether the queried combination is supported and, if not, why. Encodes the
// Phase 8/9 known limitations: non-homogeneous weighted combinatorial Benders does not
// combine with LLBI or global dominance; restricted-candidate combinatorial Benders is
// unsupported; unknown profiles/objectives are rejected.
WeightedMethodCapability weighted_method_capability(const WeightedMethodCapabilityQuery& query);

}  // namespace firebreak::experiments
