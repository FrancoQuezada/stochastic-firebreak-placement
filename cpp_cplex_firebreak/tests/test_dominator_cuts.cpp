#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "cuts/DominatorCuts.hpp"
#include "solver/FppCutReachabilityCplexModel.hpp"
#include "solver/FppSaaCplexModel.hpp"

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

const firebreak::cuts::DominatorSet* find_dominator_set(
    const firebreak::cuts::ScenarioDominatorInfo& scenario,
    int dominator_node) {
    for (const auto& set : scenario.dominated_by_u) {
        if (set.dominator_node == dominator_node) {
            return &set;
        }
    }
    return nullptr;
}

bool contains_node(const std::vector<int>& nodes, int node) {
    return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

void test_chain_dominators() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);

    firebreak::cuts::DominatorPreprocessor preprocessor(opt);
    const auto result = preprocessor.compute();
    assert(result.stats.scenarios_processed == 1);
    assert(result.stats.dag_scenarios == 1);
    assert(result.stats.fallback_scenarios == 0);

    const auto& scenario = result.scenarios.front();
    assert(find_dominator_set(scenario, 0) == nullptr);

    const auto* a = find_dominator_set(scenario, 1);
    assert(a != nullptr);
    assert(contains_node(a->dominated_nodes, 1));
    assert(contains_node(a->dominated_nodes, 2));
    assert(contains_node(a->dominated_nodes, 3));

    const auto* b = find_dominator_set(scenario, 2);
    assert(b != nullptr);
    assert(!contains_node(b->dominated_nodes, 1));
    assert(contains_node(b->dominated_nodes, 2));
    assert(contains_node(b->dominated_nodes, 3));
}

void test_branching_does_not_create_false_dominator() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);

    firebreak::cuts::DominatorPreprocessor preprocessor(opt);
    const auto result = preprocessor.compute();
    const auto& scenario = result.scenarios.front();

    const auto* a = find_dominator_set(scenario, 1);
    const auto* b = find_dominator_set(scenario, 2);
    assert(a != nullptr);
    assert(b != nullptr);
    assert(!contains_node(a->dominated_nodes, 3));
    assert(!contains_node(b->dominated_nodes, 3));
}

void test_tree_aggregate_candidate() {
    const auto opt = make_opt_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{1, 4, 2, 5},
    }, 1);

    firebreak::cuts::DominatorPreprocessor preprocessor(opt);
    const auto result = preprocessor.compute();
    const auto* a = find_dominator_set(result.scenarios.front(), 1);
    assert(a != nullptr);
    assert(contains_node(a->dominated_nodes, 1));
    assert(contains_node(a->dominated_nodes, 3));
    assert(contains_node(a->dominated_nodes, 4));
    assert(a->dominated_nodes.size() == 3);
}

void test_root_convention_skips_root_dominator() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);

    firebreak::cuts::DominatorPreprocessor preprocessor(opt);
    const auto result = preprocessor.compute();
    assert(find_dominator_set(result.scenarios.front(), 0) == nullptr);
}

void test_cycle_uses_fallback_algorithm() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 1, 3, 2},
    }, 1);

    firebreak::cuts::DominatorPreprocessor preprocessor(opt);
    const auto result = preprocessor.compute();
    assert(result.stats.scenarios_processed == 1);
    assert(result.stats.dag_scenarios == 0);
    assert(result.stats.fallback_scenarios == 1);
}

#ifdef FIREBREAK_WITH_CPLEX
void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-6);
}

firebreak::cuts::DominatorCutOptions dominator_options() {
    firebreak::cuts::DominatorCutOptions options;
    options.enabled = true;
    options.max_aggregate_dominator_cuts_per_scenario = 10;
    options.max_individual_dominator_cuts_per_scenario = 20;
    return options;
}

void assert_all_formulations_match(
    const firebreak::opt::OptimizationInstance& opt,
    double expected_objective,
    bool expect_base_dominator_cuts,
    bool expect_cut_dominator_cuts) {
    firebreak::solver::FppSaaCplexModel base_model;
    firebreak::solver::FppCutReachabilityCplexModel cut_model;
    const auto options = dominator_options();

    const auto base = base_model.solve(opt, 30.0, 0.0, 1, false);
    const auto base_dominators = base_model.solve(opt, 30.0, 0.0, 1, false, nullptr, &options);
    const auto cut = cut_model.solve(opt, 30.0, 0.0, 1, false);
    const auto cut_dominators = cut_model.solve(opt, 30.0, 0.0, 1, false, nullptr, &options);

    assert_close(base.objective_value, expected_objective);
    assert_close(base_dominators.objective_value, expected_objective);
    assert_close(cut.objective_value, expected_objective);
    assert_close(cut_dominators.objective_value, expected_objective);
    assert_close(base.objective_value, base_dominators.objective_value);
    assert_close(base.objective_value, cut.objective_value);
    assert_close(base.objective_value, cut_dominators.objective_value);

    if (expect_base_dominator_cuts) {
        assert(base_dominators.dominator_cuts_added > 0);
    } else {
        assert(base_dominators.dominator_cuts_added == 0);
    }
    if (expect_cut_dominator_cuts) {
        assert(cut_dominators.dominator_cuts_added > 0);
    } else {
        assert(cut_dominators.dominator_cuts_added == 0);
    }
}

void test_chain_solve_equivalence_with_cuts() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    assert_all_formulations_match(opt, 1.0, true, true);
}

void test_branching_solve_equivalence_with_cuts() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    assert_all_formulations_match(opt, 3.0, true, false);
}

void test_tree_solve_equivalence_with_cuts() {
    const auto opt = make_opt_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{1, 4, 2, 5},
    }, 1);
    assert_all_formulations_match(opt, 2.0, true, true);
}

void test_root_only_solve_equivalence_with_cuts() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1, {0});
    assert_all_formulations_match(opt, 3.0, false, false);
}
#endif

}  // namespace

int main() {
    test_chain_dominators();
    test_branching_does_not_create_false_dominator();
    test_tree_aggregate_candidate();
    test_root_convention_skips_root_dominator();
    test_cycle_uses_fallback_algorithm();
#ifdef FIREBREAK_WITH_CPLEX
    test_chain_solve_equivalence_with_cuts();
    test_branching_solve_equivalence_with_cuts();
    test_tree_solve_equivalence_with_cuts();
    test_root_only_solve_equivalence_with_cuts();
#endif
    std::cout << "All dominator cut tests passed.\n";
    return 0;
}
