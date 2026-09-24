#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

#include "benders/CvarTailAwareBendersCandidateScorer.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-9);
}

firebreak::benders::BendersCut cut(
    int scenario_id,
    std::vector<std::pair<int, double>> coefficients) {
    firebreak::benders::BendersCut out;
    out.scenario_id = scenario_id;
    out.coefficients_by_compact_index = std::move(coefficients);
    return out;
}

const firebreak::benders::CvarTailAwareBendersCandidateScore& score_for(
    const firebreak::benders::CvarTailAwareBendersScoringSummary& summary,
    int candidate) {
    for (const auto& score : summary.detailed_scores) {
        if (score.candidate == candidate) {
            return score;
        }
    }
    assert(false && "candidate score not found");
    return summary.detailed_scores.front();
}

void test_weighted_tail_membership_uses_weighted_losses() {
    firebreak::benders::CvarTailAwareBendersCandidateScorer scorer;
    const auto summary = scorer.scoreCandidates(
        3,
        {0, 1, 2},
        {0, 1, 2},
        {
            cut(1, {{0, -6.0}, {1, -1.0}}),
            cut(2, {{0, -1.0}, {1, -8.0}, {2, -2.0}}),
        },
        {
            {1, 3.0},
            {2, 20.0},
        },
        0.5,
        1.0,
        {},
        "weighted-tail-hash");

    assert(summary.weighted);
    assert(summary.weight_map_hash == "weighted-tail-hash");
    assert((summary.empirical_tail_scenario_ids == std::vector<int>{2}));
    assert(summary.tail_cuts_used == 1);
    assert_close(score_for(summary, 1).tail_score, 8.0);
    assert_close(score_for(summary, 0).tail_score, 1.0);
    assert(summary.blend_scores[0].first == 1);
}

void test_homogeneous_tail_regression() {
    firebreak::benders::CvarTailAwareBendersCandidateScorer scorer;
    const std::vector<firebreak::benders::BendersCut> cuts = {
        cut(1, {{0, -2.0}, {1, -1.0}}),
        cut(2, {{0, -1.0}, {1, -3.0}}),
    };
    const auto implicit = scorer.scoreCandidates(
        2,
        {0, 1},
        {0, 1},
        cuts,
        {{1, 2.0}, {2, 3.0}},
        0.5,
        0.5);
    const auto explicit_unit = scorer.scoreCandidates(
        2,
        {0, 1},
        {0, 1},
        cuts,
        {{1, 2.0}, {2, 3.0}},
        0.5,
        0.5,
        {},
        "unit-hash");

    assert(implicit.blend_scores.size() == explicit_unit.blend_scores.size());
    for (std::size_t i = 0; i < implicit.blend_scores.size(); ++i) {
        assert(implicit.blend_scores[i].first == explicit_unit.blend_scores[i].first);
        assert_close(implicit.blend_scores[i].second, explicit_unit.blend_scores[i].second);
    }
}

}  // namespace

int main() {
    test_weighted_tail_membership_uses_weighted_losses();
    test_homogeneous_tail_regression();
    std::cout << "All weighted CVaR tail-aware candidate scorer tests passed.\n";
    return 0;
}
