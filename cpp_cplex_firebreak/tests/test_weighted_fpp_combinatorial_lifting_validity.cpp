#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"
#include "core/LandscapeWeightMap.hpp"

namespace {

firebreak::opt::OptimizationInstance make_diamond_with_noneligible() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_lifting_validity";
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 2, 4};
    opt.eligible_original_nodes = {2, 3, 5};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 11;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 3, 3, 4},
        firebreak::opt::CompactArc{3, 4, 4, 5},
        firebreak::opt::CompactArc{4, 2, 5, 3},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        6202,
        false,
        {
            {1, 10.0, 10.0, 0},
            {2, 1.0, 1.0, 0},
            {3, 40.0, 40.0, 1},
            {4, 9.0, 9.0, 0},
            {5, 80.0, 80.0, 2},
        });
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

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

void validate_mode(firebreak::benders::FppCombinatorialBendersLiftMode mode) {
    const auto opt = make_diamond_with_noneligible();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    for (int incumbent_mask = 0; incumbent_mask < (1 << 3); ++incumbent_mask) {
        const auto incumbent = int_from_mask(3, incumbent_mask);
        int incumbent_count = 0;
        for (const int value : incumbent) {
            incumbent_count += value;
        }
        if (incumbent_count > opt.budget) {
            continue;
        }
        const auto incumbent_losses = separator.evaluateScenarioLosses(incumbent);
        const auto separated = separator.separateScenario(
            0,
            double_from_int(incumbent),
            0.0,
            false,
            mode,
            1.0e-7);
        assert(std::fabs(separated.baseline_rhs_at_ybar - incumbent_losses[0]) <= 1.0e-7);
        assert(std::fabs(separated.lifted_rhs_at_ybar - incumbent_losses[0]) <= 1.0e-7);
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

}  // namespace

int main() {
    validate_mode(firebreak::benders::FppCombinatorialBendersLiftMode::None);
    validate_mode(firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic);
    validate_mode(firebreak::benders::FppCombinatorialBendersLiftMode::Posterior);
    std::cout << "All weighted FPP combinatorial lifting validity tests passed.\n";
    return 0;
}
