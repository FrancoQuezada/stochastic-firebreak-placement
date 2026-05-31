#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "benders/BendersCut.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

struct FppSubproblemPropagationConstraintDescriptor {
    int scenario_id = 0;
    int u_index = -1;
    int v_index = -1;
    bool target_has_firebreak_variable = false;
    int target_y_position = -1;
};

struct FppScenarioSubproblemStructure {
    int scenario_id = 0;
    std::size_t x_variable_count = 0;
    std::size_t y_copy_variable_count = 0;
    std::size_t total_variable_count = 0;
    std::size_t y_fix_constraint_count = 0;
    std::size_t ignition_constraint_count = 0;
    std::size_t propagation_constraint_count = 0;
    std::size_t total_constraint_count = 0;
    bool x_variables_are_continuous = true;
    bool y_copy_variables_are_continuous = true;
    bool objective_is_sum_x = true;
    std::vector<FppSubproblemPropagationConstraintDescriptor> propagation_constraints;
};

struct FppSubproblemResult {
    int scenario_id = 0;
    std::string status;
    double objective_value = 0.0;
    std::vector<double> duals_for_y_copy;
    BendersCut benders_cut;
    double runtime_seconds = 0.0;
    std::size_t num_variables = 0;
    std::size_t num_constraints = 0;
    std::vector<std::string> notes;
};

FppScenarioSubproblemStructure analyze_fpp_scenario_subproblem_structure(
    const opt::OptimizationInstance& opt,
    int scenario_position);

class FppScenarioSubproblem {
public:
    FppSubproblemResult solve(
        const opt::OptimizationInstance& opt,
        int scenario_position,
        const std::vector<int>& ybar_binary,
        bool verbose) const;

    FppSubproblemResult solveFractional(
        const opt::OptimizationInstance& opt,
        int scenario_position,
        const std::vector<double>& ybar_values,
        bool verbose) const;
};

}  // namespace firebreak::benders
