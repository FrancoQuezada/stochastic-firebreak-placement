#pragma once

#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/ModelResult.hpp"

namespace firebreak::benders {

struct FppBendersOptions {
    int max_iterations = 20;
    double tolerance = 1.0e-6;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    bool verbose = false;
    risk::RiskMeasureConfig risk_config;
    bool use_lifted_lower_bounds = false;
};

class FppBendersSolver {
public:
    solver::ModelResult solve(
        const opt::OptimizationInstance& opt,
        const FppBendersOptions& options) const;
};

}  // namespace firebreak::benders
