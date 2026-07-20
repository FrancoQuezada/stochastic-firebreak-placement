#pragma once

#include <string>
#include <utility>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

struct FixedFppLossResult {
    int scenario_id = 0;
    double burned_area = 0.0;
    double weighted_burn_loss = 0.0;
    std::vector<char> burned_by_compact_index;
};

struct FppLiftedLowerBoundInequality {
    int scenario_id = 0;
    double f_empty = 0.0;
    double rhs_constant = 0.0;
    int nonzero_coefficients = 0;
    std::vector<std::pair<int, double>> coefficients_by_compact_index;
    std::vector<std::string> notes;

    double evaluateAt(
        const std::vector<int>& y_by_eligible_position,
        const opt::OptimizationInstance& opt) const;
    double evaluateAtCompact(const std::vector<char>& y_by_compact_index) const;
};

struct FppLiftedLowerBoundPrecomputeResult {
    std::vector<FppLiftedLowerBoundInequality> inequalities;
    double precompute_time_sec = 0.0;
    int total_nonzero_coefficients = 0;
    double min_rhs = 0.0;
    double max_rhs = 0.0;
    bool weighted = false;
    std::string weight_map_hash;
    int scenarios_precomputed = 0;
    int singletons_evaluated = 0;
    double no_firebreak_loss_min = 0.0;
    double no_firebreak_loss_max = 0.0;
    double singleton_benefit_min = 0.0;
    double singleton_benefit_max = 0.0;
    bool cache_hit = false;
    std::string validity_mode;
    std::vector<std::string> notes;
};

FixedFppLossResult evaluate_fixed_y_fpp_loss(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    const std::vector<char>& selected_firebreak_by_compact_index);

FixedFppLossResult evaluate_optimistic_singleton_fpp_loss(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    int firebreak_compact_index);

FppLiftedLowerBoundInequality build_fpp_lifted_lower_bound_for_scenario(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    double coefficient_threshold = 1.0e-12);

FppLiftedLowerBoundPrecomputeResult build_fpp_lifted_lower_bounds(
    const opt::OptimizationInstance& opt,
    double coefficient_threshold = 1.0e-12);

struct FppLiftedLowerBoundValidationResult {
    bool valid = true;
    std::string message;
};

FppLiftedLowerBoundValidationResult validate_fpp_lifted_lower_bound_exhaustive(
    const opt::OptimizationInstance& opt,
    int scenario_position,
    int budget,
    double tolerance = 1.0e-9);

}  // namespace firebreak::benders
