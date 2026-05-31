#pragma once

#include <string>

namespace firebreak::solver {

bool cplex_support_enabled();
std::string cplex_unavailable_message();
std::string cplex_build_mode_message();

}  // namespace firebreak::solver

