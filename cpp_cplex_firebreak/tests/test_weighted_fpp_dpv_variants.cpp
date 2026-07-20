#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benchmarks/StaticDpvBenchmark.hpp"
#include "benchmarks/StaticDpvMipBenchmark.hpp"
#include "opt/DpvIndexBuilder.hpp"
#include "opt/WeightedDpvScoring.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-9);
}

firebreak::opt::OptimizationInstance make_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    const std::vector<double>& probabilities,
    const std::vector<double>& weights) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_weighted_dpv_variants";
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.node_mapper.build_from_nodes(original_nodes);
    opt.eligible_original_nodes = original_nodes;
    for (const int original_node : original_nodes) {
        opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
    }
    opt.budget = 1;
    opt.compact_cell_weights = weights;

    firebreak::opt::DpvIndexBuilder builder;
    for (std::size_t s = 0; s < probabilities.size(); ++s) {
        firebreak::opt::OptimizationScenario scenario;
        scenario.scenario_id = static_cast<int>(s + 1);
        scenario.probability = probabilities[s];
        scenario.ignition_index = 0;
        scenario.ignition_original_node = original_nodes.front();
        scenario.arcs = arcs;
        for (int index = 0; index < opt.node_mapper.size(); ++index) {
            scenario.observed_node_indices.push_back(index);
        }
        scenario.dpv = builder.build_for_scenario(scenario, opt.node_mapper);
        opt.total_arcs += scenario.arcs.size();
        opt.total_dpv_pairs += scenario.dpv.num_pairs();
        opt.scenarios.push_back(scenario);
        opt.scenario_probabilities.push_back(probabilities[s]);
    }
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
    assert(false && "missing score");
    return -1.0;
}

double static_score_for_original(
    const firebreak::benchmarks::StaticDpvBenchmarkResult& result,
    int original_node) {
    for (const auto& score : result.all_scores) {
        if (score.original_node == original_node) {
            return score.score;
        }
    }
    assert(false && "missing legacy Static-DPV score");
    return -1.0;
}

double mip_score_for_original(
    const firebreak::benchmarks::StaticDpvMipBenchmarkResult& result,
    int original_node) {
    for (const auto& score : result.all_scores) {
        if (score.original_node == original_node) {
            return score.dpv_score;
        }
    }
    assert(false && "missing Static-DPV-MIP score");
    return -1.0;
}

void test_homogeneous_scores_match_legacy_variants() {
    const auto opt = make_instance(
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{0, 2, 1, 3},
        },
        {1.0},
        {1.0, 1.0, 1.0});

    firebreak::opt::WeightedDpvScoringOptions legacy_options;
    legacy_options.ignition_policy = firebreak::opt::WeightedDpvIgnitionPolicy::LegacyIncludeReachable;
    legacy_options.variant = firebreak::opt::WeightedDpvVariant::StaticClosedDescendantsTimesOutDegree;
    const auto weighted_static = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        legacy_options);

    firebreak::benchmarks::StaticDpvBenchmark legacy_static;
    const auto legacy_result = legacy_static.run(opt, opt.budget);
    assert_close(weighted_score_for_original(weighted_static, 1), static_score_for_original(legacy_result, 1));

    legacy_options.variant = firebreak::opt::WeightedDpvVariant::StaticClosedDescendants;
    const auto weighted_mip = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        legacy_options);

    firebreak::benchmarks::StaticDpvMipBenchmark mip_static;
    const auto mip_result = mip_static.run(opt, opt.budget);
    assert_close(weighted_score_for_original(weighted_mip, 1), mip_score_for_original(mip_result, 1));
}

void test_outdegree_variant_is_not_collapsed_into_mip_variant() {
    const auto opt = make_instance(
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{0, 2, 1, 3},
        },
        {1.0},
        {1.0, 2.0, 3.0});

    firebreak::opt::WeightedDpvScoringOptions options;
    options.ignition_policy = firebreak::opt::WeightedDpvIgnitionPolicy::LegacyIncludeReachable;
    options.variant = firebreak::opt::WeightedDpvVariant::StaticClosedDescendants;
    const auto mip_like = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        options);

    options.variant = firebreak::opt::WeightedDpvVariant::StaticClosedDescendantsTimesOutDegree;
    const auto legacy_like = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        opt.eligible_indices,
        options);

    assert_close(weighted_score_for_original(mip_like, 1), 6.0);
    assert_close(weighted_score_for_original(legacy_like, 1), 12.0);
}

void test_probabilities_are_applied_once() {
    const auto opt = make_instance(
        {1, 2, 3},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
        },
        {0.25, 0.75},
        {1.0, 2.0, 8.0});

    firebreak::opt::WeightedDpvScoringOptions options;
    const auto report = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        {1},
        options);
    assert_close(weighted_score_for_original(report, 2), 10.0);
}

void test_weighted_ranking_can_reverse_unit_ranking() {
    const auto opt = make_instance(
        {1, 2, 3, 4, 5, 6},
        {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{0, 4, 1, 5},
            firebreak::opt::CompactArc{4, 5, 5, 6},
        },
        {1.0},
        {1.0, 1.0, 1.0, 1.0, 2.0, 100.0});

    firebreak::opt::WeightedDpvScoringOptions options;
    const auto report = firebreak::opt::build_weighted_dpv_score_report(
        opt,
        {1, 4},
        options);

    assert_close(weighted_score_for_original(report, 2), 3.0);
    assert_close(weighted_score_for_original(report, 5), 102.0);
    assert(report.candidate_scores.front().original_node == 5);
}

}  // namespace

int main() {
    test_homogeneous_scores_match_legacy_variants();
    test_outdegree_variant_is_not_collapsed_into_mip_variant();
    test_probabilities_are_applied_once();
    test_weighted_ranking_can_reverse_unit_ranking();
    std::cout << "All weighted FPP DPV variant tests passed.\n";
    return 0;
}
