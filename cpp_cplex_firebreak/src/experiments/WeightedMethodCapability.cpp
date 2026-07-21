#include "experiments/WeightedMethodCapability.hpp"

#include <algorithm>
#include <cctype>

namespace firebreak::experiments {

namespace {

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool is_known_profile(const std::string& profile) {
    return profile == "homogeneous" || profile == "heterogeneous" || profile == "clustered";
}

bool is_known_objective(const std::string& objective) {
    return objective == "expected" || objective == "cvar" || objective == "mean_cvar";
}

}  // namespace

WeightedMethodCapabilityQuery classify_weighted_method(
    const std::string& method,
    const std::string& weight_profile,
    const std::string& objective) {
    const std::string lower = lower_copy(method);

    WeightedMethodCapabilityQuery query;
    query.method = method;
    query.weight_profile = lower_copy(weight_profile);

    if (!objective.empty()) {
        query.objective = lower_copy(objective);
    } else if (contains(lower, "meancvar") || contains(lower, "mean-cvar")) {
        query.objective = "mean_cvar";
    } else if (contains(lower, "cvar")) {
        query.objective = "cvar";
    } else {
        query.objective = "expected";
    }

    query.restricted_candidate = contains(lower, "restricted");
    query.combinatorial_benders = contains(lower, "combinatorial");
    query.coverage_llbi = contains(lower, "coveragellbi");
    query.path_llbi = contains(lower, "pathllbi");
    query.projected_llbi = contains(lower, "projected");
    // Plain "-LLBI" (standard lifted lower bounds) without a coverage/path/projected
    // qualifier.
    query.standard_llbi = contains(lower, "llbi") && !query.coverage_llbi &&
                          !query.path_llbi && !query.projected_llbi;
    query.global_dominance = contains(lower, "dominance");
    query.conditional_zero_benefit_fixing = contains(lower, "conditional") ||
                                            contains(lower, "zerobenefit");
    return query;
}

WeightedMethodCapability weighted_method_capability(const WeightedMethodCapabilityQuery& query) {
    WeightedMethodCapability capability;

    if (!is_known_profile(query.weight_profile)) {
        capability.supported = false;
        capability.unsupported_reason =
            "Unknown weight profile '" + query.weight_profile +
            "' (expected homogeneous, heterogeneous, or clustered).";
        return capability;
    }
    if (!is_known_objective(query.objective)) {
        capability.supported = false;
        capability.unsupported_reason =
            "Unknown objective '" + query.objective +
            "' (expected expected, cvar, or mean_cvar).";
        return capability;
    }

    const bool non_homogeneous = query.weight_profile != "homogeneous";
    const bool any_llbi = query.standard_llbi || query.coverage_llbi ||
                          query.path_llbi || query.projected_llbi;

    // Restricted-candidate combinatorial Benders is not supported (Phase 9 limitation).
    if (query.restricted_candidate && query.combinatorial_benders) {
        capability.supported = false;
        capability.unsupported_reason =
            "Restricted-candidate combinatorial Benders is not supported.";
        return capability;
    }

    if (query.combinatorial_benders) {
        // Mirrors src/benders/FppCombinatorialBenders.cpp guards: non-homogeneous
        // weighted combinatorial Benders does not combine with LLBI or global dominance.
        if (non_homogeneous && any_llbi) {
            capability.supported = false;
            capability.unsupported_reason =
                "Non-homogeneous weighted combinatorial Benders does not combine with "
                "LLBI or projected LLBI families.";
            return capability;
        }
        if (non_homogeneous && query.global_dominance) {
            capability.supported = false;
            capability.unsupported_reason =
                "Non-homogeneous weighted combinatorial Benders keeps global dominance "
                "disabled until the combinatorial separator remapping is validated.";
            return capability;
        }
        if (query.conditional_zero_benefit_fixing) {
            capability.supported = false;
            capability.unsupported_reason =
                "Conditional zero-benefit fixing is detector-only and does not combine "
                "with combinatorial Benders.";
            return capability;
        }
    }

    // All listed FPP and DPV methods have validated weighted support across the three
    // profiles and the three objectives; paired reburn evaluation is available for any
    // method that emits a firebreak selection (all of them).
    capability.supported = true;
    return capability;
}

}  // namespace firebreak::experiments
