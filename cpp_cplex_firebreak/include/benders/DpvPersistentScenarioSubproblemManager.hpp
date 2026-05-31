#pragma once

#include <memory>
#include <vector>

#include "benders/DpvScenarioSubproblem.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

struct DpvPersistentScenarioSubproblemDiagnostics {
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

class DpvPersistentScenarioSubproblemManager {
public:
    explicit DpvPersistentScenarioSubproblemManager(
        const opt::OptimizationInstance& opt,
        bool verbose = false);
    ~DpvPersistentScenarioSubproblemManager();

    DpvPersistentScenarioSubproblemManager(const DpvPersistentScenarioSubproblemManager&) = delete;
    DpvPersistentScenarioSubproblemManager& operator=(const DpvPersistentScenarioSubproblemManager&) = delete;
    DpvPersistentScenarioSubproblemManager(DpvPersistentScenarioSubproblemManager&&) noexcept;
    DpvPersistentScenarioSubproblemManager& operator=(DpvPersistentScenarioSubproblemManager&&) noexcept;

    SubproblemResult solveScenario(
        int scenario_position,
        const std::vector<int>& ybar_binary);

    SubproblemResult solveScenarioFractional(
        int scenario_position,
        const std::vector<double>& ybar_values);

    DpvPersistentScenarioSubproblemDiagnostics diagnostics() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace firebreak::benders
