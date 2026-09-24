#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppStrengthening.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-9) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_path_llbi_manual";
    opt.alpha = 1.0 / 6.0;
    opt.n_cells = 6;
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({10, 20, 30, 40, 50, 60});
    opt.eligible_indices = {0, 1, 2, 3};
    opt.eligible_original_nodes = {10, 20, 30, 40};
    opt.compact_cell_weights = {100.0, 2.0, 3.0, 50.0, 7.0, 11.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 101;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 10;
    scenario.observed_node_indices = {0, 1, 2, 3, 4, 5};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 10, 20},
        firebreak::opt::CompactArc{1, 3, 20, 40},
        firebreak::opt::CompactArc{0, 2, 10, 30},
        firebreak::opt::CompactArc{2, 3, 30, 40},
        firebreak::opt::CompactArc{0, 4, 10, 50},
        firebreak::opt::CompactArc{4, 5, 50, 60},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

const firebreak::benders::FppPathLlbiNodeRecord* find_node(
    const firebreak::benders::FppPathLlbiScenarioRecord& scenario,
    int compact_node) {
    for (const auto& node : scenario.nodes) {
        if (node.compact_node == compact_node) {
            return &node;
        }
    }
    return nullptr;
}

double projected_path_bound(
    const firebreak::benders::FppPathLlbiScenarioRecord& scenario,
    const std::vector<char>& selected_by_compact) {
    double bound = 0.0;
    for (const auto& node : scenario.nodes) {
        bool has_unblocked_path = false;
        for (const auto& path : node.paths) {
            bool blocked = false;
            for (const int candidate : path.blocking_candidate_compact_nodes) {
                blocked = blocked ||
                    selected_by_compact[static_cast<std::size_t>(candidate)] != 0;
            }
            has_unblocked_path = has_unblocked_path || !blocked;
        }
        if (has_unblocked_path) {
            bound += node.cell_weight;
        }
    }
    return bound;
}

void test_manual_paths_weights_and_ignition() {
    const auto opt = make_path_instance();
    const auto data = firebreak::benders::build_fpp_path_llbi_data(opt, true, 8);
    assert(data.enabled);
    assert(data.weighted);
    assert(data.scenarios_precomputed == 1);
    assert(data.baseline_nodes == 6);
    assert(data.num_b_vars == 6);
    assert(data.auxiliary_variables == 6);
    assert(data.loss_constraints == 1);
    assert(data.total_paths == 7);
    assert(data.total_candidate_incidence_terms == 6);
    assert(data.nodes_without_paths == 0);
    assert(data.path_enumeration_complete);
    assert(data.paths_truncated == 0);
    assert(data.validity_mode == "directed-simple-path-burning-lower-bound");

    const auto& scenario = data.scenarios.front();
    const auto* root = find_node(scenario, 0);
    const auto* shared = find_node(scenario, 3);
    const auto* noneligible = find_node(scenario, 5);
    assert(root && shared && noneligible);
    assert_close(root->cell_weight, 100.0);
    assert(root->paths.size() == 1);
    assert(root->paths.front().blocking_candidate_compact_nodes.empty());
    assert_close(shared->cell_weight, 50.0);
    assert(shared->paths.size() == 2);
    assert((shared->paths[0].blocking_candidate_compact_nodes == std::vector<int>{1, 3}));
    assert((shared->paths[1].blocking_candidate_compact_nodes == std::vector<int>{2, 3}));
    assert_close(noneligible->cell_weight, 11.0);
    assert(noneligible->paths.size() == 1);
    assert(noneligible->paths.front().blocking_candidate_compact_nodes.empty());

    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    assert_close(projected_path_bound(scenario, selected), 173.0);
    selected[0] = 1;
    assert_close(projected_path_bound(scenario, selected), 173.0);
    selected[1] = 1;
    assert_close(projected_path_bound(scenario, selected), 171.0);
    selected[2] = 1;
    assert_close(projected_path_bound(scenario, selected), 118.0);
}

void test_duplicate_blocker_sets_and_truncation() {
    auto duplicate = make_path_instance();
    duplicate.scenarios.front().arcs = {
        firebreak::opt::CompactArc{0, 4, 10, 50},
        firebreak::opt::CompactArc{4, 3, 50, 40},
        firebreak::opt::CompactArc{0, 5, 10, 60},
        firebreak::opt::CompactArc{5, 3, 60, 40},
    };
    const auto deduped = firebreak::benders::build_fpp_path_llbi_data(duplicate, true, 8);
    const auto* shared = find_node(deduped.scenarios.front(), 3);
    assert(shared != nullptr);
    assert(shared->paths.size() == 1);
    assert(shared->paths.front().blocking_candidate_compact_nodes ==
           std::vector<int>{3});

    const auto truncated = firebreak::benders::build_fpp_path_llbi_data(
        make_path_instance(),
        true,
        1);
    assert(!truncated.path_enumeration_complete);
    assert(truncated.paths_truncated > 0);
}

void test_homogeneous_regression_counts() {
    auto implicit = make_path_instance();
    implicit.compact_cell_weights.clear();
    const auto implicit_data = firebreak::benders::build_fpp_path_llbi_data(
        implicit,
        true,
        8);

    auto explicit_unit = make_path_instance();
    explicit_unit.compact_cell_weights.assign(
        static_cast<std::size_t>(explicit_unit.node_mapper.size()),
        1.0);
    const auto explicit_data = firebreak::benders::build_fpp_path_llbi_data(
        explicit_unit,
        true,
        8);
    assert(!implicit_data.weighted);
    assert(!explicit_data.weighted);
    assert(implicit_data.num_b_vars == explicit_data.num_b_vars);
    assert(implicit_data.num_path_constraints == explicit_data.num_path_constraints);
    assert(implicit_data.total_candidate_incidence_terms ==
           explicit_data.total_candidate_incidence_terms);
    for (std::size_t i = 0; i < implicit_data.scenarios.front().nodes.size(); ++i) {
        const auto& lhs = implicit_data.scenarios.front().nodes[i];
        const auto& rhs = explicit_data.scenarios.front().nodes[i];
        assert(lhs.compact_node == rhs.compact_node);
        assert_close(lhs.cell_weight, rhs.cell_weight);
        assert(lhs.paths.size() == rhs.paths.size());
    }
}

}  // namespace

int main() {
    test_manual_paths_weights_and_ignition();
    test_duplicate_blocker_sets_and_truncation();
    test_homogeneous_regression_counts();
    std::cout << "All weighted PathLLBI tests passed.\n";
    return 0;
}
