#pragma once

#include <string>
#include <utility>
#include <vector>

namespace firebreak::benders {

struct BendersCut {
    int scenario_id = 0;
    double rhs_constant = 0.0;
    std::vector<std::pair<int, double>> coefficients_by_compact_index;
    double subproblem_objective = 0.0;
    std::vector<std::pair<int, double>> ybar_compact_values;
    double max_cut_violation = 0.0;
    std::vector<std::string> notes;

    double evaluateAt(const std::vector<double>& y_values_by_compact_index) const;
    double evaluateAt(const std::vector<int>& y_values_by_compact_index) const;
    double violationAt(double eta_value, const std::vector<double>& y_values_by_compact_index) const;
    double violationAt(double eta_value, const std::vector<int>& y_values_by_compact_index) const;
};

}  // namespace firebreak::benders
