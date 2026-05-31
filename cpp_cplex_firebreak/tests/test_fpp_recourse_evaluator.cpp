#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/FirebreakSolution.hpp"
#include "core/Instance.hpp"
#include "eval/BurnedAreaEvaluator.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/FppSaaCplexModel.hpp"

namespace {

firebreak::opt::OptimizationInstance make_opt_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    const std::vector<int>& eligible_compact_nodes = {}) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = static_cast<int>(original_nodes.size());
    opt.node_mapper.build_from_nodes(original_nodes);

    if (eligible_compact_nodes.empty()) {
        for (const int original_node : original_nodes) {
            opt.eligible_original_nodes.push_back(original_node);
            opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
        }
    } else {
        opt.eligible_indices = eligible_compact_nodes;
        for (const int compact : eligible_compact_nodes) {
            opt.eligible_original_nodes.push_back(opt.node_mapper.to_node(compact));
        }
    }

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = original_nodes.front();
    scenario.message_filename = "synthetic.csv";
    scenario.arcs = arcs;
    for (int i = 0; i < static_cast<int>(original_nodes.size()); ++i) {
        scenario.observed_node_indices.push_back(i);
    }

    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

firebreak::core::Scenario make_core_scenario(
    int scenario_id,
    int ignition_node,
    const std::vector<std::pair<int, int>>& arcs) {
    firebreak::core::Scenario scenario;
    scenario.scenario_id = scenario_id;
    scenario.ignition_node = ignition_node;
    scenario.message_filename = "synthetic.csv";
    for (const auto& arc : arcs) {
        scenario.propagation_graph.add_edge(arc.first, arc.second);
    }
    return scenario;
}

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-9);
}

void test_chain_graph() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    });
    firebreak::eval::FppRecourseEvaluator evaluator(opt);

    auto result = evaluator.evaluate({}, true);
    assert_close(result.expected_burned_area, 4.0);
    assert((result.scenarios[0].burned_nodes == std::vector<int>{0, 1, 2, 3}));
    assert(evaluator.isBurned(0, 3));
    assert(evaluator.isReached(0, 3));

    result = evaluator.evaluate({1}, true);
    assert_close(result.expected_burned_area, 1.0);
    assert((result.scenarios[0].burned_nodes == std::vector<int>{0}));
    assert((result.scenarios[0].reached_nodes == std::vector<int>{0, 1}));
    assert(!evaluator.isBurned(0, 1));
    assert(evaluator.isReached(0, 1));

    result = evaluator.evaluate({2}, true);
    assert_close(result.expected_burned_area, 2.0);
    assert((result.scenarios[0].burned_nodes == std::vector<int>{0, 1}));
    assert((result.scenarios[0].reached_nodes == std::vector<int>{0, 1, 2}));

    result = evaluator.evaluate({0}, true);
    assert_close(result.expected_burned_area, 4.0);
    assert((result.scenarios[0].burned_nodes == std::vector<int>{0, 1, 2, 3}));
}

void test_branching_graph() {
    const auto opt = make_opt_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 4, 3, 5},
    });
    firebreak::eval::FppRecourseEvaluator evaluator(opt);

    const auto result = evaluator.evaluate({1}, true);
    assert_close(result.expected_burned_area, 3.0);
    assert((result.scenarios[0].burned_nodes == std::vector<int>{0, 2, 4}));
    assert((result.scenarios[0].reached_nodes == std::vector<int>{0, 1, 2, 4}));
}

void test_two_parallel_paths() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    });
    firebreak::eval::FppRecourseEvaluator evaluator(opt);

    auto result = evaluator.evaluate({1}, true);
    assert_close(result.expected_burned_area, 3.0);
    assert((result.scenarios[0].burned_nodes == std::vector<int>{0, 2, 3}));
    assert(evaluator.isBurned(0, 3));

    result = evaluator.evaluate({1, 2}, true);
    assert_close(result.expected_burned_area, 1.0);
    assert((result.scenarios[0].burned_nodes == std::vector<int>{0}));
    assert((result.scenarios[0].reached_nodes == std::vector<int>{0, 1, 2}));
    assert(!evaluator.isReached(0, 3));
}

void test_duplicate_arcs_and_noneligible_warning() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, {0, 1});
    firebreak::eval::FppRecourseEvaluator evaluator(opt);

    const auto result = evaluator.evaluate({2}, true);
    assert_close(result.expected_burned_area, 2.0);
    assert(result.warnings.size() == 1);
    assert((result.scenarios[0].burned_nodes == std::vector<int>{0, 1}));
    assert((result.scenarios[0].reached_nodes == std::vector<int>{0, 1, 2}));
}

void test_binary_vector_size_validation() {
    const auto opt = make_opt_instance({1, 2}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
    });
    firebreak::eval::FppRecourseEvaluator evaluator(opt);

    bool threw = false;
    try {
        (void)evaluator.evaluateFromBinaryVector({0}, false);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_matches_existing_burned_area_evaluator() {
    firebreak::core::Instance instance;
    instance.landscape_name = "synthetic";
    instance.n_cells = 5;
    instance.has_forest_size = true;
    instance.available_nodes_known = true;
    instance.available_nodes = {1, 2, 3, 4, 5};
    instance.scenarios.push_back(make_core_scenario(1, 1, {{1, 2}, {2, 3}, {3, 4}}));
    instance.scenarios.push_back(make_core_scenario(2, 1, {{1, 2}, {1, 5}, {2, 3}}));

    const firebreak::core::FirebreakSolution firebreaks({2});
    const auto old_result = firebreak::eval::evaluate_instance_burned_area(instance, firebreaks);

    firebreak::opt::OptimizationInstanceBuilder builder;
    const auto opt = builder.build(instance, 1.0, false);
    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto compact_firebreak = opt.node_mapper.to_index(2);
    const auto new_result = evaluator.evaluate({compact_firebreak}, true);

    assert_close(new_result.expected_burned_area, old_result.expected_burned_area);
    assert_close(new_result.scenarios[0].burned_count, 1.0);
    assert_close(new_result.scenarios[1].burned_count, 2.0);
}

#ifdef FIREBREAK_WITH_CPLEX
void test_optional_cplex_solution_matches_recourse() {
    auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    });
    opt.alpha = 1.0 / 3.0;
    opt.budget = 1;

    firebreak::solver::FppSaaCplexModel model;
    const auto solve_result = model.solve(opt, 30.0, 0.0, 1, false);

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate(solve_result.selected_firebreak_indices, true);
    assert_close(recourse.expected_burned_area, solve_result.objective_value);
}
#endif

}  // namespace

int main() {
    test_chain_graph();
    test_branching_graph();
    test_two_parallel_paths();
    test_duplicate_arcs_and_noneligible_warning();
    test_binary_vector_size_validation();
    test_matches_existing_burned_area_evaluator();
#ifdef FIREBREAK_WITH_CPLEX
    test_optional_cplex_solution_matches_recourse();
#endif
    std::cout << "All FPP recourse evaluator tests passed.\n";
    return 0;
}
