#pragma once

#include <cstddef>
#include <limits>

#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/ModelResult.hpp"
#include "benders/FppCombinatorialBenders.hpp"
#include "benders/FppStrengthening.hpp"

namespace firebreak::benders {

struct FppBranchBendersOptions {
    double tolerance = 1.0e-6;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    bool verbose = false;
    risk::RiskMeasureConfig risk_config;
    bool use_lifted_lower_bounds = false;
    bool use_root_user_cuts = false;
    int root_user_cut_max_rounds = 1;
    double root_user_cut_tolerance = std::numeric_limits<double>::quiet_NaN();
    FppCombinatorialBendersOptions combinatorial_options;
    FppStrengtheningOptions strengthening_options;
};

struct FppBranchBendersMasterStructure {
    std::size_t y_variable_count = 0;
    std::size_t eta_variable_count = 0;
    std::size_t risk_threshold_variable_count = 0;
    std::size_t cvar_excess_variable_count = 0;
    std::size_t total_variable_count = 0;
    std::size_t budget_constraint_count = 0;
    std::size_t base_constraint_count = 0;
    std::size_t risk_constraint_count = 0;
    bool has_scenario_recourse_variables = false;
};

FppBranchBendersMasterStructure analyze_fpp_branch_benders_master_structure(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config = risk::RiskMeasureConfig());

class FppBranchBendersSolver {
public:
    solver::ModelResult solve(
        const opt::OptimizationInstance& opt,
        const FppBranchBendersOptions& options) const;
};

}  // namespace firebreak::benders
