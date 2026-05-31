#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "benders/BendersCut.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

struct DpvScenarioSubproblemStructure {
    int scenario_id = 0;
    std::size_t x_variable_count = 0;
    std::size_t z_variable_count = 0;
    std::size_t y_copy_variable_count = 0;
    std::size_t total_variable_count = 0;
    std::size_t y_fix_constraint_count = 0;
    std::size_t ignition_constraint_count = 0;
    std::size_t propagation_constraint_count = 0;
    std::size_t linearization_constraint_count = 0;
    std::size_t total_constraint_count = 0;
};

struct SubproblemResult {
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

DpvScenarioSubproblemStructure analyze_dpv_scenario_subproblem_structure(
    const opt::OptimizationInstance& opt,
    int scenario_position);

class DpvScenarioSubproblem {
public:
    SubproblemResult solve(
        const opt::OptimizationInstance& opt,
        int scenario_position,
        const std::vector<int>& ybar_binary,
        bool verbose) const;

    SubproblemResult solveFractional(
        const opt::OptimizationInstance& opt,
        int scenario_position,
        const std::vector<double>& ybar_values,
        bool verbose) const;
};

}  // namespace firebreak::benders
