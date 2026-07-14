#pragma once

#include <string>
#include <utility>
#include <vector>

#include "benders/BendersCut.hpp"
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
    double rhs_at_ybar = 0.0;
    double violation = 0.0;
    int active_nodes = 0;
    int activation_paths = 0;
    int nonzeros = 0;
    bool fractional = false;
    bool lift_mode_fallback = false;
};

struct FppCombinatorialSeparationSummary {
    std::vector<FppCombinatorialCut> cuts;
    int scenarios_checked = 0;
    int scenarios_skipped = 0;
    int violated_cuts = 0;
    int nonviolated_cuts = 0;
    double max_violation = 0.0;
    double separation_time_sec = 0.0;
    int total_paths = 0;
    int total_nonzeros = 0;
    int lift_fallback_count = 0;
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

    double average_paths_per_cut() const;
    double average_cut_nonzeros() const;
};

std::string to_string(FppCombinatorialBendersLiftMode mode);
FppCombinatorialBendersLiftMode parse_fpp_combinatorial_benders_lift_mode(
    const std::string& value);
std::string to_string(FppCombinatorialBendersScenarioOrder order);
FppCombinatorialBendersScenarioOrder parse_fpp_combinatorial_benders_scenario_order(
    const std::string& value);
std::vector<int> order_fpp_combinatorial_scenarios_by_eta(
    const std::vector<double>& eta_values_by_scenario,
    FppCombinatorialBendersScenarioOrder order);

void validate_fpp_combinatorial_benders_options(
    const FppCombinatorialBendersOptions& options);

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

private:
    const opt::OptimizationInstance& opt_;
    int node_count_ = 0;
    std::vector<char> eligible_;
    std::vector<int> y_position_by_node_;
    std::vector<std::vector<std::vector<int>>> successors_by_scenario_;
};

}  // namespace firebreak::benders
