#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "benders/BendersCut.hpp"

namespace firebreak::benders {

struct CvarTailAwareBendersCandidateScore {
    int candidate = -1;
    double generic_score = 0.0;
    double tail_score = 0.0;
    double normalized_generic_score = 0.0;
    double normalized_tail_score = 0.0;
    double blend_score = 0.0;
    int generic_rank = 0;
    int tail_rank = 0;
    int blend_rank = 0;
};

struct CvarTailAwareBendersScoringSummary {
    std::vector<CvarTailAwareBendersCandidateScore> detailed_scores;
    std::vector<std::pair<int, double>> generic_scores;
    std::vector<std::pair<int, double>> tail_scores;
    std::vector<std::pair<int, double>> blend_scores;
    std::vector<int> empirical_tail_scenario_ids;
    int cuts_used = 0;
    int tail_cuts_used = 0;
    int nonzero_generic_coefficients = 0;
    int nonzero_tail_coefficients = 0;
    double gamma = 0.5;
    bool weighted = false;
    std::string weight_map_hash;
};

class CvarTailAwareBendersCandidateScorer {
public:
    CvarTailAwareBendersScoringSummary scoreCandidates(
        int candidate_count,
        const std::vector<int>& eligible_compact_indices,
        const std::vector<int>& candidates,
        const std::vector<BendersCut>& accumulated_cuts,
        const std::vector<std::pair<int, double>>& scenario_losses_by_id,
        double cvar_beta,
        double gamma,
        const std::map<int, double>& scenario_probability_by_id = {},
        const std::string& weight_map_hash = {}) const;
};

std::vector<std::pair<int, double>> topCvarTailAwareCandidates(
    const std::vector<std::pair<int, double>>& scores,
    int limit);

}  // namespace firebreak::benders
