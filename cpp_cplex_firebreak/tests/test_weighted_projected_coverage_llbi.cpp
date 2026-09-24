#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "benders/FppProjectedLlbi.hpp"
#include "benders/FppStrengthening.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-9) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_overlap_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_projected_coverage_llbi_overlap";
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

double exact_projected_family_rhs(
    const firebreak::benders::FppCoverageLlbiScenarioRecord& scenario,
    const std::vector<double>& compact_y) {
    const int n = static_cast<int>(scenario.nodes.size());
    double best = -std::numeric_limits<double>::infinity();
    for (int mask = 0; mask < (1 << n); ++mask) {
        double rhs = scenario.empty_burned_area;
        for (int pos = 0; pos < n; ++pos) {
            const auto& node = scenario.nodes[static_cast<std::size_t>(pos)];
            if ((mask & (1 << pos)) == 0) {
                rhs -= node.cell_weight;
                continue;
            }
            double cover_sum = 0.0;
            for (const int candidate : node.covering_candidate_compact_nodes) {
                cover_sum += compact_y[static_cast<std::size_t>(candidate)];
            }
            rhs -= node.cell_weight * cover_sum;
        }
        best = std::max(best, rhs);
    }
    return best;
}

std::string cut_signature(const firebreak::benders::BendersCut& cut) {
    std::ostringstream out;
    out << cut.scenario_id << "|" << cut.rhs_constant;
    for (const auto& [node, coefficient] : cut.coefficients_by_compact_index) {
        out << "|" << node << ":" << coefficient;
    }
    return out.str();
}

void test_poly_uses_destination_cell_weights() {
    const auto opt = make_weighted_overlap_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_coverage_llbi_poly = true;
    firebreak::benders::FppProjectedLlbiStats stats;
    const auto cuts =
        firebreak::benders::build_fpp_projected_llbi_poly_cuts(opt, options, &stats);

    assert(cuts.size() == 1);
    const auto& cut = cuts.front();
    assert_close(cut.rhs_constant, 162.0);
    assert_close(coefficient_for(cut, 1), -59.0);
    assert_close(coefficient_for(cut, 2), -53.0);
    assert_close(coefficient_for(cut, 4), -7.0);
    assert_close(coefficient_for(cut, 5), 0.0);
    assert(stats.projected_coverage_llbi_weighted);
    assert(stats.projected_coverage_llbi_mode == "poly-all-unsaturated-support");
    assert(stats.projected_coverage_llbi_scenarios_precomputed == 1);
    assert(stats.projected_coverage_llbi_baseline_cells == 5);
    assert(stats.projected_coverage_llbi_nonempty_coverage_sets == 4);
    assert(stats.projected_coverage_llbi_total_incidence_terms == 6);
    assert(stats.projected_coverage_llbi_cuts_generated == 1);
    assert(stats.projected_coverage_llbi_cuts_added == 1);
    assert(stats.projected_coverage_llbi_validity_mode ==
           "weighted-subset-of-exact-per-cell-capped-coverage-projection");
}

void test_exp_separation_matches_exact_projected_family() {
    const auto opt = make_weighted_overlap_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_coverage_llbi_exp = true;
    options.max_cuts_per_round = 10;
    options.violation_tolerance = 1.0e-8;
    const std::vector<double> ybar_by_eligible = {0.20, 0.90, 0.30, 0.00};
    const std::vector<double> etabar = {0.0};

    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar_by_eligible,
        etabar);

    assert(separated.scenarios_checked == 1);
    assert(separated.violated_cuts_found == 1);
    assert(separated.cuts.size() == 1);
    assert(separated.projected_coverage_llbi_weighted);
    assert(separated.projected_coverage_llbi_mode == "exp-exact-separated");
    assert(separated.projected_coverage_llbi_validity_mode ==
           "weighted-exact-per-cell-capped-coverage-projection");

    const auto& cut = separated.cuts.front().cut;
    assert_close(cut.rhs_constant, 112.0);
    assert_close(coefficient_for(cut, 1), -9.0);
    assert_close(coefficient_for(cut, 2), -3.0);
    assert_close(coefficient_for(cut, 4), -7.0);
    assert_close(separated.cuts.front().violation, 105.4);

    const auto compact_y = compact_values(opt, ybar_by_eligible);
    const auto data = firebreak::benders::build_fpp_coverage_llbi_data(opt, true);
    assert_close(cut.evaluateAt(compact_y), exact_projected_family_rhs(data.scenarios.front(), compact_y));
}

void test_repeated_separation_has_stable_duplicate_signature() {
    const auto opt = make_weighted_overlap_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_coverage_llbi_exp = true;
    options.max_cuts_per_round = 10;
    const std::vector<double> ybar_by_eligible = {0.20, 0.90, 0.30, 0.00};
    const std::vector<double> etabar = {0.0};

    const auto first = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar_by_eligible,
        etabar);
    const auto second = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar_by_eligible,
        etabar);
    assert(first.cuts.size() == 1);
    assert(second.cuts.size() == 1);
    assert(cut_signature(first.cuts.front().cut) == cut_signature(second.cuts.front().cut));
}

}  // namespace

int main() {
    test_poly_uses_destination_cell_weights();
    test_exp_separation_matches_exact_projected_family();
    test_repeated_separation_has_stable_duplicate_signature();
    std::cout << "All weighted projected CoverageLLBI tests passed.\n";
    return 0;
}
