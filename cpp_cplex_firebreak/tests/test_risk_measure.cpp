#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "risk/RiskMeasure.hpp"

namespace {

bool near(double lhs, double rhs, double tolerance = 1.0e-9) {
    return std::fabs(lhs - rhs) <= tolerance;
}

template <typename Fn>
void assert_throws(Fn fn) {
    bool threw = false;
    try {
        fn();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_risk_type_parsing() {
    using firebreak::risk::RiskMeasureType;
    assert(firebreak::risk::parse_risk_measure_type("expected") == RiskMeasureType::Expected);
    assert(firebreak::risk::parse_risk_measure_type("cvar") == RiskMeasureType::CVaR);
    assert(firebreak::risk::parse_risk_measure_type("mean-cvar") == RiskMeasureType::MeanCVaR);
    assert(firebreak::risk::to_string(RiskMeasureType::Expected) == "expected");
    assert(firebreak::risk::to_string(RiskMeasureType::CVaR) == "cvar");
    assert(firebreak::risk::to_string(RiskMeasureType::MeanCVaR) == "mean-cvar");
    assert_throws([] {
        (void)firebreak::risk::parse_risk_measure_type("tail-risk");
    });
}

void test_config_validation() {
    firebreak::risk::RiskMeasureConfig config;
    firebreak::risk::validate_risk_measure_config(config);

    config.cvarBeta = 0.0;
    assert_throws([&] { firebreak::risk::validate_risk_measure_config(config); });
    config.cvarBeta = 1.0;
    assert_throws([&] { firebreak::risk::validate_risk_measure_config(config); });
    config.cvarBeta = 0.9;

    config.cvarLambda = -0.1;
    assert_throws([&] { firebreak::risk::validate_risk_measure_config(config); });
    config.cvarLambda = 1.1;
    assert_throws([&] { firebreak::risk::validate_risk_measure_config(config); });
    config.cvarLambda = 0.5;
    firebreak::risk::validate_risk_measure_config(config);
}

void test_weighted_expected_value() {
    const std::vector<firebreak::risk::WeightedLoss> losses = {
        {1, 1.0, 0.25},
        {2, 5.0, 0.75},
    };
    const auto metrics = firebreak::risk::compute_weighted_risk_metrics(losses, 0.5);
    assert(near(metrics.expected, 4.0));
}

void test_weighted_var_and_cvar() {
    const std::vector<firebreak::risk::WeightedLoss> losses = {
        {1, 1.0, 1.0 / 3.0},
        {2, 2.0, 1.0 / 3.0},
        {3, 10.0, 1.0 / 3.0},
    };
    const auto metrics = firebreak::risk::compute_weighted_risk_metrics(losses, 2.0 / 3.0);
    assert(near(metrics.expected, 13.0 / 3.0));
    assert(near(metrics.var, 2.0));
    assert(near(metrics.cvar, 10.0));
    assert(metrics.tail_count == 2);
}

void test_second_cvar_example() {
    const std::vector<firebreak::risk::WeightedLoss> losses = {
        {1, 0.0, 0.5},
        {2, 10.0, 0.5},
    };
    const auto metrics = firebreak::risk::compute_weighted_risk_metrics(losses, 0.5);
    assert(near(metrics.expected, 5.0));
    assert(near(metrics.var, 0.0));
    assert(near(metrics.cvar, 10.0));
}

void test_probability_normalization() {
    const std::vector<firebreak::risk::WeightedLoss> losses = {
        {1, 10.0, 25.0},
        {2, 20.0, 75.0},
    };
    const auto metrics = firebreak::risk::compute_weighted_risk_metrics(losses, 0.5);
    assert(near(metrics.expected, 17.5));
    assert(near(metrics.var, 20.0));

    const std::vector<firebreak::risk::WeightedLoss> almost_normalized = {
        {1, 1.0, 0.333333},
        {2, 2.0, 0.333333},
        {3, 3.0, 0.333333},
    };
    const auto normalized_metrics =
        firebreak::risk::compute_weighted_risk_metrics(almost_normalized, 0.5);
    assert(near(normalized_metrics.expected, 2.0));
}

void test_tied_loss_determinism() {
    const std::vector<firebreak::risk::WeightedLoss> losses_a = {
        {2, 5.0, 0.25},
        {1, 5.0, 0.25},
        {3, 10.0, 0.50},
    };
    const std::vector<firebreak::risk::WeightedLoss> losses_b = {
        {3, 10.0, 0.50},
        {1, 5.0, 0.25},
        {2, 5.0, 0.25},
    };
    const auto metrics_a = firebreak::risk::compute_weighted_risk_metrics(losses_a, 0.5);
    const auto metrics_b = firebreak::risk::compute_weighted_risk_metrics(losses_b, 0.5);
    assert(near(metrics_a.var, metrics_b.var));
    assert(near(metrics_a.cvar, metrics_b.cvar));
    assert(metrics_a.tail_count == metrics_b.tail_count);
}

void test_uniform_convenience() {
    const std::vector<std::pair<int, double>> losses = {
        {1, 1.0},
        {2, 2.0},
        {3, 10.0},
    };
    const auto metrics = firebreak::risk::compute_uniform_risk_metrics(losses, 2.0 / 3.0);
    assert(near(metrics.expected, 13.0 / 3.0));
    assert(near(metrics.var, 2.0));
    assert(near(metrics.cvar, 10.0));
}

void test_invalid_inputs() {
    assert_throws([] {
        (void)firebreak::risk::compute_weighted_risk_metrics({}, 0.9);
    });
    assert_throws([] {
        (void)firebreak::risk::compute_weighted_risk_metrics({{1, 1.0, 1.0}}, 0.0);
    });
    assert_throws([] {
        (void)firebreak::risk::compute_weighted_risk_metrics({{1, 1.0, -1.0}}, 0.9);
    });
    assert_throws([] {
        (void)firebreak::risk::compute_weighted_risk_metrics({{1, 1.0, 0.0}}, 0.9);
    });
    assert_throws([] {
        (void)firebreak::risk::compute_weighted_risk_metrics({{1, NAN, 1.0}}, 0.9);
    });
}

}  // namespace

int main() {
    test_risk_type_parsing();
    test_config_validation();
    test_weighted_expected_value();
    test_weighted_var_and_cvar();
    test_second_cvar_example();
    test_probability_normalization();
    test_tied_loss_determinism();
    test_uniform_convenience();
    test_invalid_inputs();
    std::cout << "All risk-measure tests passed.\n";
    return 0;
}
