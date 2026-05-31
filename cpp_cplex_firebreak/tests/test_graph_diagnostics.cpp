#include <cassert>
#include <iostream>
#include <vector>

#include "analysis/GraphDiagnostics.hpp"
#include "core/Scenario.hpp"

namespace {

firebreak::core::Scenario make_scenario(int ignition, const std::vector<std::pair<int, int>>& arcs) {
    firebreak::core::Scenario scenario;
    scenario.scenario_id = 1;
    scenario.ignition_node = ignition;
    scenario.message_filename = "synthetic.csv";
    for (const auto& arc : arcs) {
        scenario.propagation_graph.add_edge(arc.first, arc.second);
    }
    return scenario;
}

void test_rooted_arborescence() {
    const auto scenario = make_scenario(1, {{1, 2}, {2, 3}});
    const auto d = firebreak::analysis::analyze_scenario_graph(scenario);

    assert(d.is_rooted_arborescence);
    assert(d.is_dag);
    assert(!d.has_cycles);
    assert(d.fully_reachable_from_ignition);
    assert(d.arc_excess == 0);
    assert(d.classification == "rooted_arborescence");
}

void test_dag_not_tree_multiple_parents() {
    const auto scenario = make_scenario(1, {{1, 2}, {1, 3}, {2, 3}});
    const auto d = firebreak::analysis::analyze_scenario_graph(scenario);

    assert(d.is_dag);
    assert(!d.is_rooted_arborescence);
    assert(d.in_degree_greater_than_one_count > 0);
    assert(d.has_multiple_parent_nodes);
    assert(d.arc_excess > 0);
    assert(d.classification == "dag_not_tree");
}

void test_cycle() {
    const auto scenario = make_scenario(1, {{1, 2}, {2, 3}, {3, 1}});
    const auto d = firebreak::analysis::analyze_scenario_graph(scenario);

    assert(d.has_cycles);
    assert(!d.is_dag);
    assert(d.classification == "general_directed_graph");
}

void test_not_fully_reachable() {
    const auto scenario = make_scenario(1, {{1, 2}, {3, 4}});
    const auto d = firebreak::analysis::analyze_scenario_graph(scenario);

    assert(!d.fully_reachable_from_ignition);
    assert(d.weak_component_count > 1);
    assert(d.classification == "not_fully_reachable_from_ignition");
}

void test_empty_graph() {
    firebreak::core::Scenario scenario;
    scenario.scenario_id = 1;
    scenario.ignition_node = 1;
    scenario.message_filename = "empty.csv";

    const auto d = firebreak::analysis::analyze_scenario_graph(scenario);
    assert(d.classification == "empty_or_invalid");
    assert(d.observed_node_count == 0);
    assert(d.directed_arc_count == 0);
}

void test_classification_ratio_summary() {
    const auto rooted = make_scenario(1, {{1, 2}, {2, 3}});
    const auto dag = make_scenario(1, {{1, 2}, {1, 3}, {2, 3}});
    const auto summary = firebreak::analysis::graph_classification_ratio_summary({rooted, dag});

    assert(summary.find("RT=0.500000") != std::string::npos);
    assert(summary.find("ADAG=0.500000") != std::string::npos);
    assert(summary.find("GDG=0.000000") != std::string::npos);
    assert(summary.find("NFR=0.000000") != std::string::npos);
    assert(summary.find("EMPTY=0.000000") != std::string::npos);
}

}  // namespace

int main() {
    test_rooted_arborescence();
    test_dag_not_tree_multiple_parents();
    test_cycle();
    test_not_fully_reachable();
    test_empty_graph();
    test_classification_ratio_summary();
    std::cout << "All graph diagnostics tests passed.\n";
    return 0;
}
