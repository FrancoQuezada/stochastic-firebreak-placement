#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

#include "core/FirebreakSolution.hpp"
#include "core/Scenario.hpp"
#include "eval/BurnedAreaEvaluator.hpp"

namespace {

firebreak::core::Scenario make_scenario(int scenario_id, int ignition_node, const std::vector<std::pair<int, int>>& arcs) {
    firebreak::core::Scenario scenario;
    scenario.scenario_id = scenario_id;
    scenario.ignition_node = ignition_node;
    scenario.message_filename = "synthetic.csv";
    for (const auto& arc : arcs) {
        scenario.propagation_graph.add_edge(arc.first, arc.second);
    }
    return scenario;
}

void test_chain_no_firebreaks() {
    const auto scenario = make_scenario(1, 1, {{1, 2}, {2, 3}});
    const firebreak::core::FirebreakSolution firebreaks;
    const auto result = firebreak::eval::evaluate_scenario_burned_area(scenario, firebreaks);
    assert(result.burned_count == 3);
    assert((result.burned_nodes == std::vector<int>{1, 2, 3}));
}

void test_chain_firebreak_at_middle() {
    const auto scenario = make_scenario(1, 1, {{1, 2}, {2, 3}});
    const firebreak::core::FirebreakSolution firebreaks({2});
    const auto result = firebreak::eval::evaluate_scenario_burned_area(scenario, firebreaks);
    assert(result.burned_count == 1);
    assert((result.burned_nodes == std::vector<int>{1}));
}

void test_branch_firebreak_at_one_successor() {
    const auto scenario = make_scenario(1, 1, {{1, 2}, {1, 3}});
    const firebreak::core::FirebreakSolution firebreaks({3});
    const auto result = firebreak::eval::evaluate_scenario_burned_area(scenario, firebreaks);
    assert(result.burned_count == 2);
    assert((result.burned_nodes == std::vector<int>{1, 2}));
}

void test_ignition_firebreak_still_propagates_outgoing() {
    const auto scenario = make_scenario(1, 1, {{1, 2}, {2, 3}});
    const firebreak::core::FirebreakSolution firebreaks({1});
    const auto result = firebreak::eval::evaluate_scenario_burned_area(scenario, firebreaks);
    assert(result.ignition_is_firebreak);
    assert(result.burned_count == 3);
    assert((result.burned_nodes == std::vector<int>{1, 2, 3}));
}

void test_instance_expected_and_worst_10pct() {
    firebreak::core::Instance instance;
    instance.landscape_name = "synthetic";
    instance.scenarios.push_back(make_scenario(1, 1, {{1, 2}, {2, 3}}));
    instance.scenarios.push_back(make_scenario(2, 1, {{1, 2}}));

    const firebreak::core::FirebreakSolution firebreaks;
    const auto result = firebreak::eval::evaluate_instance_burned_area(instance, firebreaks);
    assert(result.number_of_scenarios == 2);
    assert(result.expected_burned_area == 2.5);
    assert(result.worst_10pct_burned_area == 3.0);
    assert(result.empirical_var_90pct_burned_area == 3.0);
    assert(result.empirical_cvar_90pct_burned_area == 3.0);
}

}  // namespace

int main() {
    test_chain_no_firebreaks();
    test_chain_firebreak_at_middle();
    test_branch_firebreak_at_one_successor();
    test_ignition_firebreak_still_propagates_outgoing();
    test_instance_expected_and_worst_10pct();
    std::cout << "All burned-area evaluator tests passed.\n";
    return 0;
}
