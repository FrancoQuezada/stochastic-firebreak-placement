#pragma once

#include <cstddef>
#include <vector>

#include "cuts/DominatorCuts.hpp"
#include "cuts/SeparatorContextCallback.hpp"
#include "benders/FppStrengthening.hpp"
#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/ModelResult.hpp"
#include "solver/FppWeightedLossUtils.hpp"
#include "solver/WarmStart.hpp"

namespace firebreak::solver {

struct PropagationConstraintDescriptor {
    int scenario_id = 0;
    int u_index = -1;
    int v_index = -1;
    bool target_has_firebreak_variable = false;
    int target_y_position = -1;
};

struct FppSaaModelStructure {
    std::size_t x_variable_count = 0;
    std::size_t y_variable_count = 0;
    std::size_t risk_threshold_variable_count = 0;
    std::size_t cvar_excess_variable_count = 0;
    std::size_t total_variable_count = 0;
    std::size_t budget_constraint_count = 0;
    std::size_t ignition_constraint_count = 0;
    std::size_t propagation_constraint_count = 0;
    std::size_t cvar_excess_constraint_count = 0;
    std::size_t total_constraint_count = 0;
    std::vector<int> y_indices;
    std::vector<PropagationConstraintDescriptor> propagation_constraints;
    std::vector<ScenarioLossCoefficientDescriptor> scenario_loss_coefficients;
    std::vector<ScenarioLossCoefficientDescriptor> objective_x_coefficients;
    std::vector<ScenarioLossCoefficientDescriptor> cvar_loss_coefficients;

    bool has_y_for_node_index(int node_index) const;
};

FppSaaModelStructure analyze_fpp_saa_model_structure(const opt::OptimizationInstance& opt);
FppSaaModelStructure analyze_fpp_saa_model_structure(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config);

class FppSaaCplexModel {
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
        const risk::RiskMeasureConfig& risk_config = risk::RiskMeasureConfig(),
        const benders::FppStrengtheningOptions* strengthening_options = nullptr) const;
};

}  // namespace firebreak::solver
