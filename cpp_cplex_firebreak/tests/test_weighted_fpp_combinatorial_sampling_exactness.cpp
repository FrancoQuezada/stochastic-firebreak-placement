#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

firebreak::opt::OptimizationInstance make_sampling_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_sampling_exactness";
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {1};
    opt.eligible_original_nodes = {2};
    opt.compact_cell_weights = {1.0, 4.0, 100.0};

    for (int s = 0; s < 4; ++s) {
        firebreak::opt::OptimizationScenario scenario;
        scenario.scenario_id = 10 + s;
        scenario.probability = 0.25;
        scenario.ignition_index = 0;
        scenario.ignition_original_node = 1;
        scenario.observed_node_indices = {0, 1, 2};
        if (s < 2) {
            scenario.arcs = {
                firebreak::opt::CompactArc{0, 1, 1, 2},
            };
        } else {
            scenario.arcs = {
                firebreak::opt::CompactArc{0, 2, 1, 3},
            };
        }
        opt.total_arcs += scenario.arcs.size();
        opt.scenario_probabilities.push_back(scenario.probability);
        opt.scenarios.push_back(std::move(scenario));
    }
    return opt;
}

firebreak::benders::FppCombinatorialSeparationSummary separate(
    const firebreak::benders::FppCombinatorialBendersSeparator& separator,
    const std::vector<double>& eta,
    firebreak::benders::FppCombinatorialBendersScenarioOrder order,
    double ratio) {
    return separator.separateViolatedCuts(
        std::vector<double>{0.0},
        eta,
        false,
        firebreak::benders::FppCombinatorialBendersLiftMode::None,
        order,
        ratio,
        1.0e-7);
}

void test_violation_in_initial_sample_rejects_without_fallback() {
    const auto opt = make_sampling_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto summary = separate(
        separator,
        {0.0, 1000.0, 1000.0, 1000.0},
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending,
        0.5);
    assert(summary.realized_sample_size == 2);
    assert(summary.initial_sample_scenarios_evaluated == 2);
    assert(summary.fallback_scenarios_evaluated == 0);
    assert(summary.sampled_violations == 1);
    assert(summary.fallback_violations == 0);
    assert(summary.candidates_rejected_in_initial_sample == 1);
    assert(summary.candidates_rejected_in_fallback == 0);
    assert(summary.candidates_fully_verified == 0);
    assert(summary.scenarios_skipped_after_candidate_rejection == 2);
    assert(summary.scenarios_checked == 2);
    assert(summary.scenarios_skipped == 2);
    assert(summary.cuts.size() == 1);
}

void test_violation_only_outside_sample_triggers_fallback() {
    const auto opt = make_sampling_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto summary = separate(
        separator,
        {1000.0, 1000.0, 0.0, 1000.0},
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
        0.5);
    assert(summary.realized_sample_size == 2);
    assert(summary.initial_sample_scenarios_evaluated == 2);
    assert(summary.fallback_scenarios_evaluated == 2);
    assert(summary.sampled_violations == 0);
    assert(summary.fallback_violations == 1);
    assert(summary.candidates_rejected_in_initial_sample == 0);
    assert(summary.candidates_rejected_in_fallback == 1);
    assert(summary.candidates_fully_verified == 0);
    assert(summary.candidate_full_sweeps == 1);
    assert(summary.scenarios_skipped_after_candidate_rejection == 0);
    assert(summary.scenarios_checked == 4);
    assert(summary.cuts.size() == 1);
}

void test_no_violations_accepts_only_after_full_verification() {
    const auto opt = make_sampling_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto summary = separate(
        separator,
        {1000.0, 1000.0, 1000.0, 1000.0},
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
        0.5);
    assert(summary.realized_sample_size == 2);
    assert(summary.initial_sample_scenarios_evaluated == 2);
    assert(summary.fallback_scenarios_evaluated == 2);
    assert(summary.violated_cuts == 0);
    assert(summary.candidates_fully_verified == 1);
    assert(summary.candidate_full_sweeps == 1);
    assert(summary.scenarios_checked == 4);
    assert(summary.scenarios_skipped == 0);
}

void test_cvar_omitted_tail_counterexample_detected_by_fallback() {
    const auto opt = make_sampling_instance();
    firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
    const auto summary = separate(
        separator,
        {1000.0, 1000.0, 0.0, 1000.0},
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
        0.25);
    assert(summary.realized_sample_size == 1);
    assert(summary.initial_sample_scenarios_evaluated == 1);
    assert(summary.fallback_scenarios_evaluated == 3);
    assert(summary.sampled_violations == 0);
    assert(summary.fallback_violations == 1);
    assert(summary.max_violation > 100.0);
    assert(summary.candidates_rejected_in_fallback == 1);
}

void randomized_exactness_search() {
    std::mt19937 rng(62023);
    std::uniform_int_distribution<int> scenarios_dist(1, 4);
    std::uniform_real_distribution<double> ratio_dist(0.01, 1.0);
    std::uniform_real_distribution<double> eta_dist(0.0, 150.0);
    for (int trial = 0; trial < 200; ++trial) {
        auto opt = make_sampling_instance();
        opt.scenarios.resize(static_cast<std::size_t>(scenarios_dist(rng)));
        opt.scenario_probabilities.assign(opt.scenarios.size(), 1.0 / opt.scenarios.size());
        firebreak::benders::FppCombinatorialBendersSeparator separator(opt);
        std::vector<double> eta(opt.scenarios.size(), 0.0);
        for (double& value : eta) {
            value = eta_dist(rng);
        }
        const double ratio = ratio_dist(rng);
        for (const auto order : {
                 firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending,
                 firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
             }) {
            const auto sampled = separator.separateViolatedCuts(
                std::vector<double>{0.0},
                eta,
                false,
                firebreak::benders::FppCombinatorialBendersLiftMode::None,
                order,
                ratio,
                1.0e-7);
            const auto full = separator.separateViolatedCuts(
                std::vector<double>{0.0},
                eta,
                false,
                firebreak::benders::FppCombinatorialBendersLiftMode::None,
                order,
                1.0,
                1.0e-7);
            assert((sampled.violated_cuts == 0) == (full.violated_cuts == 0));
            if (sampled.candidates_fully_verified) {
                assert(sampled.scenarios_checked == static_cast<int>(opt.scenarios.size()));
                assert(full.violated_cuts == 0);
            }
            if (sampled.sampled_violations == 0) {
                assert(sampled.scenarios_checked == static_cast<int>(opt.scenarios.size()));
            }
        }
    }
}

}  // namespace

int main() {
    test_violation_in_initial_sample_rejects_without_fallback();
    test_violation_only_outside_sample_triggers_fallback();
    test_no_violations_accepts_only_after_full_verification();
    test_cvar_omitted_tail_counterexample_detected_by_fallback();
    randomized_exactness_search();
    std::cout << "All weighted FPP combinatorial sampling exactness tests passed.\n";
    return 0;
}
