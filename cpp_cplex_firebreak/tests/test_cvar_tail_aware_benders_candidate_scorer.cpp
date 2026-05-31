#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/CvarTailAwareBendersCandidateScorer.hpp"

namespace {

template <typename Fn>
void assert_throws(Fn fn, const std::string& label) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        std::cerr << "Expected exception was not thrown: " << label << "\n";
    }
    assert(threw);
}

firebreak::benders::BendersCut cut(
    int scenario_id,
    std::vector<std::pair<int, double>> coefficients) {
    firebreak::benders::BendersCut out;
    out.scenario_id = scenario_id;
    out.coefficients_by_compact_index = std::move(coefficients);
    return out;
}

std::vector<firebreak::benders::BendersCut> fixture_cuts() {
    return {
        cut(1, {{0, -2.0}, {1, -1.0}, {2, 0.5}}),
        cut(2, {{0, -1.0}, {2, -3.0}, {3, -1.0}}),
    };
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

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-9);
}

void test_tail_uses_empirical_high_loss_cuts_only() {
    firebreak::benders::CvarTailAwareBendersCandidateScorer scorer;
    const auto summary = scorer.scoreCandidates(
        4,
        {0, 1, 2, 3},
        {0, 1, 2, 3},
        fixture_cuts(),
        {{1, 10.0}, {2, 5.0}},
        0.5,
        0.5);

    assert((summary.empirical_tail_scenario_ids == std::vector<int>{1}));
    assert(summary.cuts_used == 2);
    assert(summary.tail_cuts_used == 1);
    assert_close(score_for(summary, 0).generic_score, 3.0);
    assert_close(score_for(summary, 0).tail_score, 2.0);
    assert_close(score_for(summary, 2).tail_score, -0.5);
}

void test_blend_formula_and_ranks() {
    firebreak::benders::CvarTailAwareBendersCandidateScorer scorer;
    const auto summary = scorer.scoreCandidates(
        4,
        {0, 1, 2, 3},
        {0, 1, 2, 3},
        fixture_cuts(),
        {{1, 10.0}, {2, 5.0}},
        0.5,
        0.5);

    assert_close(score_for(summary, 0).normalized_generic_score, 1.0);
    assert_close(score_for(summary, 0).normalized_tail_score, 1.0);
    assert_close(score_for(summary, 0).blend_score, 1.0);
    assert_close(score_for(summary, 2).normalized_generic_score, 0.75);
    assert_close(score_for(summary, 2).normalized_tail_score, 0.0);
    assert_close(score_for(summary, 2).blend_score, 0.375);
    assert(score_for(summary, 0).blend_rank == 1);
    assert(score_for(summary, 2).blend_rank == 2);
    assert(score_for(summary, 1).blend_rank == 3);
    assert(score_for(summary, 3).blend_rank == 4);

    const auto top = firebreak::benders::topCvarTailAwareCandidates(
        summary.blend_scores,
        2);
    assert((top == std::vector<std::pair<int, double>>{{0, 1.0}, {2, 0.375}}));
}

void test_gamma_zero_and_one() {
    firebreak::benders::CvarTailAwareBendersCandidateScorer scorer;
    const auto generic_only = scorer.scoreCandidates(
        4,
        {0, 1, 2, 3},
        {0, 1, 2, 3},
        fixture_cuts(),
        {{1, 10.0}, {2, 5.0}},
        0.5,
        0.0);
    const auto tail_only = scorer.scoreCandidates(
        4,
        {0, 1, 2, 3},
        {0, 1, 2, 3},
        fixture_cuts(),
        {{1, 10.0}, {2, 5.0}},
        0.5,
        1.0);

    assert_close(
        score_for(generic_only, 2).blend_score,
        score_for(generic_only, 2).normalized_generic_score);
    assert_close(
        score_for(tail_only, 1).blend_score,
        score_for(tail_only, 1).normalized_tail_score);
}

void test_missing_scores_are_zero_and_ties_use_candidate_id() {
    firebreak::benders::CvarTailAwareBendersCandidateScorer scorer;
    const auto summary = scorer.scoreCandidates(
        4,
        {0, 1, 2, 3},
        {0, 1, 2, 3},
        {},
        {{1, 10.0}, {2, 5.0}},
        0.5,
        0.5);

    for (int candidate = 0; candidate < 4; ++candidate) {
        assert_close(score_for(summary, candidate).generic_score, 0.0);
        assert_close(score_for(summary, candidate).tail_score, 0.0);
        assert_close(score_for(summary, candidate).blend_score, 0.0);
    }
    const auto top = firebreak::benders::topCvarTailAwareCandidates(
        summary.blend_scores,
        4);
    assert((top == std::vector<std::pair<int, double>>{
        {0, 0.0}, {1, 0.0}, {2, 0.0}, {3, 0.0}}));
}

void test_validation() {
    firebreak::benders::CvarTailAwareBendersCandidateScorer scorer;
    assert_throws(
        [&] {
            (void)scorer.scoreCandidates(
                4,
                {0, 1, 2, 3},
                {0},
                fixture_cuts(),
                {{1, 1.0}},
                0.5,
                -0.1);
        },
        "invalid gamma");
    assert_throws(
        [&] {
            (void)scorer.scoreCandidates(
                4,
                {0, 1, 2, 3},
                {0},
                fixture_cuts(),
                {{1, 1.0}},
                1.0,
                0.5);
        },
        "invalid beta");
    assert_throws(
        [&] {
            (void)scorer.scoreCandidates(
                4,
                {0, 1, 2, 3},
                {0, 0},
                fixture_cuts(),
                {{1, 1.0}},
                0.5,
                0.5);
        },
        "duplicate candidate");
}

}  // namespace

int main() {
    test_tail_uses_empirical_high_loss_cuts_only();
    test_blend_formula_and_ranks();
    test_gamma_zero_and_one();
    test_missing_scores_are_zero_and_ties_use_candidate_id();
    test_validation();

    std::cout << "All CVaR tail-aware Benders candidate scorer tests passed.\n";
    return 0;
}
