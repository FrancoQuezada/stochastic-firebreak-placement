#pragma once

#include <memory>
#include <vector>

#include "benders/FppScenarioSubproblem.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

struct FppPersistentScenarioSubproblemDiagnostics {
    int scenario_count = 0;
    bool persistent_subproblems_enabled = false;
    int subproblem_model_build_count = 0;
    int subproblem_fixed_y_update_count = 0;
    int subproblem_solve_count = 0;
    int subproblem_model_rebuild_count = 0;
    double subproblem_total_build_time = 0.0;
    double subproblem_total_update_time = 0.0;
    double subproblem_total_solve_time = 0.0;
    double subproblem_average_update_time = 0.0;
    double subproblem_average_solve_time = 0.0;
};

class FppPersistentScenarioSubproblemManager {
public:
    explicit FppPersistentScenarioSubproblemManager(
        const opt::OptimizationInstance& opt,
        bool verbose = false);
    ~FppPersistentScenarioSubproblemManager();

    FppPersistentScenarioSubproblemManager(const FppPersistentScenarioSubproblemManager&) = delete;
    FppPersistentScenarioSubproblemManager& operator=(const FppPersistentScenarioSubproblemManager&) = delete;
    FppPersistentScenarioSubproblemManager(FppPersistentScenarioSubproblemManager&&) noexcept;
    FppPersistentScenarioSubproblemManager& operator=(FppPersistentScenarioSubproblemManager&&) noexcept;

    FppSubproblemResult solveScenario(
        int scenario_position,
        const std::vector<int>& ybar_binary);

    FppSubproblemResult solveScenarioFractional(
        int scenario_position,
        const std::vector<double>& ybar_values);

    FppPersistentScenarioSubproblemDiagnostics diagnostics() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace firebreak::benders
