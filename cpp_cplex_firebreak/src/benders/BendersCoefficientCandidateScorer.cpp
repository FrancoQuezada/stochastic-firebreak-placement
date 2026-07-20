#include "benders/BendersCoefficientCandidateScorer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace firebreak::benders {

namespace {

void validate_candidate_count(int candidate_count) {
    if (candidate_count <= 0) {
        throw std::invalid_argument("Benders-coefficient scoring requires candidate_count > 0.");
    }
}

void validate_candidate_id(int candidate_count, int candidate, const char* context) {
    if (candidate < 0 || candidate >= candidate_count) {
        throw std::invalid_argument(
            std::string(context) + " candidate id " + std::to_string(candidate) +
            " is outside [0, " + std::to_string(candidate_count - 1) + "].");
    }
}

double scenario_weight_for(
    int scenario_id,
    const std::map<int, double>& scenario_probability_by_id) {
    const auto it = scenario_probability_by_id.find(scenario_id);
    if (it == scenario_probability_by_id.end()) {
        return 1.0;
    }
    if (!std::isfinite(it->second) || it->second < 0.0) {
        throw std::invalid_argument("Benders-coefficient scoring received an invalid scenario probability.");
    }
    return it->second;
}

std::vector<std::pair<int, double>> sorted_scores(
    const std::vector<std::pair<int, double>>& scores) {
    std::vector<std::pair<int, double>> sorted = scores;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });
    return sorted;
}

}  // namespace

BendersCoefficientScoringSummary BendersCoefficientCandidateScorer::scoreCandidates(
    int candidate_count,
    const std::vector<int>& eligible_compact_indices,
    const std::vector<int>& candidates,
    const std::vector<BendersCut>& cuts,
    const std::map<int, double>& scenario_probability_by_id,
    const std::string& weight_map_hash) const {
    validate_candidate_count(candidate_count);
    if (static_cast<int>(eligible_compact_indices.size()) != candidate_count) {
        throw std::invalid_argument(
            "Benders-coefficient scoring requires one eligible compact index per candidate.");
    }

    std::unordered_map<int, int> candidate_by_compact_index;
    candidate_by_compact_index.reserve(eligible_compact_indices.size());
    for (int candidate = 0; candidate < candidate_count; ++candidate) {
        const int compact_index = eligible_compact_indices[static_cast<std::size_t>(candidate)];
        if (compact_index < 0) {
            throw std::invalid_argument(
                "Benders-coefficient scoring requires nonnegative compact indices.");
        }
        if (!candidate_by_compact_index.insert({compact_index, candidate}).second) {
            throw std::invalid_argument(
                "Benders-coefficient scoring received duplicate eligible compact indices.");
        }
    }

    BendersCoefficientScoringSummary summary;
    summary.cuts_used = static_cast<int>(cuts.size());
    summary.weighted = !weight_map_hash.empty();
    summary.weight_map_hash = weight_map_hash;
    summary.detailed_scores.reserve(candidates.size());
    summary.scores.reserve(candidates.size());

    std::unordered_map<int, std::size_t> score_position_by_candidate;
    score_position_by_candidate.reserve(candidates.size());
    for (const int candidate : candidates) {
        validate_candidate_id(candidate_count, candidate, "Benders-coefficient candidate set");
        if (score_position_by_candidate.count(candidate) > 0) {
            throw std::invalid_argument(
                "Benders-coefficient candidate set contains duplicate candidate id " +
                std::to_string(candidate) + ".");
        }
        BendersCoefficientCandidateScore score;
        score.candidate = candidate;
        score.compact_index = eligible_compact_indices[static_cast<std::size_t>(candidate)];
        score_position_by_candidate[candidate] = summary.detailed_scores.size();
        summary.detailed_scores.push_back(score);
    }

    for (const auto& cut : cuts) {
        const double weight = scenario_weight_for(cut.scenario_id, scenario_probability_by_id);
        for (const auto& [compact_index, coefficient] : cut.coefficients_by_compact_index) {
            if (!std::isfinite(coefficient)) {
                throw std::invalid_argument(
                    "Benders-coefficient scoring received a non-finite cut coefficient.");
            }
            const auto candidate_it = candidate_by_compact_index.find(compact_index);
            if (candidate_it == candidate_by_compact_index.end()) {
                continue;
            }
            const auto score_it =
                score_position_by_candidate.find(candidate_it->second);
            if (score_it == score_position_by_candidate.end()) {
                continue;
            }
            if (std::fabs(coefficient) > 1.0e-12 && weight > 0.0) {
                ++summary.nonzero_inactive_coefficients;
            }
            summary.detailed_scores[score_it->second].score -= weight * coefficient;
        }
    }

    double total_score = 0.0;
    summary.max_score = summary.detailed_scores.empty()
        ? 0.0
        : -std::numeric_limits<double>::infinity();
    for (const auto& detailed : summary.detailed_scores) {
        if (!std::isfinite(detailed.score)) {
            throw std::runtime_error("Benders-coefficient scoring produced a non-finite score.");
        }
        summary.scores.push_back({detailed.candidate, detailed.score});
        summary.max_score = std::max(summary.max_score, detailed.score);
        total_score += detailed.score;
    }
    summary.average_score = summary.detailed_scores.empty()
        ? 0.0
        : total_score / static_cast<double>(summary.detailed_scores.size());

    summary.scores = sorted_scores(summary.scores);
    std::sort(
        summary.detailed_scores.begin(),
        summary.detailed_scores.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            return lhs.candidate < rhs.candidate;
        });

    return summary;
}

BendersCoefficientScoringSummary BendersCoefficientCandidateScorer::scoreInactiveCandidates(
    int candidate_count,
    const std::vector<int>& eligible_compact_indices,
    const std::vector<int>& inactive_candidates,
    const std::vector<BendersCut>& cuts,
    const std::map<int, double>& scenario_probability_by_id,
    const std::string& weight_map_hash) const {
    return scoreCandidates(
        candidate_count,
        eligible_compact_indices,
        inactive_candidates,
        cuts,
        scenario_probability_by_id,
        weight_map_hash);
}

std::vector<std::pair<int, double>> topBendersCoefficientCandidates(
    const std::vector<std::pair<int, double>>& scores,
    int limit) {
    if (limit < 0) {
        throw std::invalid_argument("topBendersCoefficientCandidates requires limit >= 0.");
    }
    std::vector<std::pair<int, double>> sorted = sorted_scores(scores);
    if (static_cast<int>(sorted.size()) > limit) {
        sorted.resize(static_cast<std::size_t>(limit));
    }
    return sorted;
}

}  // namespace firebreak::benders
