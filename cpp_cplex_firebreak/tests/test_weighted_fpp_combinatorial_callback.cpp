#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

firebreak::opt::OptimizationInstance make_multi_scenario_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_callback";
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};
    opt.compact_cell_weights = {10.0, 2.0, 30.0, 4.0};
    for (int s = 0; s < 3; ++s) {
        firebreak::opt::OptimizationScenario scenario;
        scenario.scenario_id = 100 + s;
        scenario.probability = 1.0 / 3.0;
        scenario.ignition_index = 0;
        scenario.ignition_original_node = 1;
        scenario.observed_node_indices = {0, 1, 2, 3};
        if (s == 0) {
            scenario.arcs = {
                firebreak::opt::CompactArc{0, 1, 1, 2},
                firebreak::opt::CompactArc{1, 3, 2, 4},
            };
        } else if (s == 1) {
            scenario.arcs = {
                firebreak::opt::CompactArc{0, 2, 1, 3},
                firebreak::opt::CompactArc{2, 3, 3, 4},
            };
        } else {
            scenario.arcs = {
                firebreak::opt::CompactArc{0, 1, 1, 2},
                firebreak::opt::CompactArc{1, 2, 2, 3},
                firebreak::opt::CompactArc{2, 3, 3, 4},
            };
        }
        opt.total_arcs += scenario.arcs.size();
        opt.scenario_probabilities.push_back(scenario.probability);
        opt.scenarios.push_back(std::move(scenario));
    }
    return opt;
}

firebreak::benders::FppCombinatorialBendersOptions baseline_options() {
    firebreak::benders::FppCombinatorialBendersOptions options;
    options.enabled = true;
    options.lift_mode = firebreak::benders::FppCombinatorialBendersLiftMode::None;
    options.scenario_order =
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending;
    options.cut_sampling_ratio = 1.0;
    options.separate_fractional = false;
    options.initial_cuts = false;
    return options;
}

void expect_rejected(
    const firebreak::benders::FppCombinatorialBendersOptions& options,
    bool root_cuts,
    bool llbi,
    const firebreak::benders::FppStrengtheningOptions& strengthening,
    const std::string& expected_text) {
    bool threw = false;
    try {
        firebreak::benders::validate_fpp_phase6c1_weighted_combinatorial_baseline(
            options,
            root_cuts,
            llbi,
            strengthening);
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find(expected_text) != std::string::npos;
    }
    assert(threw);
}

void test_all_scenarios_checked_without_sampling() {
    const auto opt = make_multi_scenario_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto options = baseline_options();
    firebreak::benders::validate_fpp_phase6c1_weighted_combinatorial_baseline(
        options,
        false,
        false,
        firebreak::benders::FppStrengtheningOptions());

    const auto summary = separator.separateViolatedCuts(
        std::vector<double>{0.0, 0.0},
        std::vector<double>{0.0, 0.0, 0.0},
        false,
        options.lift_mode,
        options.scenario_order,
        options.cut_sampling_ratio,
        1.0e-7);
    assert(summary.scenarios_checked == 3);
    assert(summary.scenarios_skipped == 0);
    assert(summary.weighted_recourse_evaluations == 3);
    assert(summary.violated_cuts == 3);
    assert(summary.tight_cuts == 3);
    assert(summary.max_tightness_error <= 1.0e-7);
}

void test_phase6c1_rejections() {
    auto options = baseline_options();
    firebreak::benders::FppStrengtheningOptions strengthening;

    auto sampled = options;
    sampled.cut_sampling_ratio = 0.5;
    expect_rejected(sampled, false, false, strengthening, "cut_sampling_ratio=1");

    auto lifted = options;
    lifted.lift_mode = firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic;
    expect_rejected(lifted, false, false, strengthening, "lift_mode=none");

    auto fractional = options;
    fractional.separate_fractional = true;
    expect_rejected(fractional, false, false, strengthening, "separate_fractional=false");

    auto initial = options;
    initial.initial_cuts = true;
    expect_rejected(initial, false, false, strengthening, "initial_cuts=false");

    auto eta_desc = options;
    eta_desc.scenario_order =
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending;
    expect_rejected(eta_desc, false, false, strengthening, "scenario_order=eta-asc");

    expect_rejected(options, true, false, strengthening, "root user cuts");
    expect_rejected(options, false, true, strengthening, "LLBI");

    strengthening.use_global_dominance_preprocessing = true;
    expect_rejected(options, false, false, strengthening, "global dominance disabled");

    strengthening = firebreak::benders::FppStrengtheningOptions();
    strengthening.use_projected_path_llbi_exp = true;
    expect_rejected(options, false, false, strengthening, "LLBI");
}

}  // namespace

int main() {
    test_all_scenarios_checked_without_sampling();
    test_phase6c1_rejections();
    std::cout << "All weighted FPP combinatorial callback policy tests passed.\n";
    return 0;
}
