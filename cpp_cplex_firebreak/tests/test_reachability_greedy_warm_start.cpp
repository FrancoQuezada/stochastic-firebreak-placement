#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "eval/FppRecourseEvaluator.hpp"
#include "heuristics/ReachabilityGreedyWarmStart.hpp"
#include "solver/FppSaaCplexModel.hpp"
#include "solver/WarmStart.hpp"

namespace {

firebreak::opt::OptimizationInstance make_opt_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    int budget,
    const std::vector<int>& eligible_compact_nodes = {}) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = budget;
    opt.node_mapper.build_from_nodes(original_nodes);

    if (eligible_compact_nodes.empty()) {
        for (const int original_node : original_nodes) {
            opt.eligible_original_nodes.push_back(original_node);
            opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
        }
    } else {
        opt.eligible_indices = eligible_compact_nodes;
        for (const int compact_node : eligible_compact_nodes) {
            opt.eligible_original_nodes.push_back(opt.node_mapper.to_node(compact_node));
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

firebreak::heuristics::ReachabilityGreedyWarmStartResult run_greedy(
    const firebreak::opt::OptimizationInstance& opt,
    bool exact_marginal = true,
    int pool_min_size = 50,
    int pool_multiplier = 5) {
    firebreak::heuristics::ReachabilityGreedyWarmStartOptions options;
    options.enable_greedy_exact_marginal = exact_marginal;
    options.candidate_pool_min_size = pool_min_size;
    options.candidate_pool_size_multiplier = pool_multiplier;
    firebreak::heuristics::ReachabilityGreedyWarmStart heuristic(opt, options);
    return heuristic.run();
}

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-9);
}

void assert_selected_nodes_are_eligible(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<int>& selected) {
    for (const int compact_node : selected) {
        assert(std::find(opt.eligible_indices.begin(), opt.eligible_indices.end(), compact_node) !=
               opt.eligible_indices.end());
    }
}

void test_chain_graph_selects_first_nonroot() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);

    const auto result = run_greedy(opt);
    assert((result.selected_firebreak_compact_nodes == std::vector<int>{1}));
    assert_close(result.empty_objective, 4.0);
    assert_close(result.objective, 1.0);
    assert(result.iterations == 1);
    assert(!result.y_selected[0]);
    assert(result.y_selected[1]);
}

void test_screened_mode_avoids_root_only_pool() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);

    const auto result = run_greedy(opt, false, 1, 1);
    assert((result.selected_firebreak_compact_nodes == std::vector<int>{1}));
    assert_close(result.objective, 1.0);
}

void test_branching_graph_improves_objective() {
    const auto opt = make_opt_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 4, 3, 5},
    }, 1);

    const auto result = run_greedy(opt);
    assert(result.selected_firebreak_compact_nodes.size() == 1);
    assert(result.selected_firebreak_compact_nodes[0] == 1 || result.selected_firebreak_compact_nodes[0] == 2);
    assert_close(result.empty_objective, 5.0);
    assert(result.objective < result.empty_objective);
}

void test_two_parallel_paths_budget_one_and_two() {
    auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);

    auto result = run_greedy(opt);
    assert(result.selected_firebreak_compact_nodes.size() == 1);
    assert(result.objective <= result.empty_objective);

    opt.budget = 2;
    result = run_greedy(opt);
    assert((result.selected_firebreak_compact_nodes == std::vector<int>{1, 2}));
    assert_close(result.objective, 1.0);
}

void test_multiscenario_invariants() {
    auto opt = make_opt_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 2, {1, 2, 3, 4});

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 2;
    second.probability = 1.0;
    second.ignition_index = 0;
    second.ignition_original_node = 1;
    second.message_filename = "synthetic_2.csv";
    second.arcs = {
        firebreak::opt::CompactArc{0, 3, 1, 4},
        firebreak::opt::CompactArc{3, 4, 4, 5},
    };
    for (int i = 0; i < 5; ++i) {
        second.observed_node_indices.push_back(i);
    }
    opt.scenarios.push_back(second);
    opt.scenario_probabilities = {0.5, 0.5};
    opt.scenarios[0].probability = 0.5;
    opt.scenarios[1].probability = 0.5;
    opt.total_arcs += second.arcs.size();

    const auto result = run_greedy(opt);
    assert(result.selected_firebreak_compact_nodes.size() <= static_cast<std::size_t>(opt.budget));
    assert_selected_nodes_are_eligible(opt, result.selected_firebreak_compact_nodes);
    assert(result.objective <= result.empty_objective);

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluateFromBinaryVector(result.y_selected, true);
    assert_close(recourse.expected_burned_area, result.objective);
}

#ifdef FIREBREAK_WITH_CPLEX
void test_cplex_warm_start_smoke() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);

    const auto greedy_result = run_greedy(opt);
    std::vector<int> original_nodes;
    for (const int compact_node : greedy_result.selected_firebreak_compact_nodes) {
        original_nodes.push_back(opt.node_mapper.to_node(compact_node));
    }
    auto warm_start = firebreak::solver::prepare_warm_start_from_original_nodes(
        original_nodes,
        opt,
        opt.budget,
        "reachability-greedy-warm-start-test");

    firebreak::solver::FppSaaCplexModel model;
    const auto solve_result = model.solve(opt, 30.0, 0.0, 1, false, &warm_start);
    assert(solve_result.warm_start_used);

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate(solve_result.selected_firebreak_indices, true);
    assert_close(recourse.expected_burned_area, solve_result.objective_value);
}
#endif

}  // namespace

int main() {
    test_chain_graph_selects_first_nonroot();
    test_screened_mode_avoids_root_only_pool();
    test_branching_graph_improves_objective();
    test_two_parallel_paths_budget_one_and_two();
    test_multiscenario_invariants();
#ifdef FIREBREAK_WITH_CPLEX
    test_cplex_warm_start_smoke();
#endif
    std::cout << "All reachability-greedy warm-start tests passed.\n";
    return 0;
}
