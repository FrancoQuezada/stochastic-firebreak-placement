#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include "benders/FppStrengthening.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance(
    const std::vector<int>& original_nodes,
    const std::vector<int>& eligible_compact_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_strengthening";
    opt.alpha = 1.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = 1;
    opt.node_mapper.build_from_nodes(original_nodes);
    opt.eligible_indices = eligible_compact_nodes;
    for (const int compact : eligible_compact_nodes) {
        opt.eligible_original_nodes.push_back(opt.node_mapper.to_node(compact));
    }

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = opt.node_mapper.to_node(0);
    for (int compact = 0; compact < opt.node_mapper.size(); ++compact) {
        scenario.observed_node_indices.push_back(compact);
    }
    scenario.arcs = arcs;

    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = arcs.size();
    return opt;
}

const firebreak::benders::FppCoverageLlbiNodeRecord* find_coverage_node(
    const firebreak::benders::FppCoverageLlbiScenarioRecord& scenario,
    int compact_node) {
    for (const auto& node : scenario.nodes) {
        if (node.compact_node == compact_node) {
            return &node;
        }
    }
    return nullptr;
}

const firebreak::benders::FppPathLlbiNodeRecord* find_path_node(
    const firebreak::benders::FppPathLlbiScenarioRecord& scenario,
    int compact_node) {
    for (const auto& node : scenario.nodes) {
        if (node.compact_node == compact_node) {
            return &node;
        }
    }
    return nullptr;
}

void test_coverage_llbi_tree() {
    const auto opt = make_instance(
        {1, 2, 3},
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
        });

    const auto data = firebreak::benders::build_fpp_coverage_llbi_data(opt, true);
    assert(data.enabled);
    assert(data.num_zeta_vars == 2);
    assert(data.num_constraints == 3);
    assert(data.precompute_time_sec >= 0.0);
    assert(data.scenarios.size() == 1);
    assert(data.scenarios.front().empty_burned_area == 3.0);

    const auto* node_one = find_coverage_node(data.scenarios.front(), 1);
    const auto* node_two = find_coverage_node(data.scenarios.front(), 2);
    assert(node_one != nullptr);
    assert(node_two != nullptr);
    assert((node_one->covering_candidate_compact_nodes == std::vector<int>{1}));
    assert((node_two->covering_candidate_compact_nodes == std::vector<int>{1, 2}));
}

void test_coverage_llbi_overlap_is_counted_once() {
    const auto opt = make_instance(
        {1, 2, 3, 4},
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{0, 2, 1, 3},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        });

    const auto data = firebreak::benders::build_fpp_coverage_llbi_data(opt, true);
    assert(data.num_zeta_vars == 3);
    assert(data.num_constraints == 4);
    const auto* shared = find_coverage_node(data.scenarios.front(), 3);
    assert(shared != nullptr);
    assert((shared->covering_candidate_compact_nodes == std::vector<int>{1, 2}));
}

void test_path_llbi_tree() {
    const auto opt = make_instance(
        {1, 2, 3},
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
        });

    const auto data = firebreak::benders::build_fpp_path_llbi_data(opt, true, 8);
    assert(data.enabled);
    assert(data.num_b_vars == 3);
    assert(data.num_path_constraints == 3);
    assert(data.num_paths_used == 3);
    assert(data.precompute_time_sec >= 0.0);

    const auto* leaf = find_path_node(data.scenarios.front(), 2);
    assert(leaf != nullptr);
    assert(leaf->paths.size() == 1);
    assert((leaf->paths.front().blocking_candidate_compact_nodes == std::vector<int>{1, 2}));
}

void test_path_llbi_dag_multiple_paths() {
    const auto opt = make_instance(
        {1, 2, 3, 4},
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{0, 2, 1, 3},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        });

    const auto data = firebreak::benders::build_fpp_path_llbi_data(opt, true, 8);
    assert(data.num_b_vars == 4);
    assert(data.num_paths_used == 5);
    const auto* shared = find_path_node(data.scenarios.front(), 3);
    assert(shared != nullptr);
    assert(shared->paths.size() == 2);
    assert((shared->paths[0].blocking_candidate_compact_nodes == std::vector<int>{1}));
    assert((shared->paths[1].blocking_candidate_compact_nodes == std::vector<int>{2}));
}

void test_global_dominance_strict() {
    const auto opt = make_instance(
        {1, 2, 3, 4},
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        });

    const auto result = firebreak::benders::apply_fpp_global_dominance_preprocessing(opt, true);
    assert(result.enabled);
    assert(result.candidates_removed == 2);
    assert((result.kept_candidate_compact_nodes == std::vector<int>{1}));
    assert((result.reduced_instance.eligible_indices == std::vector<int>{1}));
    assert(result.precompute_time_sec >= 0.0);
}

void test_global_dominance_duplicate_representative() {
    firebreak::opt::OptimizationInstance opt = make_instance(
        {1, 2, 3},
        {1, 1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{0, 2, 1, 3},
        });
    opt.eligible_original_nodes = {2, 2, 3};

    const auto result = firebreak::benders::apply_fpp_global_dominance_preprocessing(opt, true);
    assert(result.candidates_removed == 1);
    assert(result.equivalence_classes == 1);
    assert((result.reduced_instance.eligible_indices == std::vector<int>{1, 2}));
}

void test_global_dominance_equal_cardinality_nondominance() {
    const auto opt = make_instance(
        {1, 2, 3, 4, 5},
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{0, 2, 1, 3},
            firebreak::opt::CompactArc{2, 4, 3, 5},
        });

    const auto result = firebreak::benders::apply_fpp_global_dominance_preprocessing(opt, true);
    assert(result.candidates_removed == 0);
    assert((result.reduced_instance.eligible_indices == std::vector<int>{1, 2}));
}

void test_global_dominance_keeps_budget_feasible() {
    auto opt = make_instance(
        {1, 2, 3, 4},
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        });
    opt.budget = 2;

    const auto result = firebreak::benders::apply_fpp_global_dominance_preprocessing(opt, true);
    assert(result.enabled);
    assert(result.candidates_removed == 0);
    assert(result.equivalence_classes == 0);
    assert((result.reduced_instance.eligible_indices == std::vector<int>{1, 2, 3}));
    assert(!result.notes.empty());
}

void test_conditional_zero_benefit_detector() {
    const auto opt = make_instance(
        {1, 2, 3, 4},
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{0, 3, 1, 4},
        });

    const auto result =
        firebreak::benders::detect_fpp_conditional_zero_benefit_candidates(opt, {1}, true);
    assert(result.enabled);
    assert(result.fixings_attempted == 2);
    assert(result.fixings_applied == 0);
    assert(result.time_sec >= 0.0);
    assert((result.zero_benefit_candidate_compact_nodes == std::vector<int>{2}));
}

}  // namespace

int main() {
    test_coverage_llbi_tree();
    test_coverage_llbi_overlap_is_counted_once();
    test_path_llbi_tree();
    test_path_llbi_dag_multiple_paths();
    test_global_dominance_strict();
    test_global_dominance_duplicate_representative();
    test_global_dominance_equal_cardinality_nondominance();
    test_global_dominance_keeps_budget_feasible();
    test_conditional_zero_benefit_detector();
    std::cout << "All FPP strengthening tests passed.\n";
    return 0;
}
