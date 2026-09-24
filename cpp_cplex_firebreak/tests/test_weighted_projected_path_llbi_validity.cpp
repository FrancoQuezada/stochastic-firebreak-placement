#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "benders/FppLiftedLowerBound.hpp"
#include "benders/FppProjectedLlbi.hpp"
#include "benders/FppStrengthening.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance(
    int node_count,
    int budget,
    const std::vector<int>& eligible,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    const std::vector<double>& weights,
    int scenario_id = 1) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_projected_path_validity";
    opt.alpha = 1.0 / static_cast<double>(node_count);
    opt.n_cells = node_count;
    opt.budget = budget;
    std::vector<int> original_nodes;
    for (int i = 0; i < node_count; ++i) {
        original_nodes.push_back(i + 1);
    }
    opt.node_mapper.build_from_nodes(original_nodes);
    opt.eligible_indices = eligible;
    for (const int compact : eligible) {
        opt.eligible_original_nodes.push_back(opt.node_mapper.to_node(compact));
    }
    opt.compact_cell_weights = weights;

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = scenario_id;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    for (int compact = 0; compact < node_count; ++compact) {
        scenario.observed_node_indices.push_back(compact);
    }
    scenario.arcs = arcs;
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = arcs.size();
    return opt;
}

std::vector<double> compact_from_eligible(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<double>& eligible_y) {
    std::vector<double> compact(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        compact[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            eligible_y[pos];
    }
    return compact;
}

double nonlinear_projected_path_value(
    const firebreak::benders::FppPathLlbiScenarioRecord& scenario,
    const std::vector<double>& compact_y) {
    double value = 0.0;
    for (const auto& node : scenario.nodes) {
        double best = std::numeric_limits<double>::infinity();
        for (const auto& path : node.paths) {
            double cost = 0.0;
            for (const int candidate : path.blocking_candidate_compact_nodes) {
                cost += compact_y[static_cast<std::size_t>(candidate)];
            }
            best = std::min(best, cost);
        }
        if (std::isfinite(best)) {
            value += node.cell_weight * std::max(0.0, 1.0 - best);
        }
    }
    return value;
}

double max_projected_mapping_rhs_recursive(
    const firebreak::benders::FppPathLlbiScenarioRecord& scenario,
    const std::vector<double>& compact_y,
    int node_pos,
    double current) {
    if (node_pos == static_cast<int>(scenario.nodes.size())) {
        return current;
    }
    const auto& node = scenario.nodes[static_cast<std::size_t>(node_pos)];
    double best = max_projected_mapping_rhs_recursive(
        scenario,
        compact_y,
        node_pos + 1,
        current);
    for (const auto& path : node.paths) {
        double cost = 0.0;
        for (const int candidate : path.blocking_candidate_compact_nodes) {
            cost += compact_y[static_cast<std::size_t>(candidate)];
        }
        best = std::max(
            best,
            max_projected_mapping_rhs_recursive(
                scenario,
                compact_y,
                node_pos + 1,
                current + node.cell_weight * (1.0 - cost)));
    }
    return best;
}

void assert_extended_projected_equivalence(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<double>& eligible_y,
    int max_paths_per_node) {
    const auto data = firebreak::benders::build_fpp_path_llbi_data(
        opt,
        true,
        max_paths_per_node);
    assert(data.enabled);
    assert(data.scenarios.size() == 1);
    const auto compact_y = compact_from_eligible(opt, eligible_y);
    const auto& scenario = data.scenarios.front();
    const double nonlinear = nonlinear_projected_path_value(scenario, compact_y);
    const double mapped = max_projected_mapping_rhs_recursive(
        scenario,
        compact_y,
        0,
        0.0);
    if (std::fabs(nonlinear - mapped) > 1.0e-8) {
        throw std::runtime_error("Projected path nonlinear/mapping equivalence failed.");
    }

    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_exp = true;
    options.path_max_paths_per_node = max_paths_per_node;
    options.max_cuts_per_round = 10;
    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        eligible_y,
        {0.0});
    if (!separated.cuts.empty()) {
        const double separated_rhs =
            separated.cuts.front().cut.evaluateAt(compact_y);
        if (std::fabs(separated_rhs - mapped) > 1.0e-8) {
            throw std::runtime_error("Projected path separator did not produce the most violated mapping.");
        }
    }
}

void assert_binary_validity(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<double>& eligible_y,
    int max_paths_per_node) {
    const auto data = firebreak::benders::build_fpp_path_llbi_data(
        opt,
        true,
        max_paths_per_node);
    const auto compact_y = compact_from_eligible(opt, eligible_y);
    const double projected =
        nonlinear_projected_path_value(data.scenarios.front(), compact_y);
    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        if (eligible_y[pos] > 0.5) {
            selected[static_cast<std::size_t>(opt.eligible_indices[pos])] = 1;
        }
    }
    const double actual = firebreak::benders::evaluate_fixed_y_fpp_loss(
        opt,
        0,
        selected).weighted_burn_loss;
    if (projected > actual + 1.0e-8) {
        throw std::runtime_error("Projected path lower bound exceeded exact recourse.");
    }
}

void enumerate_binary(
    const firebreak::opt::OptimizationInstance& opt,
    int max_paths_per_node,
    int pos,
    int selected_count,
    std::vector<double>& eligible_y) {
    if (pos == static_cast<int>(eligible_y.size())) {
        assert_extended_projected_equivalence(opt, eligible_y, max_paths_per_node);
        assert_binary_validity(opt, eligible_y, max_paths_per_node);
        return;
    }
    eligible_y[static_cast<std::size_t>(pos)] = 0.0;
    enumerate_binary(opt, max_paths_per_node, pos + 1, selected_count, eligible_y);
    if (selected_count < opt.budget) {
        eligible_y[static_cast<std::size_t>(pos)] = 1.0;
        enumerate_binary(opt, max_paths_per_node, pos + 1, selected_count + 1, eligible_y);
    }
    eligible_y[static_cast<std::size_t>(pos)] = 0.0;
}

void validate_instance(const firebreak::opt::OptimizationInstance& opt, int max_paths_per_node) {
    std::vector<double> eligible_y(opt.eligible_indices.size(), 0.0);
    enumerate_binary(opt, max_paths_per_node, 0, 0, eligible_y);

    if (!eligible_y.empty()) {
        eligible_y.assign(opt.eligible_indices.size(), 0.35);
        assert_extended_projected_equivalence(opt, eligible_y, max_paths_per_node);
        for (std::size_t pos = 0; pos < eligible_y.size(); ++pos) {
            eligible_y[pos] = (pos % 2 == 0) ? 0.15 : 0.80;
        }
        assert_extended_projected_equivalence(opt, eligible_y, max_paths_per_node);
    }
}

void test_structured_graphs() {
    validate_instance(make_instance(
        3,
        1,
        {1},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
        },
        {10.0, 2.0, 50.0}), 8);

    validate_instance(make_instance(
        4,
        2,
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{0, 2, 1, 3},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        },
        {100.0, 2.0, 3.0, 50.0}), 8);

    validate_instance(make_instance(
        4,
        2,
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{2, 1, 3, 2},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        },
        {4.0, 30.0, 5.0, 80.0}), 8);
}

void test_truncated_path_family_remains_valid() {
    validate_instance(make_instance(
        5,
        2,
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 4, 2, 5},
            firebreak::opt::CompactArc{0, 2, 1, 3},
            firebreak::opt::CompactArc{2, 4, 3, 5},
            firebreak::opt::CompactArc{0, 3, 1, 4},
            firebreak::opt::CompactArc{3, 4, 4, 5},
        },
        {10.0, 7.0, 11.0, 13.0, 100.0}), 1);
}

void test_fixed_seed_randomized_search() {
    std::mt19937 rng(6331);
    std::uniform_real_distribution<double> weight_dist(0.5, 40.0);
    std::bernoulli_distribution arc_dist(0.30);
    for (int trial = 0; trial < 80; ++trial) {
        const int node_count = 3 + (trial % 4);
        std::vector<firebreak::opt::CompactArc> arcs;
        for (int u = 0; u < node_count; ++u) {
            for (int v = 0; v < node_count; ++v) {
                if (u != v && arc_dist(rng)) {
                    arcs.push_back(firebreak::opt::CompactArc{u, v, u + 1, v + 1});
                }
            }
        }
        std::vector<int> eligible;
        for (int node = 1; node < node_count; ++node) {
            eligible.push_back(node);
        }
        std::vector<double> weights;
        for (int node = 0; node < node_count; ++node) {
            weights.push_back(weight_dist(rng));
        }
        validate_instance(make_instance(node_count, 2, eligible, arcs, weights, trial + 1), 4);
    }
}

}  // namespace

int main() {
    test_structured_graphs();
    test_truncated_path_family_remains_valid();
    test_fixed_seed_randomized_search();
    std::cout << "All weighted projected PathLLBI validity tests passed.\n";
    return 0;
}
