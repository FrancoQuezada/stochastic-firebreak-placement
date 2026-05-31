#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

#include "benders/BendersCoefficientCandidateScorer.hpp"
#include "benders/RestrictedCandidateManager.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-12);
}

firebreak::benders::BendersCut make_cut(
    int scenario_id,
    std::vector<std::pair<int, double>> coefficients) {
    firebreak::benders::BendersCut cut;
    cut.scenario_id = scenario_id;
    cut.rhs_constant = 0.0;
    cut.coefficients_by_compact_index = std::move(coefficients);
    return cut;
}

void test_synthetic_cut_scoring() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreInactiveCandidates(
        3,
        {10, 20, 30},
        {0, 1, 2},
        {
            make_cut(1, {{10, -2.0}, {20, 1.0}, {30, 0.5}}),
            make_cut(2, {{10, -1.0}, {30, -4.0}}),
        });

    assert(summary.cuts_used == 2);
    assert(summary.nonzero_inactive_coefficients == 5);
    assert(summary.scores.size() == 3);
    assert(summary.scores[0].first == 2);
    assert_close(summary.scores[0].second, 3.5);
    assert(summary.scores[1].first == 0);
    assert_close(summary.scores[1].second, 3.0);
    assert(summary.scores[2].first == 1);
    assert_close(summary.scores[2].second, -1.0);
}

void test_missing_coefficient_treated_as_zero() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreInactiveCandidates(
        3,
        {0, 1, 2},
        {0, 1, 2},
        {make_cut(1, {{0, -2.0}})});

    assert_close(summary.scores[0].second, 2.0);
    assert_close(summary.scores[1].second, 0.0);
    assert_close(summary.scores[2].second, 0.0);
}

void test_negative_coefficient_gives_positive_score() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreInactiveCandidates(
        1,
        {5},
        {0},
        {make_cut(7, {{5, -3.25}})});

    assert_close(summary.scores[0].second, 3.25);
    assert_close(summary.max_score, 3.25);
}

void test_positive_coefficient_gives_negative_score() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreInactiveCandidates(
        1,
        {5},
        {0},
        {make_cut(7, {{5, 1.5}})});

    assert_close(summary.scores[0].second, -1.5);
    assert_close(summary.max_score, -1.5);
}

void test_weighted_scores() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreInactiveCandidates(
        2,
        {0, 1},
        {0, 1},
        {
            make_cut(10, {{0, -4.0}, {1, -2.0}}),
            make_cut(11, {{0, -4.0}, {1, 2.0}}),
        },
        std::map<int, double>{{10, 0.25}, {11, 0.75}});

    assert(summary.scores[0].first == 0);
    assert_close(summary.scores[0].second, 4.0);
    assert(summary.scores[1].first == 1);
    assert_close(summary.scores[1].second, -1.0);
}

void test_deterministic_tie_breaking() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreInactiveCandidates(
        4,
        {0, 1, 2, 3},
        {0, 1, 2, 3},
        {make_cut(1, {{0, -1.0}, {1, -1.0}, {2, -0.5}, {3, -1.0}})});
    const auto top = firebreak::benders::topBendersCoefficientCandidates(summary.scores, 3);

    assert((top == std::vector<std::pair<int, double>>{
        {0, 1.0},
        {1, 1.0},
        {3, 1.0},
    }));
}

void test_top_k_activation_from_benders_scores() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    firebreak::benders::RestrictedCandidateManager manager(4, 1, {0});
    const auto summary = scorer.scoreInactiveCandidates(
        manager.candidateCount(),
        {0, 1, 2, 3},
        manager.inactiveCandidates(),
        {make_cut(1, {{1, -1.0}, {2, -3.0}, {3, -2.0}})});

    const auto activated = manager.activateTopK(summary.scores, 2);
    assert((activated == std::vector<int>{2, 3}));
    assert((manager.activeCandidates() == std::vector<int>{0, 2, 3}));
}

void test_all_zero_score_fallback_tie_breaking() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    firebreak::benders::RestrictedCandidateManager manager(4, 1, {0});
    const auto summary = scorer.scoreInactiveCandidates(
        manager.candidateCount(),
        {0, 1, 2, 3},
        manager.inactiveCandidates(),
        {});

    assert(summary.cuts_used == 0);
    assert(summary.nonzero_inactive_coefficients == 0);
    assert((summary.scores == std::vector<std::pair<int, double>>{
        {1, 0.0},
        {2, 0.0},
        {3, 0.0},
    }));
    const auto activated = manager.activateTopK(summary.scores, 2);
    assert((activated == std::vector<int>{1, 2}));
}

void test_score_arbitrary_candidate_set_for_deactivation() {
    firebreak::benders::BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreCandidates(
        5,
        {0, 1, 2, 3, 4},
        {0, 2, 4},
        {
            make_cut(1, {{0, 2.0}, {2, -3.0}}),
            make_cut(2, {{2, -1.0}}),
        });

    assert(summary.scores.size() == 3);
    assert(summary.scores[0].first == 2);
    assert_close(summary.scores[0].second, 4.0);
    assert(summary.scores[1].first == 4);
    assert_close(summary.scores[1].second, 0.0);
    assert(summary.scores[2].first == 0);
    assert_close(summary.scores[2].second, -2.0);
}

}  // namespace

int main() {
    test_synthetic_cut_scoring();
    test_missing_coefficient_treated_as_zero();
    test_negative_coefficient_gives_positive_score();
    test_positive_coefficient_gives_negative_score();
    test_weighted_scores();
    test_deterministic_tie_breaking();
    test_top_k_activation_from_benders_scores();
    test_all_zero_score_fallback_tie_breaking();
    test_score_arbitrary_candidate_set_for_deactivation();

    std::cout << "All Benders-coefficient candidate scorer tests passed.\n";
    return 0;
}
