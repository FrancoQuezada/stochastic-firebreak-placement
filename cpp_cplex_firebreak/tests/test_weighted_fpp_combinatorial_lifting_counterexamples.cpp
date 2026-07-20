#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"
#include "core/LandscapeWeightMap.hpp"

namespace {

std::vector<int> int_from_mask(std::size_t n, int mask) {
    std::vector<int> y(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = ((mask >> static_cast<int>(i)) & 1) != 0 ? 1 : 0;
    }
    return y;
}

std::vector<double> double_from_int(const std::vector<int>& y) {
    std::vector<double> out;
    out.reserve(y.size());
    for (const int value : y) {
        out.push_back(static_cast<double>(value));
    }
    return out;
}

double cut_rhs(
    const firebreak::benders::BendersCut& cut,
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<int>& y_by_position) {
    std::vector<double> y_compact(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < y_by_position.size(); ++pos) {
        y_compact[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            static_cast<double>(y_by_position[pos]);
    }
    return cut.evaluateAt(y_compact);
}

firebreak::opt::OptimizationInstance random_instance(std::mt19937& rng, int trial) {
    const int n = 5;
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_lifting_counterexample";
    opt.budget = 1 + (trial % 3);
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 2, 3};
    opt.eligible_original_nodes = {2, 3, 4};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 2000 + trial;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};

    std::bernoulli_distribution arc_pick(0.42);
    for (int u = 0; u < n; ++u) {
        for (int v = 0; v < n; ++v) {
            if (u == v) {
                continue;
            }
            if (arc_pick(rng)) {
                scenario.arcs.push_back(firebreak::opt::CompactArc{u, v, u + 1, v + 1});
            }
        }
    }
    if (scenario.arcs.empty()) {
        scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    }
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();

    std::uniform_real_distribution<double> weight_dist(0.25, 100.0);
    std::vector<firebreak::core::LandscapeWeightRecord> records;
    for (int node = 1; node <= n; ++node) {
        const double weight = weight_dist(rng);
        records.push_back({node, weight, weight, node % 2});
    }
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        static_cast<std::uint64_t>(620300 + trial),
        false,
        records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

void search_random_counterexamples() {
    std::mt19937 rng(6203);
    const std::vector<firebreak::benders::FppCombinatorialBendersLiftMode> modes = {
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        firebreak::benders::FppCombinatorialBendersLiftMode::Posterior,
    };
    for (int trial = 0; trial < 150; ++trial) {
        const auto opt = random_instance(rng, trial);
        firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
        for (const auto mode : modes) {
            for (int incumbent_mask = 0; incumbent_mask < (1 << 3); ++incumbent_mask) {
                const auto incumbent = int_from_mask(3, incumbent_mask);
                int incumbent_count = 0;
                for (const int value : incumbent) {
                    incumbent_count += value;
                }
                if (incumbent_count > opt.budget) {
                    continue;
                }
                const auto incumbent_loss = separator.evaluateScenarioLosses(incumbent)[0];
                const auto separated = separator.separateScenario(
                    0,
                    double_from_int(incumbent),
                    0.0,
                    false,
                    mode,
                    1.0e-7);
                assert(separated.lifted_dominates_baseline);
                assert(separated.lifted_rhs_at_ybar <= incumbent_loss + 1.0e-7);
                for (int mask = 0; mask < (1 << 3); ++mask) {
                    const auto y = int_from_mask(3, mask);
                    int count = 0;
                    for (const int value : y) {
                        count += value;
                    }
                    if (count > opt.budget) {
                        continue;
                    }
                    const double loss = separator.evaluateScenarioLosses(y)[0];
                    const double baseline_rhs = cut_rhs(separated.baseline_cut, opt, y);
                    const double lifted_rhs = cut_rhs(separated.cut, opt, y);
                    assert(baseline_rhs <= loss + 1.0e-7);
                    assert(lifted_rhs <= loss + 1.0e-7);
                    assert(lifted_rhs + 1.0e-7 >= baseline_rhs);
                }
            }
        }
    }
}

}  // namespace

int main() {
    search_random_counterexamples();
    std::cout << "All weighted FPP combinatorial lifting counterexample tests passed.\n";
    return 0;
}
