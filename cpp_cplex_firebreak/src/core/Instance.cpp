#include "core/Instance.hpp"

#include <algorithm>

namespace firebreak::core {

std::size_t Instance::num_scenarios() const {
    return scenarios.size();
}

std::size_t Instance::num_available_nodes() const {
    return available_nodes.size();
}

int Instance::min_scenario_id() const {
    if (scenarios.empty()) {
        return 0;
    }
    const auto it = std::min_element(scenarios.begin(), scenarios.end(), [](const Scenario& a, const Scenario& b) {
        return a.scenario_id < b.scenario_id;
    });
    return it->scenario_id;
}

int Instance::max_scenario_id() const {
    if (scenarios.empty()) {
        return 0;
    }
    const auto it = std::max_element(scenarios.begin(), scenarios.end(), [](const Scenario& a, const Scenario& b) {
        return a.scenario_id < b.scenario_id;
    });
    return it->scenario_id;
}

}  // namespace firebreak::core
