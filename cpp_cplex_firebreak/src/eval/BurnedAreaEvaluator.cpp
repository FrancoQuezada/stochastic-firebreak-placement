#include "eval/BurnedAreaEvaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "risk/RiskMeasure.hpp"

namespace firebreak::eval {

ScenarioBurnedAreaResult evaluate_scenario_burned_area(
    const core::Scenario& scenario,
    const core::FirebreakSolution& firebreaks) {
    if (scenario.ignition_node <= 0) {
        throw std::runtime_error("Scenario has an invalid ignition node.");
    }

    ScenarioBurnedAreaResult result;
    result.scenario_id = scenario.scenario_id;
    result.ignition_node = scenario.ignition_node;
    result.ignition_is_firebreak = firebreaks.contains(scenario.ignition_node);
    result.message_filename = scenario.message_filename;

    std::queue<int> frontier;
    std::unordered_set<int> burned;
    burned.insert(scenario.ignition_node);
    frontier.push(scenario.ignition_node);

    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();

        for (const int next : scenario.graph().successors(current)) {
            if (firebreaks.contains(next)) {
                continue;
            }
            if (burned.insert(next).second) {
                frontier.push(next);
            }
        }
    }

    result.burned_nodes.assign(burned.begin(), burned.end());
    std::sort(result.burned_nodes.begin(), result.burned_nodes.end());
    result.burned_count = static_cast<int>(result.burned_nodes.size());
    return result;
}

InstanceBurnedAreaResult evaluate_instance_burned_area(
    const core::Instance& instance,
    const core::FirebreakSolution& firebreaks) {
    if (instance.scenarios.empty()) {
        throw std::runtime_error("Cannot evaluate burned area for an instance with no scenarios.");
    }

    const auto start = std::chrono::steady_clock::now();

    InstanceBurnedAreaResult result;
    result.number_of_scenarios = static_cast<int>(instance.scenarios.size());
    result.firebreak_count = static_cast<int>(firebreaks.size());
    result.per_scenario_results.reserve(instance.scenarios.size());

    double total_burned = 0.0;
    std::vector<int> burned_counts;
    burned_counts.reserve(instance.scenarios.size());
    for (const auto& scenario : instance.scenarios) {
        auto scenario_result = evaluate_scenario_burned_area(scenario, firebreaks);
        total_burned += scenario_result.burned_count;
        burned_counts.push_back(scenario_result.burned_count);
        result.per_scenario_results.push_back(std::move(scenario_result));
    }

    result.expected_burned_area = total_burned / static_cast<double>(result.number_of_scenarios);

    std::sort(burned_counts.begin(), burned_counts.end(), std::greater<int>());
    const int worst_count = std::max(1, static_cast<int>(std::ceil(0.10 * result.number_of_scenarios)));
    double worst_total = 0.0;
    for (int i = 0; i < worst_count; ++i) {
        worst_total += burned_counts[static_cast<std::size_t>(i)];
    }
    result.worst_10pct_burned_area = worst_total / static_cast<double>(worst_count);

    std::vector<std::pair<int, double>> burned_area_losses;
    burned_area_losses.reserve(result.per_scenario_results.size());
    for (const auto& scenario_result : result.per_scenario_results) {
        burned_area_losses.push_back({
            scenario_result.scenario_id,
            static_cast<double>(scenario_result.burned_count),
        });
    }
    const auto risk_metrics = risk::compute_uniform_risk_metrics(burned_area_losses, 0.9);
    result.empirical_var_90pct_burned_area = risk_metrics.var;
    result.empirical_cvar_90pct_burned_area = risk_metrics.cvar;

    const auto end = std::chrono::steady_clock::now();
    result.total_runtime_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

}  // namespace firebreak::eval
