#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

firebreak::opt::OptimizationInstance make_weighted_diamond() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_fractional_diamond";
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 2, 3};
    opt.eligible_original_nodes = {2, 3, 4};
    opt.compact_cell_weights = {0.01, 0.1, 1.0, 10.0, 100.0};
    opt.cell_weight_map.deterministic_hash = "fractional-test-hash";

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 17;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{0, 3, 1, 4},
        firebreak::opt::CompactArc{1, 4, 2, 5},
        firebreak::opt::CompactArc{2, 4, 3, 5},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = static_cast<int>(scenario.arcs.size());
    return opt;
}

std::vector<double> compact_y(const firebreak::opt::OptimizationInstance& opt,
                              const std::vector<int>& eligible_y) {
    std::vector<double> compact(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < eligible_y.size(); ++pos) {
        compact[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            static_cast<double>(eligible_y[pos]);
    }
    return compact;
}

void test_fractional_cut_is_nonlifted_weighted_path_cut() {
    const auto opt = make_weighted_diamond();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto cut = separator.separateScenario(
        0,
        std::vector<double>{0.3, 0.35, 0.2},
        0.0,
        true,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        1.0e-8);

    assert(cut.fractional);
    assert(cut.lift_mode_fallback);
    assert(!cut.lifting_attempted);
    assert(cut.active_nodes == 5);
    assert(std::fabs(cut.incumbent_weighted_loss - 111.11) <= 1.0e-8);
    assert(cut.cut.rhs_constant == cut.incumbent_weighted_loss);
    assert(cut.cut.evaluateAt(std::vector<double>{0.0, 0.3, 0.35, 0.2, 0.0}) > 0.0);
}

void test_fractional_cut_binary_and_convex_hull_validity() {
    const auto opt = make_weighted_diamond();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto separated = separator.separateScenario(
        0,
        std::vector<double>{0.3, 0.35, 0.2},
        0.0,
        true,
        firebreak::benders::FppCombinatorialBendersLiftMode::None,
        1.0e-8);
    const auto& cut = separated.cut;

    struct Vertex {
        std::vector<int> y;
        std::vector<double> compact;
        double loss = 0.0;
        double rhs = 0.0;
    };

    std::vector<Vertex> vertices;
    for (int a = 0; a <= 1; ++a) {
        for (int b = 0; b <= 1; ++b) {
            for (int c = 0; c <= 1; ++c) {
                if (a + b + c > opt.budget) {
                    continue;
                }
                Vertex vertex;
                vertex.y = {a, b, c};
                vertex.compact = compact_y(opt, vertex.y);
                vertex.loss = separator.evaluateScenarioLosses(vertex.y).at(0);
                vertex.rhs = cut.evaluateAt(vertex.compact);
                assert(vertex.rhs <= vertex.loss + 1.0e-8);
                vertices.push_back(std::move(vertex));
            }
        }
    }

    for (std::size_t i = 0; i < vertices.size(); ++i) {
        for (std::size_t j = 0; j < vertices.size(); ++j) {
            for (int step = 0; step <= 10; ++step) {
                const double lambda = static_cast<double>(step) / 10.0;
                std::vector<double> y_mix(vertices[i].compact.size(), 0.0);
                for (std::size_t n = 0; n < y_mix.size(); ++n) {
                    y_mix[n] = lambda * vertices[i].compact[n] +
                        (1.0 - lambda) * vertices[j].compact[n];
                }
                const double q_mix = lambda * vertices[i].loss +
                    (1.0 - lambda) * vertices[j].loss;
                assert(cut.evaluateAt(y_mix) <= q_mix + 1.0e-8);
            }
        }
    }
}

void test_full_fractional_summary_policy() {
    const auto opt = make_weighted_diamond();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    firebreak::benders::FppCombinatorialBendersOptions options;
    options.enabled = true;
    options.lift_mode = firebreak::benders::FppCombinatorialBendersLiftMode::Posterior;
    options.scenario_order =
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending;
    options.cut_sampling_ratio = 1.0;
    options.initial_cuts = true;
    options.separate_fractional = true;
    firebreak::benders::validate_fpp_phase6c2b_weighted_combinatorial_mode(
        options,
        false,
        false,
        firebreak::benders::FppStrengtheningOptions());

    const auto summary = separator.separateViolatedCuts(
        std::vector<double>{0.3, 0.35, 0.2},
        std::vector<double>{0.0},
        true,
        options.lift_mode,
        options.scenario_order,
        options.cut_sampling_ratio,
        1.0e-8);
    assert(summary.scenarios_checked == 1);
    assert(summary.weighted_recourse_evaluations == 1);
    assert(summary.violated_cuts == 1);
    assert(summary.lift_fallback_count == 1);
    assert(summary.lifting_attempts == 0);
}

}  // namespace

int main() {
    test_fractional_cut_is_nonlifted_weighted_path_cut();
    test_fractional_cut_binary_and_convex_hull_validity();
    test_full_fractional_summary_policy();
    std::cout << "All weighted FPP combinatorial fractional validity tests passed.\n";
    return 0;
}
