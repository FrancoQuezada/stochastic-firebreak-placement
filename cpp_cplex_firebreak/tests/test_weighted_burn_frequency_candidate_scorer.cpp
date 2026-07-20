#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/BurnFrequencyCandidateScorer.hpp"
#include "benders/RestrictedCandidateManager.hpp"
#include "opt/OptimizationInstance.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-12);
}

firebreak::opt::OptimizationInstance make_weighted_exposure_instance(
    const std::vector<double>& compact_weights) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_burn_frequency";
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {1, 2, 3};
    opt.eligible_original_nodes = {2, 3, 4};

    firebreak::opt::OptimizationScenario first;
    first.scenario_id = 1;
    first.probability = 0.25;
    first.ignition_index = 0;
    first.ignition_original_node = 1;
    first.observed_node_indices = {0, 1};
    first.arcs = {firebreak::opt::CompactArc{0, 1, 1, 2}};

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 2;
    second.probability = 0.75;
    second.ignition_index = 0;
    second.ignition_original_node = 1;
    second.observed_node_indices = {0, 2};
    second.arcs = {firebreak::opt::CompactArc{0, 2, 1, 3}};

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.25, 0.75};
    opt.total_arcs = 2;
    opt.compact_cell_weights = compact_weights;
    opt.cell_weight_map.deterministic_hash = "weights-a";
    return opt;
}

void test_weighted_direct_exposure_formula() {
    const auto opt = make_weighted_exposure_instance({1.0, 10.0, 1.0, 100.0});
    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    const auto detailed = scorer.scoreDetailed(opt);

    assert(detailed.size() == 3);
    assert(detailed[0].candidate == 0);
    assert_close(detailed[0].burn_frequency, 0.25);
    assert_close(detailed[0].cell_weight, 10.0);
    assert_close(detailed[0].score, 2.5);
    assert(detailed[0].weighted);
    assert(detailed[0].weight_map_hash == "weights-a");
    assert_close(detailed[1].burn_frequency, 0.75);
    assert_close(detailed[1].cell_weight, 1.0);
    assert_close(detailed[1].score, 0.75);

    const auto top = firebreak::benders::topBurnFrequencyCandidates(
        scorer.scoreCandidates(opt),
        2);
    assert((top == std::vector<std::pair<int, double>>{{0, 2.5}, {1, 0.75}}));
}

void test_homogeneous_regression_and_activation_order() {
    auto implicit = make_weighted_exposure_instance({});
    implicit.cell_weight_map.deterministic_hash.clear();
    const auto explicit_unit = make_weighted_exposure_instance({1.0, 1.0, 1.0, 1.0});

    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    const auto implicit_scores = scorer.scoreCandidates(implicit);
    const auto explicit_scores = scorer.scoreCandidates(explicit_unit);
    assert(implicit_scores == explicit_scores);

    firebreak::benders::RestrictedCandidateManager manager(3, 1, {2});
    const auto activated = manager.activateTopK(explicit_scores, 1);
    assert((activated == std::vector<int>{1}));
}

}  // namespace

int main() {
    test_weighted_direct_exposure_formula();
    test_homogeneous_regression_and_activation_order();
    std::cout << "All weighted burn-frequency candidate scorer tests passed.\n";
    return 0;
}
