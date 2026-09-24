#pragma once

#include "opt/OptimizationInstance.hpp"
#include "opt/WeightedDpvScoring.hpp"
#include "solver/ModelResult.hpp"
#include "solver/WarmStart.hpp"

namespace firebreak::benders {

struct DpvBendersOptions {
    int max_iterations = 20;
    double tolerance = 1.0e-6;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    bool verbose = false;
    bool use_lifted_lower_bounds = false;
    opt::WeightedDpvIgnitionPolicy dpv_ignition_policy =
        opt::WeightedDpvIgnitionPolicy::FppIgnitionNoProtection;
    const solver::WarmStart* warm_start = nullptr;
};

class DpvBendersSolver {
public:
    solver::ModelResult solve(
        const opt::OptimizationInstance& opt,
        const DpvBendersOptions& options) const;
};

}  // namespace firebreak::benders
