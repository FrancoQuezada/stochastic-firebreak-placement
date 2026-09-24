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
    const std::vector<double>& weights) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_projected_coverage_llbi_validity";
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

std::vector<double> compact_from_eligible(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<double>& eligible_values) {
    std::vector<double> compact(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        compact[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            eligible_values[pos];
    }
    return compact;
}

void check_binary_selection(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::benders::FppCoverageLlbiScenarioRecord& scenario,
    const std::vector<double>& eligible_y) {
    const auto compact_y = compact_from_eligible(opt, eligible_y);
    const double projected_rhs = exact_projected_family_rhs(scenario, compact_y);
    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        if (eligible_y[pos] > 0.5) {
            selected[static_cast<std::size_t>(opt.eligible_indices[pos])] = 1;
        }
    }
    const double actual = firebreak::benders::evaluate_fixed_y_fpp_loss(
        opt,
        scenario.scenario_index,
        selected).weighted_burn_loss;
    if (projected_rhs > actual + 1.0e-8) {
        throw std::runtime_error("Projected CoverageLLBI bound exceeded exact recourse.");
    }
}

void enumerate_binary(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::benders::FppCoverageLlbiScenarioRecord& scenario,
    int pos,
    int selected_count,
    std::vector<double>& eligible_y) {
    if (pos == static_cast<int>(eligible_y.size())) {
        check_binary_selection(opt, scenario, eligible_y);
        return;
    }
    eligible_y[static_cast<std::size_t>(pos)] = 0.0;
    enumerate_binary(opt, scenario, pos + 1, selected_count, eligible_y);
    if (selected_count < opt.budget) {
        eligible_y[static_cast<std::size_t>(pos)] = 1.0;
        enumerate_binary(opt, scenario, pos + 1, selected_count + 1, eligible_y);
    }
    eligible_y[static_cast<std::size_t>(pos)] = 0.0;
}

void validate_instance(const firebreak::opt::OptimizationInstance& opt) {
    const auto data = firebreak::benders::build_fpp_coverage_llbi_data(opt, true);
    assert(data.enabled);
    assert(data.scenarios.size() == 1);
    std::vector<double> eligible_y(opt.eligible_indices.size(), 0.0);
    enumerate_binary(opt, data.scenarios.front(), 0, 0, eligible_y);

    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_coverage_llbi_exp = true;
    options.max_cuts_per_round = 10;
    eligible_y.assign(opt.eligible_indices.size(), 0.35);
    const auto compact_y = compact_from_eligible(opt, eligible_y);
    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        eligible_y,
        {0.0});
    if (!separated.cuts.empty()) {
        const double exact_rhs =
            exact_projected_family_rhs(data.scenarios.front(), compact_y);
        assert(std::fabs(separated.cuts.front().cut.evaluateAt(compact_y) - exact_rhs) <= 1.0e-8);
    }
}

void test_structured_counterexamples() {
    validate_instance(make_instance(
        4,
        2,
        {1, 2},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        },
        {1.0, 2.0, 7.0, 13.0}));

    validate_instance(make_instance(
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
}

void test_fixed_seed_randomized_search() {
    std::mt19937 rng(6301);
    std::uniform_real_distribution<double> weight_dist(0.25, 25.0);
    std::bernoulli_distribution arc_dist(0.35);
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
        validate_instance(make_instance(node_count, 2, eligible, arcs, weights));
    }
}

}  // namespace

int main() {
    test_structured_counterexamples();
    test_fixed_seed_randomized_search();
    std::cout << "All weighted projected CoverageLLBI validity tests passed.\n";
    return 0;
}
