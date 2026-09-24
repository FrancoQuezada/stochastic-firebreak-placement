#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/FppLiftedLowerBound.hpp"
#include "benders/FppStrengthening.hpp"
#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"

namespace firebreak::benders {

struct FppMasterSolveResult {
    std::string status;
    double objective_value = 0.0;
    double best_bound = 0.0;
    double mip_gap = 0.0;
    double runtime_seconds = 0.0;
    int solver_status_code = 0;
    std::vector<int> y_values;
    std::vector<double> eta_values;
    double risk_threshold_value = 0.0;
    std::vector<double> cvar_excess_values;
};

class FppBendersMaster {
public:
    FppBendersMaster();
    ~FppBendersMaster();

    FppBendersMaster(const FppBendersMaster&) = delete;
    FppBendersMaster& operator=(const FppBendersMaster&) = delete;

    void initialize(
        const opt::OptimizationInstance& opt,
        const risk::RiskMeasureConfig& risk_config = risk::RiskMeasureConfig());
    void setParameters(double time_limit_seconds, double mip_gap, int threads, bool verbose);
    FppMasterSolveResult solve();
    std::vector<int> getCurrentY() const;
    std::vector<double> getEtaValues() const;
    void addCut(int scenario_position, const BendersCut& cut);
    void addLiftedLowerBound(
        int scenario_position,
        const FppLiftedLowerBoundInequality& inequality);
    double addCoverageLlbi(const FppCoverageLlbiData& data);
    double addPathLlbi(const FppPathLlbiData& data);
    double getObjective() const;
    double getBestBound() const;
    double getMipGap() const;
    double getRuntime() const;
    std::vector<int> getSelectedFirebreaks() const;
    std::vector<int> getSelectedFirebreakOriginalNodes() const;
    std::size_t getNumVariables() const;
    std::size_t getNumConstraints() const;
    int getCutCount() const;
    int getLiftedLowerBoundCount() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace firebreak::benders
