#pragma once

#include <string>
#include <vector>

#include "core/FirebreakSolution.hpp"
#include "core/Instance.hpp"
#include "core/Scenario.hpp"

namespace firebreak::eval {

struct ScenarioBurnedAreaResult {
    int scenario_id = 0;
    int ignition_node = 0;
    int burned_count = 0;
    std::vector<int> burned_nodes;
    bool ignition_is_firebreak = false;
    std::string message_filename;
};

struct InstanceBurnedAreaResult {
    int number_of_scenarios = 0;
    int firebreak_count = 0;
    double expected_burned_area = 0.0;
    double worst_10pct_burned_area = 0.0;
    double empirical_var_90pct_burned_area = 0.0;
    double empirical_cvar_90pct_burned_area = 0.0;
    double total_runtime_seconds = 0.0;
    std::vector<ScenarioBurnedAreaResult> per_scenario_results;
};

ScenarioBurnedAreaResult evaluate_scenario_burned_area(
    const core::Scenario& scenario,
    const core::FirebreakSolution& firebreaks);

InstanceBurnedAreaResult evaluate_instance_burned_area(
    const core::Instance& instance,
    const core::FirebreakSolution& firebreaks);

}  // namespace firebreak::eval
