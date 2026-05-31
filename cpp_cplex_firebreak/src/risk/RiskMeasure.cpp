#include "risk/RiskMeasure.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace firebreak::risk {

namespace {

void validate_beta(double beta) {
    if (!std::isfinite(beta) || beta <= 0.0 || beta >= 1.0) {
        throw std::runtime_error("CVaR beta must satisfy 0.0 < beta < 1.0.");
    }
}

struct NormalizedLoss {
    int scenario_id = 0;
    double loss = 0.0;
    double probability = 0.0;
};

}  // namespace

RiskMeasureType parse_risk_measure_type(const std::string& value) {
    if (value == "expected") {
        return RiskMeasureType::Expected;
    }
    if (value == "cvar") {
        return RiskMeasureType::CVaR;
    }
    if (value == "mean-cvar") {
        return RiskMeasureType::MeanCVaR;
    }
    throw std::runtime_error(
        "Unsupported risk measure '" + value + "'. Expected one of: expected, cvar, mean-cvar.");
}

std::string to_string(RiskMeasureType type) {
    switch (type) {
        case RiskMeasureType::Expected:
            return "expected";
        case RiskMeasureType::CVaR:
            return "cvar";
        case RiskMeasureType::MeanCVaR:
            return "mean-cvar";
    }
    throw std::runtime_error("Unsupported risk measure enum value.");
}

void validate_risk_measure_config(const RiskMeasureConfig& config) {
    (void)to_string(config.type);
    validate_beta(config.cvarBeta);
    if (!std::isfinite(config.cvarLambda) ||
        config.cvarLambda < 0.0 ||
        config.cvarLambda > 1.0) {
        throw std::runtime_error("CVaR lambda must satisfy 0.0 <= lambda <= 1.0.");
    }
}

RiskMetrics compute_weighted_risk_metrics(
    const std::vector<WeightedLoss>& losses,
    double beta) {
    validate_beta(beta);
    if (losses.empty()) {
        throw std::runtime_error("Cannot compute risk metrics for an empty loss vector.");
    }

    double total_probability = 0.0;
    for (const auto& entry : losses) {
        if (!std::isfinite(entry.loss)) {
            throw std::runtime_error("Risk metric loss values must be finite.");
        }
        if (!std::isfinite(entry.probability) || entry.probability < 0.0) {
            throw std::runtime_error("Risk metric probabilities must be finite and nonnegative.");
        }
        total_probability += entry.probability;
    }
    if (total_probability <= 0.0) {
        throw std::runtime_error("Risk metric probabilities must have positive total mass.");
    }

    std::vector<NormalizedLoss> normalized;
    normalized.reserve(losses.size());
    for (const auto& entry : losses) {
        normalized.push_back({
            entry.scenario_id,
            entry.loss,
            entry.probability / total_probability,
        });
    }
    std::sort(
        normalized.begin(),
        normalized.end(),
        [](const NormalizedLoss& lhs, const NormalizedLoss& rhs) {
            if (lhs.loss != rhs.loss) {
                return lhs.loss < rhs.loss;
            }
            return lhs.scenario_id < rhs.scenario_id;
        });

    RiskMetrics metrics;
    metrics.beta = beta;
    for (const auto& entry : normalized) {
        metrics.expected += entry.probability * entry.loss;
    }

    double cumulative_probability = 0.0;
    metrics.var = normalized.back().loss;
    for (const auto& entry : normalized) {
        cumulative_probability += entry.probability;
        if (cumulative_probability + 1.0e-12 >= beta) {
            metrics.var = entry.loss;
            break;
        }
    }

    double excess_expectation = 0.0;
    for (const auto& entry : normalized) {
        if (entry.loss >= metrics.var && entry.probability > 0.0) {
            ++metrics.tail_count;
        }
        excess_expectation += entry.probability * std::max(0.0, entry.loss - metrics.var);
    }
    metrics.cvar = metrics.var + excess_expectation / (1.0 - beta);
    return metrics;
}

RiskMetrics compute_uniform_risk_metrics(
    const std::vector<std::pair<int, double>>& scenario_losses,
    double beta) {
    if (scenario_losses.empty()) {
        throw std::runtime_error("Cannot compute uniform risk metrics for an empty loss vector.");
    }
    const double probability = 1.0 / static_cast<double>(scenario_losses.size());
    std::vector<WeightedLoss> weighted_losses;
    weighted_losses.reserve(scenario_losses.size());
    for (const auto& [scenario_id, loss] : scenario_losses) {
        weighted_losses.push_back({scenario_id, loss, probability});
    }
    return compute_weighted_risk_metrics(weighted_losses, beta);
}

}  // namespace firebreak::risk
