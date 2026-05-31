#include "solver/CplexEnvironment.hpp"

namespace firebreak::solver {

bool cplex_support_enabled() {
#ifdef FIREBREAK_WITH_CPLEX
    return true;
#else
    return false;
#endif
}

std::string cplex_unavailable_message() {
    return "CPLEX support was not enabled at build time.";
}

std::string cplex_build_mode_message() {
#ifdef FIREBREAK_WITH_CPLEX
    return "CPLEX support is enabled.";
#else
    return "CPLEX support is disabled.";
#endif
}

}  // namespace firebreak::solver

