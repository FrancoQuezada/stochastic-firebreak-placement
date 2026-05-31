#include <cassert>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/BendersCut.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-9);
}

void test_cut_is_tight_at_ybar() {
    firebreak::benders::BendersCut cut;
    cut.scenario_id = 7;
    cut.subproblem_objective = 10.0;
    cut.coefficients_by_compact_index = {
        {1, -2.0},
        {3, 4.0},
    };
    cut.ybar_compact_values = {
        {1, 1.0},
        {3, 0.0},
    };
    cut.rhs_constant = cut.subproblem_objective - (-2.0 * 1.0 + 4.0 * 0.0);

    std::vector<double> ybar = {0.0, 1.0, 0.0, 0.0};
    assert_close(cut.evaluateAt(ybar), cut.subproblem_objective);
}

void test_violation_computation() {
    firebreak::benders::BendersCut cut;
    cut.rhs_constant = 3.0;
    cut.coefficients_by_compact_index = {
        {0, 2.0},
        {2, -1.0},
    };

    std::vector<int> y = {1, 0, 1};
    assert_close(cut.evaluateAt(y), 4.0);
    assert_close(cut.violationAt(3.25, y), 0.75);
}

void test_stored_cut_algebra_matches_dual_form() {
    const double q_ybar = 12.0;
    const std::vector<int> ybar = {1, 0, 1, 0};
    const std::vector<std::pair<int, double>> duals = {
        {0, 1.5},
        {1, -2.0},
        {2, 0.25},
        {3, 3.0},
    };

    double dual_dot_ybar = 0.0;
    for (const auto& [compact_index, dual] : duals) {
        dual_dot_ybar += dual * ybar[static_cast<std::size_t>(compact_index)];
    }

    firebreak::benders::BendersCut cut;
    cut.subproblem_objective = q_ybar;
    cut.coefficients_by_compact_index = duals;
    cut.rhs_constant = q_ybar - dual_dot_ybar;

    assert_close(cut.rhs_constant, 10.25);
    assert_close(cut.evaluateAt(ybar), q_ybar);

    const std::vector<int> other_y = {0, 1, 1, 1};
    const double expected =
        q_ybar +
        1.5 * (0.0 - 1.0) +
        (-2.0) * (1.0 - 0.0) +
        0.25 * (1.0 - 1.0) +
        3.0 * (1.0 - 0.0);
    assert_close(cut.evaluateAt(other_y), expected);
}

}  // namespace

int main() {
    test_cut_is_tight_at_ybar();
    test_violation_computation();
    test_stored_cut_algebra_matches_dual_form();
    std::cout << "All Benders cut tests passed.\n";
    return 0;
}
