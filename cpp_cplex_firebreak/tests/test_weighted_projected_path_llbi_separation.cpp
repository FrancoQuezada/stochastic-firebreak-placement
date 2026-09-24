#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "benders/FppProjectedLlbi.hpp"

namespace {

firebreak::opt::OptimizationInstance make_tie_instance(const std::vector<double>& weights) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_projected_path_separation";
    opt.alpha = 0.25;
    opt.n_cells = 4;
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};
    opt.compact_cell_weights = weights;

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 77;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

double coefficient_for(const firebreak::benders::BendersCut& cut, int compact_node) {
    for (const auto& [node, coefficient] : cut.coefficients_by_compact_index) {
        if (node == compact_node) {
            return coefficient;
        }
    }
    return 0.0;
}

std::string signature(const firebreak::benders::BendersCut& cut) {
    std::ostringstream out;
    out << cut.scenario_id << "|" << cut.rhs_constant;
    for (const auto& [node, coefficient] : cut.coefficients_by_compact_index) {
        out << "|" << node << ":" << coefficient;
    }
    return out.str();
}

void test_deterministic_tie_breaking() {
    const auto opt = make_tie_instance({100.0, 2.0, 3.0, 50.0});
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_exp = true;
    options.max_cuts_per_round = 10;
    const std::vector<double> ybar = {0.40, 0.40};
    const auto first = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar,
        {0.0});
    const auto second = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar,
        {0.0});
    assert(first.cuts.size() == 1);
    assert(second.cuts.size() == 1);
    assert(signature(first.cuts.front().cut) == signature(second.cuts.front().cut));
    assert(coefficient_for(first.cuts.front().cut, 1) == -52.0);
    assert(coefficient_for(first.cuts.front().cut, 2) == -3.0);
}

void test_same_support_different_weights_is_distinct() {
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_exp = true;
    options.max_cuts_per_round = 10;
    const std::vector<double> ybar = {0.20, 0.70};
    const auto first = firebreak::benders::separate_fpp_projected_llbi_cuts(
        make_tie_instance({100.0, 2.0, 3.0, 50.0}),
        options,
        ybar,
        {0.0});
    const auto second = firebreak::benders::separate_fpp_projected_llbi_cuts(
        make_tie_instance({100.0, 20.0, 30.0, 500.0}),
        options,
        ybar,
        {0.0});
    assert(first.cuts.size() == 1);
    assert(second.cuts.size() == 1);
    assert(signature(first.cuts.front().cut) != signature(second.cuts.front().cut));
}

void test_saturation_tolerance_skips_blocked_paths() {
    const auto opt = make_tie_instance({100.0, 2.0, 3.0, 50.0});
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_exp = true;
    options.max_cuts_per_round = 10;
    options.violation_tolerance = 1.0e-8;
    const std::vector<double> ybar = {1.0, 1.0};
    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar,
        {100.0});
    assert(separated.cuts.empty());
    assert(separated.violated_cuts_found == 0);
}

}  // namespace

int main() {
    test_deterministic_tie_breaking();
    test_same_support_different_weights_is_distinct();
    test_saturation_tolerance_skips_blocked_paths();
    std::cout << "All weighted projected PathLLBI separation tests passed.\n";
    return 0;
}
