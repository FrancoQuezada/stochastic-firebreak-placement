#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppStrengthening.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-9) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_overlap_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_coverage_llbi_overlap";
    opt.alpha = 1.0 / 6.0;
    opt.n_cells = 6;
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({10, 20, 30, 40, 50, 60});
    opt.eligible_indices = {1, 2, 4, 5};
    opt.eligible_original_nodes = {20, 30, 50, 60};
    opt.compact_cell_weights = {100.0, 2.0, 3.0, 50.0, 7.0, 11.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 101;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 10;
    scenario.observed_node_indices = {0, 1, 2, 3, 4, 5};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 10, 20},
        firebreak::opt::CompactArc{0, 2, 10, 30},
        firebreak::opt::CompactArc{1, 3, 20, 40},
        firebreak::opt::CompactArc{2, 3, 30, 40},
        firebreak::opt::CompactArc{1, 4, 20, 50},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

const firebreak::benders::FppCoverageLlbiNodeRecord* find_node(
    const firebreak::benders::FppCoverageLlbiScenarioRecord& scenario,
    int compact_node) {
    for (const auto& node : scenario.nodes) {
        if (node.compact_node == compact_node) {
            return &node;
        }
    }
    return nullptr;
}

double projected_coverage_bound(
    const firebreak::benders::FppCoverageLlbiScenarioRecord& scenario,
    const std::vector<char>& selected_by_compact) {
    double bound = scenario.empty_burned_area;
    for (const auto& node : scenario.nodes) {
        int selected_covering_candidates = 0;
        for (const int candidate : node.covering_candidate_compact_nodes) {
            selected_covering_candidates +=
                selected_by_compact[static_cast<std::size_t>(candidate)] ? 1 : 0;
        }
        if (selected_covering_candidates > 0) {
            bound -= node.cell_weight;
        }
    }
    return bound;
}

void test_manual_weighted_baseline_and_coverage_sets() {
    const auto opt = make_weighted_overlap_instance();
    const auto data = firebreak::benders::build_fpp_coverage_llbi_data(opt, true);
    assert(data.enabled);
    assert(data.weighted);
    assert(data.scenarios_precomputed == 1);
    assert(data.baseline_cells == 5);
    assert(data.num_zeta_vars == 4);
    assert(data.auxiliary_variables == 4);
    assert(data.linking_constraints == 4);
    assert(data.loss_constraints == 1);
    assert(data.nonempty_coverage_sets == 4);
    assert(data.total_incidence_terms == 6);
    assert(data.validity_mode == "per-cell-capped-downstream-coverage-bound");

    const auto& scenario = data.scenarios.front();
    assert_close(scenario.empty_burned_area, 162.0);
    assert(scenario.baseline_burned_cell_count == 5);
    assert(find_node(scenario, 0) == nullptr);
    assert(find_node(scenario, 5) == nullptr);

    const auto* node_one = find_node(scenario, 1);
    const auto* node_two = find_node(scenario, 2);
    const auto* shared = find_node(scenario, 3);
    const auto* tail = find_node(scenario, 4);
    assert(node_one && node_two && shared && tail);
    assert_close(node_one->cell_weight, 2.0);
    assert_close(node_two->cell_weight, 3.0);
    assert_close(shared->cell_weight, 50.0);
    assert_close(tail->cell_weight, 7.0);
    assert((node_one->covering_candidate_compact_nodes == std::vector<int>{1}));
    assert((node_two->covering_candidate_compact_nodes == std::vector<int>{2}));
    assert((shared->covering_candidate_compact_nodes == std::vector<int>{1, 2}));
    assert((tail->covering_candidate_compact_nodes == std::vector<int>{1, 4}));
}

void test_overlap_is_capped_per_cell() {
    const auto opt = make_weighted_overlap_instance();
    const auto data = firebreak::benders::build_fpp_coverage_llbi_data(opt, true);
    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    selected[1] = 1;
    selected[2] = 1;
    assert_close(projected_coverage_bound(data.scenarios.front(), selected), 100.0);
}

void test_homogeneous_regression_counts() {
    auto implicit = make_weighted_overlap_instance();
    implicit.compact_cell_weights.clear();
    const auto implicit_data = firebreak::benders::build_fpp_coverage_llbi_data(implicit, true);

    auto explicit_unit = make_weighted_overlap_instance();
    explicit_unit.compact_cell_weights.assign(
        static_cast<std::size_t>(explicit_unit.node_mapper.size()),
        1.0);
    const auto explicit_data =
        firebreak::benders::build_fpp_coverage_llbi_data(explicit_unit, true);

    assert(!implicit_data.weighted);
    assert(!explicit_data.weighted);
    assert_close(implicit_data.scenarios.front().empty_burned_area, 5.0);
    assert_close(explicit_data.scenarios.front().empty_burned_area, 5.0);
    assert(implicit_data.num_zeta_vars == explicit_data.num_zeta_vars);
    assert(implicit_data.total_incidence_terms == explicit_data.total_incidence_terms);
    for (std::size_t i = 0; i < implicit_data.scenarios.front().nodes.size(); ++i) {
        const auto& lhs = implicit_data.scenarios.front().nodes[i];
        const auto& rhs = explicit_data.scenarios.front().nodes[i];
        assert(lhs.compact_node == rhs.compact_node);
        assert_close(lhs.cell_weight, rhs.cell_weight);
        assert(lhs.covering_candidate_compact_nodes == rhs.covering_candidate_compact_nodes);
    }
}

}  // namespace

int main() {
    test_manual_weighted_baseline_and_coverage_sets();
    test_overlap_is_capped_per_cell();
    test_homogeneous_regression_counts();
    std::cout << "All weighted CoverageLLBI tests passed.\n";
    return 0;
}
