#include <cassert>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "opt/DpvIndexBuilder.hpp"
#include "opt/WeightedDpvScoring.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_weighted_dpv_cache";
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};
    opt.compact_cell_weights = {1.0, 2.0, 3.0};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    };
    scenario.observed_node_indices = {0, 1, 2};
    firebreak::opt::DpvIndexBuilder builder;
    scenario.dpv = builder.build_for_scenario(scenario, opt.node_mapper);
    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.num_pairs();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

bool throws_with_message(const std::function<void()>& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::runtime_error& exc) {
        return std::string(exc.what()).find(needle) != std::string::npos;
    }
    return false;
}

void test_structural_cache_is_weight_independent_but_numeric_cache_is_not() {
    const auto opt = make_instance();
    firebreak::opt::WeightedDpvScoringOptions options;
    const auto structural = firebreak::opt::build_weighted_dpv_structural_data(
        opt,
        opt.eligible_indices,
        options);
    const auto first = firebreak::opt::evaluate_weighted_dpv_scores(
        opt,
        structural,
        {1.0, 2.0, 3.0},
        options);
    const auto second = firebreak::opt::evaluate_weighted_dpv_scores(
        opt,
        structural,
        {1.0, 20.0, 30.0},
        options);

    assert(first.structural_data.cache_key.digest == second.structural_data.cache_key.digest);
    assert(first.numerical_cache_key.weight_map_hash != second.numerical_cache_key.weight_map_hash);
    assert(first.numerical_cache_key.digest != second.numerical_cache_key.digest);
}

void test_cache_mismatch_fails_clearly() {
    const auto opt = make_instance();
    firebreak::opt::WeightedDpvScoringOptions options;
    auto structural = firebreak::opt::build_weighted_dpv_structural_data(
        opt,
        {1},
        options);
    structural.cache_key.variant = "wrong";

    assert(throws_with_message([&]() {
        (void)firebreak::opt::evaluate_weighted_dpv_scores(
            opt,
            structural,
            {1.0, 2.0, 3.0},
            options);
    }, "variant mismatch"));
}

void test_probability_mismatch_fails_clearly() {
    auto opt = make_instance();
    firebreak::opt::WeightedDpvScoringOptions options;
    const auto structural = firebreak::opt::build_weighted_dpv_structural_data(
        opt,
        {1},
        options);
    opt.scenarios.front().probability = 0.5;

    assert(throws_with_message([&]() {
        (void)firebreak::opt::evaluate_weighted_dpv_scores(
            opt,
            structural,
            {1.0, 2.0, 3.0},
            options);
    }, "probability mismatch"));
}

void test_missing_and_nonfinite_weights_fail_before_scoring() {
    auto opt = make_instance();
    opt.compact_cell_weights = {1.0, 2.0};
    firebreak::opt::WeightedDpvScoringOptions options;
    assert(throws_with_message([&]() {
        (void)firebreak::opt::build_weighted_dpv_score_report(
            opt,
            opt.eligible_indices,
            options);
    }, "size"));

    opt.compact_cell_weights = {1.0, 2.0, std::numeric_limits<double>::infinity()};
    assert(throws_with_message([&]() {
        (void)firebreak::opt::build_weighted_dpv_score_report(
            opt,
            opt.eligible_indices,
            options);
    }, "finite"));
}

}  // namespace

int main() {
    test_structural_cache_is_weight_independent_but_numeric_cache_is_not();
    test_cache_mismatch_fails_clearly();
    test_probability_mismatch_fails_clearly();
    test_missing_and_nonfinite_weights_fail_before_scoring();
    std::cout << "All weighted FPP DPV cache tests passed.\n";
    return 0;
}
