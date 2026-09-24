#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "benders/FppLiftedLowerBound.hpp"
#include "benders/FppStrengthening.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance(
    int node_count,
    int budget,
    const std::vector<int>& eligible,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    const std::vector<double>& weights) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_coverage_llbi_validity";
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
    scenario.scenario_id = 1;
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

double projected_coverage_bound(
    const firebreak::benders::FppCoverageLlbiScenarioRecord& scenario,
    const std::vector<char>& selected_by_compact) {
    double bound = scenario.empty_burned_area;
    for (const auto& node : scenario.nodes) {
        bool covered = false;
        for (const int candidate : node.covering_candidate_compact_nodes) {
            covered = covered ||
                selected_by_compact[static_cast<std::size_t>(candidate)] != 0;
        }
        if (covered) {
            bound -= node.cell_weight;
        }
    }
    return bound;
}

void enumerate_subsets(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::benders::FppCoverageLlbiScenarioRecord& scenario,
    int budget,
    int start_pos,
    std::vector<int>& selected_positions) {
    std::vector<char> selected_by_compact(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    for (const int pos : selected_positions) {
        selected_by_compact[static_cast<std::size_t>(opt.eligible_indices[pos])] = 1;
    }

    const double lower_bound = projected_coverage_bound(scenario, selected_by_compact);
    const double actual = firebreak::benders::evaluate_fixed_y_fpp_loss(
        opt,
        scenario.scenario_index,
        selected_by_compact).weighted_burn_loss;
    if (actual + 1.0e-8 < lower_bound) {
        std::ostringstream out;
        out << "CoverageLLBI violation: actual=" << actual
            << " lower_bound=" << lower_bound
            << " selected_positions=";
        for (const int pos : selected_positions) {
            out << pos << ";";
        }
        throw std::runtime_error(out.str());
    }

    if (static_cast<int>(selected_positions.size()) >= budget) {
        return;
    }
    for (int pos = start_pos; pos < static_cast<int>(opt.eligible_indices.size()); ++pos) {
        selected_positions.push_back(pos);
        enumerate_subsets(opt, scenario, budget, pos + 1, selected_positions);
        selected_positions.pop_back();
    }
}

void validate_exhaustive(const firebreak::opt::OptimizationInstance& opt) {
    const auto data = firebreak::benders::build_fpp_coverage_llbi_data(opt, true);
    assert(data.enabled);
    assert(data.scenarios.size() == 1);
    std::vector<int> selected_positions;
    enumerate_subsets(opt, data.scenarios.front(), opt.budget, 0, selected_positions);
}

void test_tree_diamond_overlap_and_downstream_overinclusion() {
    validate_exhaustive(make_instance(
        4,
        2,
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        },
        {1.0, 2.0, 7.0, 13.0}));

    validate_exhaustive(make_instance(
        4,
        2,
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{0, 2, 1, 3},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        },
        {100.0, 2.0, 3.0, 50.0}));

    const auto downstream_overinclusion = make_instance(
        4,
        2,
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{0, 2, 1, 3},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        },
        {1.0, 100.0, 100.0, 1000.0});
    const auto data = firebreak::benders::build_fpp_coverage_llbi_data(
        downstream_overinclusion,
        true);
    const auto& nodes = data.scenarios.front().nodes;
    bool shared_has_singleton_coverage = false;
    for (const auto& node : nodes) {
        if (node.compact_node == 3) {
            shared_has_singleton_coverage = true;
            assert((node.covering_candidate_compact_nodes == std::vector<int>{1, 2}));
        }
    }
    assert(shared_has_singleton_coverage);
    validate_exhaustive(downstream_overinclusion);
}

void test_fixed_seed_randomized_search() {
    std::mt19937 rng(6202);
    std::uniform_real_distribution<double> weight_dist(0.25, 25.0);
    std::bernoulli_distribution arc_dist(0.35);
    for (int trial = 0; trial < 120; ++trial) {
        const int node_count = 3 + (trial % 5);
        std::vector<firebreak::opt::CompactArc> arcs;
        for (int u = 0; u < node_count; ++u) {
            for (int v = 0; v < node_count; ++v) {
                if (u == v) {
                    continue;
                }
                if (arc_dist(rng)) {
                    arcs.push_back(firebreak::opt::CompactArc{
                        u,
                        v,
                        u + 1,
                        v + 1,
                    });
                }
            }
        }
        if (arcs.empty()) {
            arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
        }

        std::vector<int> eligible;
        for (int node = 1; node < node_count; ++node) {
            eligible.push_back(node);
        }
        std::vector<double> weights;
        for (int node = 0; node < node_count; ++node) {
            weights.push_back(weight_dist(rng));
        }
        validate_exhaustive(make_instance(
            node_count,
            std::min(2, static_cast<int>(eligible.size())),
            eligible,
            arcs,
            weights));
    }
}

}  // namespace

int main() {
    test_tree_diamond_overlap_and_downstream_overinclusion();
    test_fixed_seed_randomized_search();
    std::cout << "All weighted CoverageLLBI validity tests passed.\n";
    return 0;
}
