#pragma once

#include <string>
#include <vector>

#include "cuts/NodeSeparatorMinCut.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::cuts {

struct SeparatorCutOptions {
    bool enabled = false;
    bool sep_at_root = true;
    int sep_frequency_nodes = 50;
    int sep_max_scenarios_per_call = 10;
    int sep_max_nodes_per_scenario = 20;
    int sep_max_cuts_per_call = 100;
    double sep_min_violation = 1.0e-5;
    int sep_max_cut_cardinality = 50;
    bool verbose = false;
};

struct SeparatorCutStats {
    int callback_invocations = 0;
    int cuts_added = 0;
    int min_cut_calls = 0;
    int duplicate_cuts_skipped = 0;
    int large_cuts_skipped = 0;
    double separator_time_sec = 0.0;
    double max_cut_violation = 0.0;
    std::vector<std::string> notes;
};

struct CandidateSeparatorCut {
    int scenario_index = -1;
    int target_compact_node = -1;
    std::vector<int> separator_compact_nodes;
    double separator_capacity = 0.0;
    double lhs_value = 0.0;
    double rhs_value = 0.0;
    double violation = 0.0;
};

class SeparatorCutSeparator {
public:
    SeparatorCutSeparator(
        const opt::OptimizationInstance& instance,
        SeparatorCutOptions options);

    std::vector<CandidateSeparatorCut> separate(
        const std::vector<double>& ybar_by_compact_node,
        const std::vector<std::vector<double>>& xbar_by_scenario_and_compact_node,
        int max_cuts_override = -1);

    const SeparatorCutStats& stats() const;
    void resetStats();
    void clearCutCache();

private:
    bool isEligible(int compact_node) const;
    bool separatorIsUsable(const opt::OptimizationScenario& scenario, const std::vector<int>& separator) const;
    double yValue(const std::vector<double>& ybar_by_compact_node, int compact_node) const;
    double xValue(
        const std::vector<std::vector<double>>& xbar_by_scenario_and_compact_node,
        std::size_t scenario_index,
        int compact_node) const;
    double scenarioProbability(std::size_t scenario_index) const;
    std::string cutKey(int scenario_index, int target_compact_node, const std::vector<int>& separator) const;

    const opt::OptimizationInstance& instance_;
    SeparatorCutOptions options_;
    SeparatorCutStats stats_;
    NodeSeparatorMinCut separator_engine_;
    std::vector<char> eligible_by_node_;
    std::vector<std::string> cut_cache_;
};

}  // namespace firebreak::cuts
