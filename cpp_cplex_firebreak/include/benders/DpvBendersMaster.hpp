#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/DpvLiftedLowerBound.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

struct MasterSolveResult {
    std::string status;
    double objective_value = 0.0;
    double best_bound = 0.0;
    double mip_gap = 0.0;
    double runtime_seconds = 0.0;
    int solver_status_code = 0;
    std::vector<int> y_values;
    std::vector<double> eta_values;
};

class DpvBendersMaster {
public:
    DpvBendersMaster();
    ~DpvBendersMaster();

    DpvBendersMaster(const DpvBendersMaster&) = delete;
    DpvBendersMaster& operator=(const DpvBendersMaster&) = delete;

    void initialize(const opt::OptimizationInstance& opt);
    void setParameters(double time_limit_seconds, double mip_gap, int threads, bool verbose);
    void setWarmStart(const std::vector<int>& y_start_binary);
    MasterSolveResult solve();
    std::vector<int> getCurrentY() const;
    std::vector<double> getEtaValues() const;
    void addCut(int scenario_position, const BendersCut& cut);
    double getObjective() const;
    double getBestBound() const;
    double getMipGap() const;
    double getRuntime() const;
    std::vector<int> getSelectedFirebreaks() const;
    std::vector<int> getSelectedFirebreakOriginalNodes() const;
    void addLiftedLowerBound(
        int scenario_position,
        const DpvLiftedLowerBoundInequality& inequality);
    std::size_t getNumVariables() const;
    std::size_t getNumConstraints() const;
    int getCutCount() const;
    int getLiftedLowerBoundCount() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace firebreak::benders
