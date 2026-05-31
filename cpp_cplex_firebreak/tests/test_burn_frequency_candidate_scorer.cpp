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

firebreak::opt::OptimizationInstance make_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "burn_frequency_path";
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({10, 11, 12, 13});
    opt.eligible_indices = {0, 1, 2, 3};
    opt.eligible_original_nodes = {10, 11, 12, 13};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 10;
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 10, 11});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 11, 12});

    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

firebreak::opt::OptimizationInstance make_weighted_two_scenario_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "burn_frequency_weighted";
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
    first.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 2;
    second.probability = 0.75;
    second.ignition_index = 0;
    second.ignition_original_node = 1;
    second.observed_node_indices = {0, 2};
    second.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.25, 0.75};
    opt.total_arcs = first.arcs.size() + second.arcs.size();
    return opt;
}

firebreak::opt::OptimizationInstance make_uniform_fallback_instance() {
    auto opt = make_weighted_two_scenario_instance();
    for (auto& scenario : opt.scenarios) {
        scenario.probability = 0.0;
    }
    opt.scenario_probabilities.clear();
    return opt;
}

void test_simple_path_graph_reachability() {
    const auto opt = make_path_instance();
    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    const auto scores = scorer.scoreCandidates(opt);

    assert(scores.size() == 4);
    assert_close(scores[0].second, 1.0);
    assert_close(scores[1].second, 1.0);
    assert_close(scores[2].second, 1.0);
    assert_close(scores[3].second, 0.0);
}

void test_multiple_scenarios_weighted_scores() {
    const auto opt = make_weighted_two_scenario_instance();
    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    const auto scores = scorer.scoreCandidates(opt);

    assert(scores.size() == 3);
    assert(scores[0].first == 0);
    assert(scores[1].first == 1);
    assert(scores[2].first == 2);
    assert_close(scores[0].second, 0.25);
    assert_close(scores[1].second, 0.75);
    assert_close(scores[2].second, 0.0);
}

void test_uniform_fallback_when_probabilities_unavailable() {
    const auto opt = make_uniform_fallback_instance();
    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    const auto scores = scorer.scoreCandidates(opt);

    assert_close(scores[0].second, 1.0);
    assert_close(scores[1].second, 1.0);
    assert_close(scores[2].second, 0.0);
}

void test_unburned_candidate_score_zero() {
    const auto opt = make_path_instance();
    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    const auto detailed = scorer.scoreDetailed(opt);

    assert(detailed[3].candidate == 3);
    assert(detailed[3].compact_index == 3);
    assert(detailed[3].original_node == 13);
    assert(detailed[3].scenarios_burned == 0);
    assert_close(detailed[3].score, 0.0);
}

void test_deterministic_tie_breaking() {
    const auto opt = make_path_instance();
    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    const auto top = firebreak::benders::topBurnFrequencyCandidates(scorer.scoreCandidates(opt), 3);

    assert((top == std::vector<std::pair<int, double>>{
        {0, 1.0},
        {1, 1.0},
        {2, 1.0},
    }));
}

void test_top_score_initial_set() {
    const auto opt = make_path_instance();
    const auto active = firebreak::benders::makeInitialActiveSetFromBurnFrequency(opt, 2);
    assert((active == std::vector<int>{0, 1}));

    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    const auto active_from_scores = firebreak::benders::makeInitialActiveSetFromScores(
        static_cast<int>(opt.eligible_indices.size()),
        opt.budget,
        scorer.scoreCandidates(opt),
        2);
    assert(active_from_scores == active);
}

void test_activation_top_k() {
    const auto opt = make_path_instance();
    firebreak::benders::BurnFrequencyCandidateScorer scorer;
    firebreak::benders::RestrictedCandidateManager manager(
        static_cast<int>(opt.eligible_indices.size()),
        opt.budget,
        {0, 1});

    const auto activated = manager.activateTopK(scorer.scoreCandidates(opt), 1);
    assert((activated == std::vector<int>{2}));
    assert((manager.activeCandidates() == std::vector<int>{0, 1, 2}));
}

}  // namespace

int main() {
    test_simple_path_graph_reachability();
    test_multiple_scenarios_weighted_scores();
    test_uniform_fallback_when_probabilities_unavailable();
    test_unburned_candidate_score_zero();
    test_deterministic_tie_breaking();
    test_top_score_initial_set();
    test_activation_top_k();

    std::cout << "All burn-frequency candidate scorer tests passed.\n";
    return 0;
}
