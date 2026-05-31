#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <vector>

#include "heuristics/CumulativePropagationGraph.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0;
    opt.n_cells = 4;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_original_nodes = {1, 2, 3, 4};
    opt.eligible_indices = {0, 1, 2, 3};

    firebreak::opt::OptimizationScenario s1;
    s1.scenario_id = 1;
    s1.probability = 0.5;
    s1.ignition_index = 0;
    s1.ignition_original_node = 1;
    s1.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    s1.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    s1.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    firebreak::opt::OptimizationScenario s2;
    s2.scenario_id = 2;
    s2.probability = 0.5;
    s2.ignition_index = 0;
    s2.ignition_original_node = 1;
    s2.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    s2.arcs.push_back(firebreak::opt::CompactArc{2, 3, 3, 4});

    opt.scenarios = {s1, s2};
    opt.scenario_probabilities = {0.5, 0.5};
    opt.total_arcs = s1.arcs.size() + s2.arcs.size();
    return opt;
}

void test_arc_frequency_accumulation() {
    firebreak::heuristics::CumulativePropagationGraph graph;
    graph.buildFromOptimizationInstance(make_instance());

    assert(graph.numNodes() == 4);
    assert(graph.numArcs() == 3);
    assert(std::fabs(graph.arcWeight(0, 1) - 2.0) < 1.0e-9);
    assert(std::fabs(graph.inverseArcWeight(0, 1) - 0.5) < 1.0e-9);
    assert(std::fabs(graph.arcWeight(1, 2) - 1.0) < 1.0e-9);
    assert(std::fabs(graph.arcWeight(2, 3) - 1.0) < 1.0e-9);
}

void test_successors_and_predecessors() {
    firebreak::heuristics::CumulativePropagationGraph graph;
    graph.buildFromOptimizationInstance(make_instance());

    assert((graph.successors(0) == std::vector<int>{1}));
    assert((graph.successors(1) == std::vector<int>{2}));
    assert((graph.predecessors(1) == std::vector<int>{0}));
    assert((graph.predecessors(2) == std::vector<int>{1}));
    assert((graph.predecessors(3) == std::vector<int>{2}));
}

void test_reachable_respects_blocked_nodes() {
    firebreak::heuristics::CumulativePropagationGraph graph;
    graph.buildFromOptimizationInstance(make_instance());

    assert((graph.reachableFrom(0, {}) == std::vector<int>{0, 1, 2, 3}));

    const std::unordered_set<int> blocked{1};
    assert((graph.reachableFrom(0, blocked) == std::vector<int>{0}));
}

}  // namespace

int main() {
    test_arc_frequency_accumulation();
    test_successors_and_predecessors();
    test_reachable_respects_blocked_nodes();
    std::cout << "All cumulative propagation graph tests passed.\n";
    return 0;
}
