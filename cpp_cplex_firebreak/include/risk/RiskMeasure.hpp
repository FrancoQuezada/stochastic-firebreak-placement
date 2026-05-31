#pragma once

#include <string>
#include <utility>
#include <vector>

namespace firebreak::risk {

enum class RiskMeasureType {
    Expected,
    CVaR,
    MeanCVaR,
};

struct RiskMeasureConfig {
    RiskMeasureType type = RiskMeasureType::Expected;
    double cvarBeta = 0.9;
    double cvarLambda = 1.0;
};

struct WeightedLoss {
    int scenario_id = 0;
    double loss = 0.0;
    double probability = 0.0;
};

struct RiskMetrics {
    double expected = 0.0;
    double var = 0.0;
    double cvar = 0.0;
    double beta = 0.9;
    int tail_count = 0;
};

RiskMeasureType parse_risk_measure_type(const std::string& value);

std::string to_string(RiskMeasureType type);

void validate_risk_measure_config(const RiskMeasureConfig& config);

RiskMetrics compute_weighted_risk_metrics(
    const std::vector<WeightedLoss>& losses,
    double beta);

RiskMetrics compute_uniform_risk_metrics(
    const std::vector<std::pair<int, double>>& scenario_losses,
    double beta);

}  // namespace firebreak::risk
