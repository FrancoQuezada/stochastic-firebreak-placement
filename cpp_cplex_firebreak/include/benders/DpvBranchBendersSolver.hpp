#pragma once

#include <cstddef>
#include <limits>

#include "opt/OptimizationInstance.hpp"
#include "solver/ModelResult.hpp"
#include "solver/WarmStart.hpp"

namespace firebreak::benders {

struct DpvBranchBendersOptions {
    double tolerance = 1.0e-6;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    bool verbose = false;
    bool use_lifted_lower_bounds = false;
    bool use_root_user_cuts = false;
    int root_user_cut_max_rounds = 1;
    double root_user_cut_tolerance = std::numeric_limits<double>::quiet_NaN();
    const solver::WarmStart* warm_start = nullptr;
};

struct DpvBranchBendersMasterStructure {
    std::size_t y_variable_count = 0;
    std::size_t eta_variable_count = 0;
    std::size_t total_variable_count = 0;
    std::size_t budget_constraint_count = 0;
    std::size_t base_constraint_count = 0;
    bool has_scenario_recourse_variables = false;
};

DpvBranchBendersMasterStructure analyze_dpv_branch_benders_master_structure(
    const opt::OptimizationInstance& opt);

class DpvBranchBendersSolver {
public:
    solver::ModelResult solve(
        const opt::OptimizationInstance& opt,
        const DpvBranchBendersOptions& options) const;
};

}  // namespace firebreak::benders
