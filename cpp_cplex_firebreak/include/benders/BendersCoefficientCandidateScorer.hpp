#pragma once

#include <map>
#include <utility>
#include <vector>

#include "benders/BendersCut.hpp"

namespace firebreak::benders {

struct BendersCoefficientCandidateScore {
    int candidate = -1;
    int compact_index = -1;
    double score = 0.0;
};

struct BendersCoefficientScoringSummary {
    std::vector<BendersCoefficientCandidateScore> detailed_scores;
    std::vector<std::pair<int, double>> scores;
    int cuts_used = 0;
    int nonzero_inactive_coefficients = 0;
    double max_score = 0.0;
    double average_score = 0.0;
};

class BendersCoefficientCandidateScorer {
public:
    BendersCoefficientScoringSummary scoreCandidates(
        int candidate_count,
        const std::vector<int>& eligible_compact_indices,
        const std::vector<int>& candidates,
        const std::vector<BendersCut>& cuts,
        const std::map<int, double>& scenario_probability_by_id = {}) const;

    BendersCoefficientScoringSummary scoreInactiveCandidates(
        int candidate_count,
        const std::vector<int>& eligible_compact_indices,
        const std::vector<int>& inactive_candidates,
        const std::vector<BendersCut>& cuts,
        const std::map<int, double>& scenario_probability_by_id = {}) const;
};

std::vector<std::pair<int, double>> topBendersCoefficientCandidates(
    const std::vector<std::pair<int, double>>& scores,
    int limit);

}  // namespace firebreak::benders
