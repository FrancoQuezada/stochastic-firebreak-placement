#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

void test_sample_size_rounding() {
    const std::vector<int> scenario_counts = {1, 2, 3, 10, 100};
    const std::vector<double> ratios = {0.01, 0.1, 0.5, 1.0};
    for (const int count : scenario_counts) {
        for (const double ratio : ratios) {
            const int expected = std::max(
                1,
                static_cast<int>(std::ceil(ratio * static_cast<double>(count))));
            const int actual = firebreak::benders::fpp_combinatorial_realized_sample_size(
                static_cast<std::size_t>(count),
                ratio);
            assert(actual == expected);
        }
    }
}

void test_zero_scenarios() {
    assert(firebreak::benders::fpp_combinatorial_realized_sample_size(0, 0.5) == 0);
}

void expect_invalid(double ratio) {
    bool threw = false;
    try {
        (void)firebreak::benders::fpp_combinatorial_realized_sample_size(10, ratio);
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("(0, 1]") != std::string::npos;
    }
    assert(threw);
}

void test_invalid_ratios() {
    expect_invalid(0.0);
    expect_invalid(-0.1);
    expect_invalid(1.00001);
}

void test_option_validation() {
    firebreak::benders::FppCombinatorialBendersOptions options;
    options.enabled = true;
    options.cut_sampling_ratio = 0.1;
    firebreak::benders::validate_fpp_phase6c2c_weighted_combinatorial_mode(
        options,
        false,
        false,
        firebreak::benders::FppStrengtheningOptions());

    options.cut_sampling_ratio = 1.1;
    bool threw = false;
    try {
        firebreak::benders::validate_fpp_phase6c2c_weighted_combinatorial_mode(
            options,
            false,
            false,
            firebreak::benders::FppStrengtheningOptions());
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("Phase 6C2C") != std::string::npos ||
                std::string(error.what()).find("(0,1]") != std::string::npos ||
                std::string(error.what()).find("(0, 1]") != std::string::npos;
    }
    assert(threw);
}

}  // namespace

int main() {
    test_sample_size_rounding();
    test_zero_scenarios();
    test_invalid_ratios();
    test_option_validation();
    std::cout << "All weighted FPP combinatorial sampling tests passed.\n";
    return 0;
}
