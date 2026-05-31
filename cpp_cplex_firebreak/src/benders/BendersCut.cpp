#include "benders/BendersCut.hpp"

#include <stdexcept>

namespace firebreak::benders {

double BendersCut::evaluateAt(const std::vector<double>& y_values_by_compact_index) const {
    double value = rhs_constant;
    for (const auto& [compact_index, coefficient] : coefficients_by_compact_index) {
        if (compact_index < 0 || compact_index >= static_cast<int>(y_values_by_compact_index.size())) {
            throw std::runtime_error("Benders cut coefficient references an out-of-range compact y index.");
        }
        value += coefficient * y_values_by_compact_index[static_cast<std::size_t>(compact_index)];
    }
    return value;
}

double BendersCut::evaluateAt(const std::vector<int>& y_values_by_compact_index) const {
    std::vector<double> as_double;
    as_double.reserve(y_values_by_compact_index.size());
    for (const int value : y_values_by_compact_index) {
        as_double.push_back(static_cast<double>(value));
    }
    return evaluateAt(as_double);
}

double BendersCut::violationAt(double eta_value, const std::vector<double>& y_values_by_compact_index) const {
    return evaluateAt(y_values_by_compact_index) - eta_value;
}

double BendersCut::violationAt(double eta_value, const std::vector<int>& y_values_by_compact_index) const {
    std::vector<double> as_double;
    as_double.reserve(y_values_by_compact_index.size());
    for (const int value : y_values_by_compact_index) {
        as_double.push_back(static_cast<double>(value));
    }
    return violationAt(eta_value, as_double);
}

}  // namespace firebreak::benders
