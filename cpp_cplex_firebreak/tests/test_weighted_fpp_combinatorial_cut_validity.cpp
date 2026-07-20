#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_instance(
    int node_count,
    const std::vector<int>& eligible,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    const std::vector<double>& weights,
    int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_validity";
    opt.budget = budget;
    std::vector<int> originals;
    for (int i = 0; i < node_count; ++i) {
        originals.push_back(i + 1);
    }
    opt.node_mapper.build_from_nodes(originals);
    opt.eligible_indices = eligible;
    for (const int node : eligible) {
        opt.eligible_original_nodes.push_back(node + 1);
    }
    opt.compact_cell_weights = weights;

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    for (int i = 0; i < node_count; ++i) {
        scenario.observed_node_indices.push_back(i);
    }
    scenario.arcs = arcs;
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = arcs.size();
    return opt;
}

std::vector<double> expand_y(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<int>& y) {
    std::vector<double> compact(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < y.size(); ++pos) {
        compact[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            static_cast<double>(y[pos]);
    }
    return compact;
}

int selected_count(const std::vector<int>& y) {
    int count = 0;
    for (const int value : y) {
        count += value != 0 ? 1 : 0;
    }
    return count;
}

void verify_cut_validity(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<int>& incumbent) {
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    std::vector<double> ybar;
    for (const int value : incumbent) {
        ybar.push_back(static_cast<double>(value));
    }
    const auto cut = separator.separateScenario(
        0,
        ybar,
        0.0,
        false,
        firebreak::benders::FppCombinatorialBendersLiftMode::None,
        1.0e-7);
    const auto incumbent_loss = separator.evaluateScenarioLosses(incumbent);
    assert_close(cut.rhs_at_ybar, incumbent_loss[0]);
    assert(cut.tightness_error <= 1.0e-7);

    const int n = static_cast<int>(opt.eligible_indices.size());
    const int total = 1 << n;
    for (int mask = 0; mask < total; ++mask) {
        std::vector<int> y(static_cast<std::size_t>(n), 0);
        for (int pos = 0; pos < n; ++pos) {
            y[static_cast<std::size_t>(pos)] = (mask & (1 << pos)) ? 1 : 0;
        }
        if (selected_count(y) > opt.budget) {
            continue;
        }
        const auto losses = separator.evaluateScenarioLosses(y);
        const double rhs = cut.cut.evaluateAt(expand_y(opt, y));
        assert(rhs <= losses[0] + 1.0e-7);
    }
}

void test_fixed_tiny_graphs() {
    verify_cut_validity(
        make_instance(
            5,
            {1, 2, 4},
            {
                firebreak::opt::CompactArc{0, 1, 1, 2},
                firebreak::opt::CompactArc{1, 3, 2, 4},
                firebreak::opt::CompactArc{0, 2, 1, 3},
                firebreak::opt::CompactArc{2, 3, 3, 4},
                firebreak::opt::CompactArc{3, 4, 4, 5},
            },
            {11.0, 2.0, 90.0, 7.0, 41.0},
            2),
        {0, 1, 0});

    verify_cut_validity(
        make_instance(
            4,
            {0, 1, 2},
            {
                firebreak::opt::CompactArc{0, 1, 1, 2},
                firebreak::opt::CompactArc{1, 2, 2, 3},
                firebreak::opt::CompactArc{2, 1, 3, 2},
                firebreak::opt::CompactArc{2, 3, 3, 4},
            },
            {100.0, 1.5, 60.0, 4.0},
            2),
        {1, 0, 1});
}

void test_randomized_fixed_seed_validity() {
    std::mt19937 rng(6201);
    for (int trial = 0; trial < 120; ++trial) {
        const int node_count = 4 + static_cast<int>(rng() % 3);
        std::vector<int> eligible;
        for (int node = 0; node < node_count; ++node) {
            if (node == 0 || (rng() % 100) < 70) {
                eligible.push_back(node);
            }
        }
        if (eligible.empty()) {
            eligible.push_back(0);
        }
        const int budget = std::max(1, static_cast<int>(eligible.size()) / 2);

        std::vector<firebreak::opt::CompactArc> arcs;
        for (int u = 0; u < node_count; ++u) {
            for (int v = 0; v < node_count; ++v) {
                if (u == v) {
                    continue;
                }
                if ((rng() % 100) < 35) {
                    arcs.push_back(firebreak::opt::CompactArc{u, v, u + 1, v + 1});
                }
            }
        }
        if (arcs.empty()) {
            arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
        }

        std::vector<double> weights;
        for (int node = 0; node < node_count; ++node) {
            weights.push_back(0.25 + static_cast<double>(rng() % 200) / 7.0);
        }
        weights[0] = 100.0 + static_cast<double>(trial % 17);

        std::vector<int> incumbent(eligible.size(), 0);
        int selected = 0;
        for (std::size_t pos = 0; pos < incumbent.size(); ++pos) {
            if (selected < budget && (rng() % 100) < 45) {
                incumbent[pos] = 1;
                ++selected;
            }
        }

        verify_cut_validity(
            make_instance(node_count, eligible, arcs, weights, budget),
            incumbent);
    }
}

}  // namespace

int main() {
    test_fixed_tiny_graphs();
    test_randomized_fixed_seed_validity();
    std::cout << "All weighted FPP combinatorial cut validity tests passed.\n";
    return 0;
}
