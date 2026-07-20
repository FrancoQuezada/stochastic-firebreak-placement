#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "opt/DpvIndexBuilder.hpp"
#include "opt/WeightedDpvScoring.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-9);
}

firebreak::opt::OptimizationInstance make_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    const std::vector<int>& eligible_original_nodes,
    const std::vector<double>& weights) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_weighted_dpv";
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.node_mapper.build_from_nodes(original_nodes);
    opt.compact_cell_weights = weights;
    for (const int original_node : eligible_original_nodes) {
        opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
        opt.eligible_original_nodes.push_back(original_node);
    }
    opt.budget = static_cast<int>(opt.eligible_indices.size());

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 7;
    scenario.probability = 1.0;
    scenario.ignition_index = opt.node_mapper.to_index(original_nodes.front());
    scenario.ignition_original_node = original_nodes.front();
    scenario.arcs = arcs;
    for (int index = 0; index < opt.node_mapper.size(); ++index) {
        scenario.observed_node_indices.push_back(index);
    }

    firebreak::opt::DpvIndexBuilder builder;
    scenario.dpv = builder.build_for_scenario(scenario, opt.node_mapper);
    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.num_pairs();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

double weighted_score_for_original(
    const firebreak::opt::WeightedDpvScoreReport& report,
    int original_node) {
    for (const auto& score : report.candidate_scores) {
        if (score.original_node == original_node) {
            return score.weighted_score;
        }
    }
    assert(false && "missing candidate score");
    return -1.0;
}

std::vector<int> valued_set_for_original(
    const firebreak::opt::WeightedDpvStructuralData& data,
    int original_node) {
    assert(!data.scenarios.empty());
    for (const auto& candidate : data.scenarios.front().candidates) {
        if (candidate.original_node == original_node) {
            return candidate.valued_node_indices;
        }
    }
    assert(false && "missing candidate structure");
    return {};
}

void test_single_chain_weights_destination_cells() {
    const auto opt = make_instance(
        {1, 2, 3, 4},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{2, 3, 3, 4},
        },
        {2},
        {500.0, 1.0, 100.0, 10.0});

    firebreak::opt::WeightedDpvScoringOptions options;
    const auto report = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        options);

    assert_close(weighted_score_for_original(report, 2), 111.0);
    assert(report.candidate_scores.front().unweighted_score == 3.0);
    assert(weighted_score_for_original(report, 2) != 3.0);
}

void test_branching_reconverging_and_cycle_count_once() {
    const auto opt = make_instance(
        {1, 2, 3, 4, 5},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{2, 4, 3, 5},
            firebreak::opt::CompactArc{3, 4, 4, 5},
            firebreak::opt::CompactArc{4, 2, 5, 3},
        },
        {2},
        {1.0, 2.0, 3.0, 5.0, 7.0});

    firebreak::opt::WeightedDpvScoringOptions options;
    const auto report = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        options);
    const auto valued = valued_set_for_original(report.structural_data, 2);

    assert((valued == std::vector<int>{1, 2, 3, 4}));
    assert_close(weighted_score_for_original(report, 2), 17.0);
}

void test_ignition_candidate_gets_no_false_downstream_credit() {
    const auto opt = make_instance(
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
        },
        {1, 2},
        {1000.0, 1.0, 100.0});

    firebreak::opt::WeightedDpvScoringOptions options;
    const auto report = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        options);

    assert_close(weighted_score_for_original(report, 1), 0.0);
    assert_close(weighted_score_for_original(report, 2), 101.0);
}

void test_non_eligible_descendant_weight_is_valued() {
    const auto opt = make_instance(
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
        },
        {2},
        {1.0, 2.0, 99.0});

    firebreak::opt::WeightedDpvScoringOptions options;
    const auto report = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        options);

    assert_close(weighted_score_for_original(report, 2), 101.0);
}

}  // namespace

int main() {
    test_single_chain_weights_destination_cells();
    test_branching_reconverging_and_cycle_count_once();
    test_ignition_candidate_gets_no_false_downstream_credit();
    test_non_eligible_descendant_weight_is_valued();
    std::cout << "All weighted FPP DPV score tests passed.\n";
    return 0;
}
