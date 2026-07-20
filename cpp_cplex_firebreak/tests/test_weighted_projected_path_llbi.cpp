#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "benders/FppProjectedLlbi.hpp"
#include "benders/FppStrengthening.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-9) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_single_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_projected_path_single";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {0, 1};
    opt.eligible_original_nodes = {1, 2};
    opt.compact_cell_weights = {10.0, 2.0, 50.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 101;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

firebreak::opt::OptimizationInstance make_parallel_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_projected_path_parallel";
    opt.alpha = 0.25;
    opt.n_cells = 4;
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};
    opt.compact_cell_weights = {100.0, 2.0, 3.0, 50.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 202;
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

std::vector<double> compact_values(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<double>& eligible_values) {
    std::vector<double> values(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        values[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            eligible_values[pos];
    }
    return values;
}

double coefficient_for(const firebreak::benders::BendersCut& cut, int compact_node) {
    for (const auto& [node, coefficient] : cut.coefficients_by_compact_index) {
        if (node == compact_node) {
            return coefficient;
        }
    }
    return 0.0;
}

double projected_path_expression(
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
        if (std::isfinite(best) && best < 1.0) {
            value += node.cell_weight * (1.0 - best);
        }
    }
    return value;
}

void test_manual_single_path_cut_and_ignition() {
    const auto opt = make_single_path_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_exp = true;
    options.max_cuts_per_round = 10;
    const std::vector<double> ybar = {0.0, 0.25};
    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar,
        {0.0});

    assert(separated.cuts.size() == 1);
    assert(separated.projected_path_llbi_weighted);
    assert(separated.projected_path_llbi_mode == "exp-exact-stored-path-separation");
    assert(separated.projected_path_llbi_validity_mode == "exact-directed-path-projection");
    const auto& cut = separated.cuts.front().cut;
    assert_close(cut.rhs_constant, 62.0);
    assert_close(coefficient_for(cut, 0), 0.0);
    assert_close(coefficient_for(cut, 1), -52.0);
    assert_close(cut.evaluateAt(compact_values(opt, ybar)), 49.0);

    const auto data = firebreak::benders::build_fpp_path_llbi_data(opt, true, 8);
    assert_close(projected_path_expression(data.scenarios.front(), compact_values(opt, ybar)), 49.0);
}

void test_parallel_paths_choose_minimum_fractional_path() {
    const auto opt = make_parallel_path_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_exp = true;
    options.max_cuts_per_round = 10;
    const std::vector<double> ybar = {0.0, 0.20, 0.70};
    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar,
        {0.0});

    assert(separated.cuts.size() == 1);
    const auto& cut = separated.cuts.front().cut;
    assert_close(cut.rhs_constant, 155.0);
    assert_close(coefficient_for(cut, 1), -52.0);
    assert_close(coefficient_for(cut, 2), -3.0);
    assert_close(cut.evaluateAt(compact_values(opt, ybar)), 142.5);
}

void test_poly_is_weighted_first_stored_path_subset() {
    const auto opt = make_parallel_path_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_poly = true;
    firebreak::benders::FppProjectedLlbiStats stats;
    const auto cuts =
        firebreak::benders::build_fpp_projected_llbi_poly_cuts(opt, options, &stats);

    assert(cuts.size() == 1);
    const auto& cut = cuts.front();
    assert_close(cut.rhs_constant, 155.0);
    assert_close(coefficient_for(cut, 1), -52.0);
    assert_close(coefficient_for(cut, 2), -3.0);
    assert(stats.projected_path_llbi_weighted);
    assert(stats.projected_path_llbi_mode == "poly-first-stored-path");
    assert(stats.projected_path_llbi_validity_mode ==
           "weighted-fixed-subset-of-directed-path-projection");
    assert(stats.projected_path_llbi_cuts_generated == 1);
    assert(stats.projected_path_llbi_cuts_added == 1);
}

}  // namespace

int main() {
    test_manual_single_path_cut_and_ignition();
    test_parallel_paths_choose_minimum_fractional_path();
    test_poly_is_weighted_first_stored_path_subset();
    std::cout << "All weighted projected PathLLBI tests passed.\n";
    return 0;
}
