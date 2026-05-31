#include "benders/CvarTailAwareBendersCandidateScorer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "benders/BendersCoefficientCandidateScorer.hpp"

namespace firebreak::benders {

namespace {

void validate_candidate_count(int candidate_count) {
    if (candidate_count <= 0) {
        throw std::invalid_argument("CVaR tail-aware Benders scoring requires candidate_count > 0.");
    }
}

void validate_candidate_id(int candidate_count, int candidate, const char* context) {
    if (candidate < 0 || candidate >= candidate_count) {
        throw std::invalid_argument(
            std::string(context) + " candidate id " + std::to_string(candidate) +
            " is outside [0, " + std::to_string(candidate_count - 1) + "].");
    }
}

void validate_candidates(
    int candidate_count,
    const std::vector<int>& candidates) {
    std::set<int> seen;
    for (const int candidate : candidates) {
        validate_candidate_id(candidate_count, candidate, "CVaR tail-aware scoring");
        if (!seen.insert(candidate).second) {
            throw std::invalid_argument(
                "CVaR tail-aware scoring candidate set contains duplicate candidate id " +
                std::to_string(candidate) + ".");
        }
    }
}

std::vector<int> empirical_tail_scenario_ids(
    const std::vector<std::pair<int, double>>& scenario_losses_by_id,
    double cvar_beta) {
    if (!std::isfinite(cvar_beta) || cvar_beta <= 0.0 || cvar_beta >= 1.0) {
        throw std::invalid_argument(
            "CVaR tail-aware Benders scoring requires 0.0 < cvar_beta < 1.0.");
    }

    std::vector<std::pair<int, double>> losses;
    losses.reserve(scenario_losses_by_id.size());
    for (const auto& [scenario_id, loss] : scenario_losses_by_id) {
        if (std::isfinite(loss)) {
            losses.push_back({scenario_id, loss});
        }
    }
    if (losses.empty()) {
        return {};
    }

    std::sort(
        losses.begin(),
        losses.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });

    const double tail_fraction = std::max(0.0, 1.0 - cvar_beta);
    int tail_count = static_cast<int>(
        std::ceil(tail_fraction * static_cast<double>(losses.size())));
    tail_count = std::max(1, std::min(tail_count, static_cast<int>(losses.size())));

    std::vector<int> scenario_ids;
    scenario_ids.reserve(static_cast<std::size_t>(tail_count));
    for (int i = 0; i < tail_count; ++i) {
        scenario_ids.push_back(losses[static_cast<std::size_t>(i)].first);
    }
    std::sort(scenario_ids.begin(), scenario_ids.end());
    return scenario_ids;
}

std::vector<BendersCut> filter_cuts_by_scenario_ids(
    const std::vector<BendersCut>& cuts,
    const std::vector<int>& scenario_ids) {
    const std::unordered_set<int> scenario_set(scenario_ids.begin(), scenario_ids.end());
    std::vector<BendersCut> filtered;
    filtered.reserve(cuts.size());
    for (const auto& cut : cuts) {
        if (scenario_set.count(cut.scenario_id) > 0) {
            filtered.push_back(cut);
        }
    }
    return filtered;
}

std::map<int, double> score_map_from(
    const std::vector<std::pair<int, double>>& scores) {
    std::map<int, double> out;
    for (const auto& [candidate, score] : scores) {
        out[candidate] = score;
    }
    return out;
}

double score_for(const std::map<int, double>& scores, int candidate) {
    const auto it = scores.find(candidate);
    return it == scores.end() ? 0.0 : it->second;
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

std::map<int, int> rank_map_from_scores(
    const std::vector<std::pair<int, double>>& scores) {
    const auto sorted = sorted_scores(scores);
    std::map<int, int> ranks;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        ranks[sorted[i].first] = static_cast<int>(i + 1);
    }
    return ranks;
}

std::map<int, double> normalized_scores(
    const std::vector<int>& candidates,
    const std::map<int, double>& scores) {
    std::map<int, double> normalized;
    if (candidates.empty()) {
        return normalized;
    }

    double min_score = std::numeric_limits<double>::infinity();
    double max_score = -std::numeric_limits<double>::infinity();
    for (const int candidate : candidates) {
        const double score = score_for(scores, candidate);
        if (!std::isfinite(score)) {
            throw std::runtime_error("CVaR tail-aware Benders scoring produced a non-finite score.");
        }
        min_score = std::min(min_score, score);
        max_score = std::max(max_score, score);
    }

    const double range = max_score - min_score;
    for (const int candidate : candidates) {
        const double score = score_for(scores, candidate);
        normalized[candidate] = range <= 1.0e-12 ? 0.0 : (score - min_score) / range;
    }
    return normalized;
}

}  // namespace

CvarTailAwareBendersScoringSummary CvarTailAwareBendersCandidateScorer::scoreCandidates(
    int candidate_count,
    const std::vector<int>& eligible_compact_indices,
    const std::vector<int>& candidates,
    const std::vector<BendersCut>& accumulated_cuts,
    const std::vector<std::pair<int, double>>& scenario_losses_by_id,
    double cvar_beta,
    double gamma,
    const std::map<int, double>& scenario_probability_by_id) const {
    validate_candidate_count(candidate_count);
    if (static_cast<int>(eligible_compact_indices.size()) != candidate_count) {
        throw std::invalid_argument(
            "CVaR tail-aware Benders scoring requires one eligible compact index per candidate.");
    }
    if (!std::isfinite(gamma) || gamma < 0.0 || gamma > 1.0) {
        throw std::invalid_argument(
            "CVaR tail-aware Benders scoring requires 0.0 <= gamma <= 1.0.");
    }
    validate_candidates(candidate_count, candidates);

    CvarTailAwareBendersScoringSummary summary;
    summary.gamma = gamma;
    summary.empirical_tail_scenario_ids =
        empirical_tail_scenario_ids(scenario_losses_by_id, cvar_beta);
    const auto tail_cuts =
        filter_cuts_by_scenario_ids(accumulated_cuts, summary.empirical_tail_scenario_ids);

    BendersCoefficientCandidateScorer scorer;
    const auto generic_summary = scorer.scoreCandidates(
        candidate_count,
        eligible_compact_indices,
        candidates,
        accumulated_cuts,
        scenario_probability_by_id);
    const auto tail_summary = scorer.scoreCandidates(
        candidate_count,
        eligible_compact_indices,
        candidates,
        tail_cuts,
        scenario_probability_by_id);

    summary.cuts_used = generic_summary.cuts_used;
    summary.tail_cuts_used = tail_summary.cuts_used;
    summary.nonzero_generic_coefficients =
        generic_summary.nonzero_inactive_coefficients;
    summary.nonzero_tail_coefficients =
        tail_summary.nonzero_inactive_coefficients;
    summary.generic_scores = generic_summary.scores;
    summary.tail_scores = tail_summary.scores;

    const auto generic_scores = score_map_from(generic_summary.scores);
    const auto tail_scores = score_map_from(tail_summary.scores);
    const auto normalized_generic = normalized_scores(candidates, generic_scores);
    const auto normalized_tail = normalized_scores(candidates, tail_scores);

    std::vector<std::pair<int, double>> blend_scores;
    blend_scores.reserve(candidates.size());
    for (const int candidate : candidates) {
        const double generic_component = score_for(normalized_generic, candidate);
        const double tail_component = score_for(normalized_tail, candidate);
        const double blend_score =
            (1.0 - gamma) * generic_component + gamma * tail_component;
        blend_scores.push_back({candidate, blend_score});
    }
    summary.blend_scores = sorted_scores(blend_scores);

    const auto generic_ranks = rank_map_from_scores(summary.generic_scores);
    const auto tail_ranks = rank_map_from_scores(summary.tail_scores);
    const auto blend_ranks = rank_map_from_scores(summary.blend_scores);

    summary.detailed_scores.reserve(candidates.size());
    std::vector<int> sorted_candidates = candidates;
    std::sort(sorted_candidates.begin(), sorted_candidates.end());
    for (const int candidate : sorted_candidates) {
        CvarTailAwareBendersCandidateScore detailed;
        detailed.candidate = candidate;
        detailed.generic_score = score_for(generic_scores, candidate);
        detailed.tail_score = score_for(tail_scores, candidate);
        detailed.normalized_generic_score =
            score_for(normalized_generic, candidate);
        detailed.normalized_tail_score = score_for(normalized_tail, candidate);
        detailed.blend_score =
            (1.0 - gamma) * detailed.normalized_generic_score +
            gamma * detailed.normalized_tail_score;
        detailed.generic_rank = generic_ranks.at(candidate);
        detailed.tail_rank = tail_ranks.at(candidate);
        detailed.blend_rank = blend_ranks.at(candidate);
        summary.detailed_scores.push_back(detailed);
    }

    summary.generic_scores = sorted_scores(summary.generic_scores);
    summary.tail_scores = sorted_scores(summary.tail_scores);
    return summary;
}

std::vector<std::pair<int, double>> topCvarTailAwareCandidates(
    const std::vector<std::pair<int, double>>& scores,
    int limit) {
    if (limit < 0) {
        throw std::invalid_argument("topCvarTailAwareCandidates requires limit >= 0.");
    }
    auto sorted = sorted_scores(scores);
    if (static_cast<int>(sorted.size()) > limit) {
        sorted.resize(static_cast<std::size_t>(limit));
    }
    return sorted;
}

}  // namespace firebreak::benders
