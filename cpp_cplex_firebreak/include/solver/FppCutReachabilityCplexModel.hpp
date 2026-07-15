#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cuts/DominatorCuts.hpp"
#include "cuts/SeparatorContextCallback.hpp"
#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/ModelResult.hpp"
#include "solver/FppWeightedLossUtils.hpp"
#include "solver/WarmStart.hpp"

namespace firebreak::solver {

struct CutPropagationConstraintDescriptor {
    int scenario_id = 0;
    int u_index = -1;
    int v_index = -1;
};

struct CutPassThroughConstraintDescriptor {
    int scenario_id = 0;
    int node_index = -1;
    bool node_has_firebreak_variable = false;
    int y_position = -1;
};

struct FppCutReachabilityModelStructure {
    std::size_t x_variable_count = 0;
    std::size_t q_variable_count = 0;
    std::size_t y_variable_count = 0;
    std::size_t total_variable_count = 0;
    std::size_t budget_constraint_count = 0;
    std::size_t root_constraint_count = 0;
    std::size_t propagation_entrance_constraint_count = 0;
    std::size_t pass_through_constraint_count = 0;
    std::size_t firebreak_upper_bound_constraint_count = 0;
    std::size_t total_constraint_count = 0;
    std::vector<int> y_indices;
    std::vector<std::size_t> observed_node_count_by_scenario;
    std::vector<CutPropagationConstraintDescriptor> propagation_constraints;
    std::vector<CutPassThroughConstraintDescriptor> pass_through_constraints;
    std::vector<CutPassThroughConstraintDescriptor> firebreak_upper_bound_constraints;
    std::vector<ScenarioLossCoefficientDescriptor> scenario_loss_coefficients;
    std::vector<ScenarioLossCoefficientDescriptor> objective_x_coefficients;
    std::vector<ScenarioLossCoefficientDescriptor> objective_q_coefficients;
    std::vector<ScenarioLossCoefficientDescriptor> cvar_loss_coefficients;

    bool has_y_for_node_index(int node_index) const;
};

struct FppCutReachabilityMipStartValues {
    std::vector<char> y_selected_by_compact_node;
    std::vector<std::vector<int>> nodes_by_scenario;
    std::vector<std::vector<char>> x_burned_by_scenario;
    std::vector<std::vector<char>> q_reached_by_scenario;
    double recourse_objective = 0.0;
    bool feasible = true;
    std::vector<std::string> notes;
};

FppCutReachabilityModelStructure analyze_fpp_cut_reachability_model_structure(
    const opt::OptimizationInstance& opt);
FppCutReachabilityModelStructure analyze_fpp_cut_reachability_model_structure(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config);

FppCutReachabilityMipStartValues build_fpp_cut_reachability_mip_start_values(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& selected_firebreak_compact_nodes);

class FppCutReachabilityCplexModel {
public:
    ModelResult solve(
        const opt::OptimizationInstance& opt,
        double time_limit_seconds,
        double mip_gap,
        int threads,
        bool verbose,
        const WarmStart* warm_start = nullptr,
        const cuts::DominatorCutOptions* dominator_options = nullptr,
        const cuts::SeparatorCutOptions* separator_options = nullptr,
        const risk::RiskMeasureConfig& risk_config = risk::RiskMeasureConfig()) const;
};

}  // namespace firebreak::solver
