#pragma once

#include <string>
#include <utility>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/FppStrengthening.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

enum class FppCombinatorialBendersLiftMode {
    None,
    Posterior,
    Heuristic,
};

enum class FppCombinatorialBendersScenarioOrder {
    EtaAscending,
    EtaDescending,
};

struct FppCombinatorialBendersOptions {
    bool enabled = false;
    FppCombinatorialBendersLiftMode lift_mode =
        FppCombinatorialBendersLiftMode::Heuristic;
    FppCombinatorialBendersScenarioOrder scenario_order =
        FppCombinatorialBendersScenarioOrder::EtaAscending;
    double cut_sampling_ratio = 0.10;
    bool separate_fractional = true;
    bool initial_cuts = true;
};

struct FppCombinatorialCut {
    BendersCut cut;
    BendersCut baseline_cut;
    double incumbent_weighted_loss = 0.0;
    double incumbent_eta = 0.0;
    double baseline_rhs_at_ybar = 0.0;
    double lifted_rhs_at_ybar = 0.0;
    double rhs_at_ybar = 0.0;
    double violation = 0.0;
    double baseline_tightness_error = 0.0;
    double lifted_tightness_error = 0.0;
    double tightness_error = 0.0;
    double cut_build_time_sec = 0.0;
    double propagation_time_sec = 0.0;
    double lifting_time_sec = 0.0;
    int active_nodes = 0;
    int activation_paths = 0;
    int baseline_nonzeros = 0;
    int lifted_nonzeros = 0;
    int nonzeros = 0;
    int candidates_considered_for_lifting = 0;
    int coefficients_changed = 0;
    int propagation_evaluations_for_lifting = 0;
    double max_coefficient_change = 0.0;
    bool fractional = false;
    bool lift_mode_fallback = false;
    bool lifting_attempted = false;
    bool lifting_success = false;
    bool lifting_failure = false;
    bool lifted_dominates_baseline = true;
};

struct FppCombinatorialSeparationSummary {
    std::vector<FppCombinatorialCut> cuts;
    int scenarios_checked = 0;
    int scenarios_skipped = 0;
    int violated_cuts = 0;
    int nonviolated_cuts = 0;
    double max_violation = 0.0;
    double separation_time_sec = 0.0;
    double propagation_time_sec = 0.0;
    double cut_build_time_sec = 0.0;
    int total_paths = 0;
    int total_nonzeros = 0;
    int lift_fallback_count = 0;
    int weighted_recourse_evaluations = 0;
    int tight_cuts = 0;
    double max_tightness_error = 0.0;
    int lifting_attempts = 0;
    int lifting_successes = 0;
    int lifting_failures = 0;
    int candidates_considered_for_lifting = 0;
    int coefficients_changed_by_lifting = 0;
    int propagation_evaluations_for_lifting = 0;
    int baseline_cut_nonzeros = 0;
    int lifted_cut_nonzeros = 0;
    int lifted_cuts_dominating_baseline = 0;
    double max_coefficient_change = 0.0;
    double max_baseline_tightness_error = 0.0;
    double max_lifted_tightness_error = 0.0;
    double lifting_time_sec = 0.0;
};

struct FppCombinatorialBendersStats {
    bool enabled = false;
    std::string lift_mode = "heuristic";
    std::string scenario_order = "eta-asc";
    double cut_sampling_ratio = 0.10;
    bool fractional_separation_enabled = true;
    bool initial_cuts_enabled = true;
    int integer_cuts_added = 0;
    int fractional_cuts_added = 0;
    int initial_cuts_added = 0;
    int scenarios_checked = 0;
    double separation_time_sec = 0.0;
    double total_paths_per_cut = 0.0;
    double total_nonzeros_per_cut = 0.0;
    int cuts_for_averages = 0;
    int num_violated_cuts = 0;
    int lift_fallback_count = 0;
    bool fractional_lift_disabled_due_to_validity = false;
    bool weighted = false;
    std::string mode = "legacy-unit-path-activation";
    std::string weight_map_hash;
    int weighted_recourse_evaluations = 0;
    int duplicate_cuts = 0;
    int tight_cuts = 0;
    double max_tightness_error = 0.0;
    double max_violation = 0.0;
    double propagation_time_sec = 0.0;
    double cut_build_time_sec = 0.0;
    std::string validity_mode = "unit-path-activation-cut";
    bool lifting_weighted = false;
    std::string lifting_mode = "none";
    std::string lifting_weight_map_hash;
    int lifting_attempts = 0;
    int lifting_successes = 0;
    int lifting_failures = 0;
    int candidates_considered_for_lifting = 0;
    int coefficients_changed_by_lifting = 0;
    int propagation_evaluations_for_lifting = 0;
    int baseline_cut_nonzeros = 0;
    int lifted_cut_nonzeros = 0;
    int lifted_cuts_dominating_baseline = 0;
    double max_coefficient_change = 0.0;
    double max_baseline_tightness_error = 0.0;
    double max_lifted_tightness_error = 0.0;
    double lifting_time_sec = 0.0;
    std::string lifting_validity_mode = "none";
    int initial_solutions_evaluated = 0;
    int initial_cuts_generated = 0;
    int initial_duplicate_cuts = 0;
    double initial_cut_time_sec = 0.0;
    bool root_cuts_enabled = false;
    int root_rounds = 0;
    int root_integer_points_evaluated = 0;
    int root_fractional_points_evaluated = 0;
    int root_cuts_generated = 0;
    int root_cuts_added = 0;
    int root_duplicate_cuts = 0;
    double root_cut_time_sec = 0.0;
    std::string root_skipped_reason;
    std::string fractional_validity_mode = "disabled";
    int fractional_separation_calls = 0;
    int fractional_scenarios_evaluated = 0;
    int fractional_cuts_generated = 0;
    int fractional_duplicate_cuts = 0;
    double fractional_max_violation = 0.0;
    double fractional_max_tightness_error = 0.0;
    double fractional_separation_time_sec = 0.0;

    double average_paths_per_cut() const;
    double average_cut_nonzeros() const;
};

std::string to_string(FppCombinatorialBendersLiftMode mode);
FppCombinatorialBendersLiftMode parse_fpp_combinatorial_benders_lift_mode(
    const std::string& value);
std::string fpp_phase6c2a_combinatorial_mode(
    FppCombinatorialBendersLiftMode mode);
std::string fpp_phase6c2a_lifting_validity_mode(
    FppCombinatorialBendersLiftMode mode,
    bool weighted);
std::string to_string(FppCombinatorialBendersScenarioOrder order);
FppCombinatorialBendersScenarioOrder parse_fpp_combinatorial_benders_scenario_order(
    const std::string& value);
std::vector<int> order_fpp_combinatorial_scenarios_by_eta(
    const std::vector<double>& eta_values_by_scenario,
    FppCombinatorialBendersScenarioOrder order);

void validate_fpp_combinatorial_benders_options(
    const FppCombinatorialBendersOptions& options);

bool is_fpp_phase6c1_weighted_combinatorial_baseline(
    const FppCombinatorialBendersOptions& options);

void validate_fpp_phase6c1_weighted_combinatorial_baseline(
    const FppCombinatorialBendersOptions& options,
    bool use_root_user_cuts,
    bool use_lifted_lower_bounds,
    const FppStrengtheningOptions& strengthening_options);

bool is_fpp_phase6c2a_weighted_combinatorial_integer_mode(
    const FppCombinatorialBendersOptions& options);

void validate_fpp_phase6c2a_weighted_combinatorial_integer_mode(
    const FppCombinatorialBendersOptions& options,
    bool use_root_user_cuts,
    bool use_lifted_lower_bounds,
    const FppStrengtheningOptions& strengthening_options);

bool is_fpp_phase6c2b_weighted_combinatorial_mode(
    const FppCombinatorialBendersOptions& options);

void validate_fpp_phase6c2b_weighted_combinatorial_mode(
    const FppCombinatorialBendersOptions& options,
    bool use_root_user_cuts,
    bool use_lifted_lower_bounds,
    const FppStrengtheningOptions& strengthening_options);

class FppCombinatorialBendersSeparator {
public:
    explicit FppCombinatorialBendersSeparator(const opt::OptimizationInstance& opt);

    FppCombinatorialCut separateScenario(
        int scenario_position,
        const std::vector<double>& y_values_by_eligible_position,
        double eta_value,
        bool fractional,
        FppCombinatorialBendersLiftMode lift_mode,
        double tolerance) const;

    FppCombinatorialSeparationSummary separateViolatedCuts(
        const std::vector<double>& y_values_by_eligible_position,
        const std::vector<double>& eta_values_by_scenario,
        bool fractional,
        FppCombinatorialBendersLiftMode lift_mode,
        FppCombinatorialBendersScenarioOrder scenario_order,
        double cut_sampling_ratio,
        double tolerance) const;

    std::vector<FppCombinatorialCut> initialCutsFromSolution(
        const std::vector<int>& y_values_by_eligible_position,
        FppCombinatorialBendersLiftMode lift_mode) const;

    std::vector<int> greedyInitialSolution() const;

    std::vector<double> evaluateScenarioLosses(
        const std::vector<int>& y_values_by_eligible_position) const;

    bool weighted() const { return weighted_; }
    const std::string& weightMapHash() const { return weight_map_hash_; }
    const std::string& validityMode() const { return validity_mode_; }

private:
    const opt::OptimizationInstance& opt_;
    int node_count_ = 0;
    std::vector<char> eligible_;
    std::vector<int> y_position_by_node_;
    std::vector<std::vector<std::vector<int>>> successors_by_scenario_;
    std::vector<double> compact_weights_;
    bool weighted_ = false;
    std::string weight_map_hash_;
    std::string validity_mode_ = "unit-path-activation-cut";
};

}  // namespace firebreak::benders
