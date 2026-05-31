#include "benders/CvarTailScoreDiagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "benders/BendersCoefficientCandidateScorer.hpp"

namespace firebreak::benders {

namespace {

constexpr double kScoreTieTolerance = 1.0e-12;

void validate_candidate_vector(
    const std::vector<int>& candidates,
    int candidate_count,
    const char* context) {
    std::set<int> seen;
    for (const int candidate : candidates) {
        if (candidate < 0 || candidate >= candidate_count) {
            throw std::invalid_argument(
                std::string(context) + " contains candidate id outside the valid range.");
        }
        if (!seen.insert(candidate).second) {
            throw std::invalid_argument(
                std::string(context) + " contains duplicate candidate ids.");
        }
    }
}

std::vector<int> all_candidate_ids(int candidate_count) {
    std::vector<int> candidates(static_cast<std::size_t>(candidate_count));
    std::iota(candidates.begin(), candidates.end(), 0);
    return candidates;
}

bool contains_candidate(const std::vector<int>& candidates, int candidate) {
    return std::find(candidates.begin(), candidates.end(), candidate) != candidates.end();
}

std::unordered_set<int> set_from_vector(const std::vector<int>& values) {
    return std::unordered_set<int>(values.begin(), values.end());
}

std::vector<BendersCut> filter_cuts_by_scenario_ids(
    const std::vector<BendersCut>& cuts,
    const std::vector<int>& scenario_ids) {
    const auto scenario_set = set_from_vector(scenario_ids);
    std::vector<BendersCut> filtered;
    filtered.reserve(cuts.size());
    for (const auto& cut : cuts) {
        if (scenario_set.count(cut.scenario_id) > 0) {
            filtered.push_back(cut);
        }
    }
    return filtered;
}

std::vector<int> empirical_tail_scenario_ids(
    const std::vector<std::pair<int, double>>& scenario_losses_by_id,
    double cvar_beta) {
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

std::vector<int> excess_tail_scenario_ids(
    const std::vector<std::pair<int, double>>& cvar_excess_by_id,
    double tolerance) {
    std::vector<int> scenario_ids;
    for (const auto& [scenario_id, excess] : cvar_excess_by_id) {
        if (std::isfinite(excess) && excess > tolerance) {
            scenario_ids.push_back(scenario_id);
        }
    }
    std::sort(scenario_ids.begin(), scenario_ids.end());
    scenario_ids.erase(
        std::unique(scenario_ids.begin(), scenario_ids.end()),
        scenario_ids.end());
    return scenario_ids;
}

std::map<int, double> score_map_from(
    int candidate_count,
    const std::vector<int>& eligible_compact_indices,
    const std::vector<BendersCut>& cuts,
    const std::map<int, double>& scenario_probability_by_id) {
    BendersCoefficientCandidateScorer scorer;
    const auto summary = scorer.scoreCandidates(
        candidate_count,
        eligible_compact_indices,
        all_candidate_ids(candidate_count),
        cuts,
        scenario_probability_by_id);
    std::map<int, double> scores;
    for (const auto& [candidate, score] : summary.scores) {
        scores[candidate] = score;
    }
    return scores;
}

std::vector<TailScoreCandidateRank> ranked_descending(
    const std::map<int, double>& scores) {
    std::vector<TailScoreCandidateRank> ranked;
    ranked.reserve(scores.size());
    for (const auto& [candidate, score] : scores) {
        ranked.push_back({candidate, score, 0});
    }
    std::sort(
        ranked.begin(),
        ranked.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            return lhs.candidate_id < rhs.candidate_id;
        });
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        ranked[i].rank = static_cast<int>(i + 1);
    }
    return ranked;
}

std::map<int, int> rank_map_from(const std::vector<TailScoreCandidateRank>& ranked) {
    std::map<int, int> ranks;
    for (const auto& item : ranked) {
        ranks[item.candidate_id] = item.rank;
    }
    return ranks;
}

double score_for(const std::map<int, double>& scores, int candidate) {
    const auto it = scores.find(candidate);
    return it == scores.end() ? 0.0 : it->second;
}

int rank_for(const std::map<int, int>& ranks, int candidate) {
    const auto it = ranks.find(candidate);
    return it == ranks.end() ? 0 : it->second;
}

std::vector<TailScoreCandidateRank> take_top(
    const std::vector<TailScoreCandidateRank>& ranked,
    int limit) {
    if (limit < 0) {
        throw std::invalid_argument("Tail-score diagnostics top_k must be nonnegative.");
    }
    std::vector<TailScoreCandidateRank> out = ranked;
    if (static_cast<int>(out.size()) > limit) {
        out.resize(static_cast<std::size_t>(limit));
    }
    return out;
}

std::vector<TailScoreCandidateRank> take_bottom_active(
    const std::map<int, double>& scores,
    const std::map<int, int>& ranks,
    const std::vector<int>& active_candidates,
    int limit) {
    if (limit < 0) {
        throw std::invalid_argument("Tail-score diagnostics top_k must be nonnegative.");
    }
    std::vector<TailScoreCandidateRank> out;
    out.reserve(active_candidates.size());
    for (const int candidate : active_candidates) {
        out.push_back({
            candidate,
            score_for(scores, candidate),
            rank_for(ranks, candidate),
        });
    }
    std::sort(
        out.begin(),
        out.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score < rhs.score;
            }
            return lhs.candidate_id < rhs.candidate_id;
        });
    if (static_cast<int>(out.size()) > limit) {
        out.resize(static_cast<std::size_t>(limit));
    }
    return out;
}

int overlap_with_candidates(
    const std::vector<int>& candidates,
    const std::vector<TailScoreCandidateRank>& ranked) {
    const auto candidate_set = set_from_vector(candidates);
    int overlap = 0;
    for (const auto& item : ranked) {
        if (candidate_set.count(item.candidate_id) > 0) {
            ++overlap;
        }
    }
    return overlap;
}

double pearson_correlation(
    const std::vector<double>& x,
    const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double mean_x =
        std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size());
    const double mean_y =
        std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(y.size());
    double numerator = 0.0;
    double sum_x2 = 0.0;
    double sum_y2 = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double dx = x[i] - mean_x;
        const double dy = y[i] - mean_y;
        numerator += dx * dy;
        sum_x2 += dx * dx;
        sum_y2 += dy * dy;
    }
    if (sum_x2 <= 0.0 || sum_y2 <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return numerator / std::sqrt(sum_x2 * sum_y2);
}

std::map<int, double> average_rank_by_score(
    const std::map<int, double>& scores) {
    std::vector<std::pair<int, double>> sorted(scores.begin(), scores.end());
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });

    std::map<int, double> ranks;
    std::size_t i = 0;
    while (i < sorted.size()) {
        std::size_t j = i + 1;
        while (j < sorted.size() &&
               std::fabs(sorted[j].second - sorted[i].second) <= kScoreTieTolerance) {
            ++j;
        }
        const double average_rank =
            (static_cast<double>(i + 1) + static_cast<double>(j)) / 2.0;
        for (std::size_t k = i; k < j; ++k) {
            ranks[sorted[k].first] = average_rank;
        }
        i = j;
    }
    return ranks;
}

double spearman_correlation(
    const std::map<int, double>& x_scores,
    const std::map<int, double>& y_scores,
    int candidate_count) {
    const auto x_ranks = average_rank_by_score(x_scores);
    const auto y_ranks = average_rank_by_score(y_scores);
    std::vector<double> x;
    std::vector<double> y;
    x.reserve(static_cast<std::size_t>(candidate_count));
    y.reserve(static_cast<std::size_t>(candidate_count));
    for (int candidate = 0; candidate < candidate_count; ++candidate) {
        const auto x_it = x_ranks.find(candidate);
        const auto y_it = y_ranks.find(candidate);
        if (x_it != x_ranks.end() && y_it != y_ranks.end()) {
            x.push_back(x_it->second);
            y.push_back(y_it->second);
        }
    }
    return pearson_correlation(x, y);
}

std::vector<double> score_vector(
    const std::map<int, double>& scores,
    int candidate_count) {
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(candidate_count));
    for (int candidate = 0; candidate < candidate_count; ++candidate) {
        values.push_back(score_for(scores, candidate));
    }
    return values;
}

std::string event_type_for(
    bool activated,
    bool deactivated,
    bool selected,
    bool protected_selected) {
    std::vector<std::string> pieces;
    if (activated) {
        pieces.push_back("activated");
    }
    if (deactivated) {
        pieces.push_back("deactivated");
    }
    if (protected_selected) {
        pieces.push_back("protected-selected");
    } else if (selected) {
        pieces.push_back("selected");
    }
    if (pieces.empty()) {
        return "observed";
    }
    std::string out = pieces.front();
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        out += "+";
        out += pieces[i];
    }
    return out;
}

std::string event_warning_for(
    bool activated,
    bool deactivated,
    bool protected_selected,
    int tail_rank,
    int top_k) {
    std::vector<std::string> warnings;
    if (deactivated && tail_rank > 0 && tail_rank <= top_k) {
        warnings.push_back("deactivated_tail_top_k");
    }
    if (protected_selected && tail_rank > 0 && tail_rank <= top_k) {
        warnings.push_back("protected_selected_tail_top_k");
    }
    if (activated && (tail_rank <= 0 || tail_rank > top_k)) {
        warnings.push_back("activated_poor_tail_rank");
    }
    if (warnings.empty()) {
        return "";
    }
    std::string out = warnings.front();
    for (std::size_t i = 1; i < warnings.size(); ++i) {
        out += ";";
        out += warnings[i];
    }
    return out;
}

std::vector<TailScoreCandidateEventDiagnostic> candidate_events(
    const CvarTailScoreDiagnosticsInput& input,
    const std::map<int, double>& generic_scores,
    const std::map<int, double>& empirical_scores,
    const std::map<int, double>& excess_scores,
    const std::map<int, double>& recent_scores,
    const std::map<int, int>& generic_ranks,
    const std::map<int, int>& empirical_ranks,
    const std::map<int, int>& excess_ranks,
    int top_k) {
    std::set<int> event_candidates;
    event_candidates.insert(input.activated_candidates.begin(), input.activated_candidates.end());
    event_candidates.insert(input.deactivated_candidates.begin(), input.deactivated_candidates.end());
    event_candidates.insert(input.selected_candidates.begin(), input.selected_candidates.end());
    event_candidates.insert(
        input.protected_selected_candidates.begin(),
        input.protected_selected_candidates.end());

    std::vector<TailScoreCandidateEventDiagnostic> out;
    out.reserve(event_candidates.size());
    for (const int candidate : event_candidates) {
        const bool activated = contains_candidate(input.activated_candidates, candidate);
        const bool deactivated = contains_candidate(input.deactivated_candidates, candidate);
        const bool selected = contains_candidate(input.selected_candidates, candidate);
        const bool protected_selected =
            contains_candidate(input.protected_selected_candidates, candidate);
        const int tail_rank = rank_for(empirical_ranks, candidate);
        TailScoreCandidateEventDiagnostic event;
        event.candidate_id = candidate;
        event.event_type = event_type_for(
            activated,
            deactivated,
            selected,
            protected_selected);
        event.generic_score = score_for(generic_scores, candidate);
        event.tail_empirical_score = score_for(empirical_scores, candidate);
        event.tail_excess_score = score_for(excess_scores, candidate);
        event.recent_tail_score = score_for(recent_scores, candidate);
        event.was_active_before =
            contains_candidate(input.active_candidates_before_round, candidate);
        event.was_active_after =
            contains_candidate(input.active_candidates_after_round, candidate);
        event.was_selected = selected;
        event.was_protected = protected_selected;
        event.was_deactivated = deactivated;
        event.was_activated = activated;
        event.rank_generic = rank_for(generic_ranks, candidate);
        event.rank_tail_empirical = tail_rank;
        event.rank_tail_excess = rank_for(excess_ranks, candidate);
        event.warning = event_warning_for(
            activated,
            deactivated,
            protected_selected,
            tail_rank,
            top_k);
        out.push_back(event);
    }
    return out;
}

}  // namespace

int tailScoreTopKOverlap(
    const std::vector<TailScoreCandidateRank>& lhs,
    const std::vector<TailScoreCandidateRank>& rhs) {
    std::unordered_set<int> lhs_candidates;
    lhs_candidates.reserve(lhs.size());
    for (const auto& item : lhs) {
        lhs_candidates.insert(item.candidate_id);
    }
    int overlap = 0;
    for (const auto& item : rhs) {
        if (lhs_candidates.count(item.candidate_id) > 0) {
            ++overlap;
        }
    }
    return overlap;
}

CvarTailScoreRoundDiagnostics computeCvarTailScoreRoundDiagnostics(
    const CvarTailScoreDiagnosticsInput& input) {
    if (input.candidate_count <= 0) {
        throw std::invalid_argument("CVaR tail-score diagnostics require candidate_count > 0.");
    }
    if (static_cast<int>(input.eligible_compact_indices.size()) != input.candidate_count) {
        throw std::invalid_argument(
            "CVaR tail-score diagnostics require one eligible compact index per candidate.");
    }
    const int top_k = std::max(1, std::min(input.top_k, input.candidate_count));
    validate_candidate_vector(input.active_candidates_before_round, input.candidate_count, "active_candidates_before_round");
    validate_candidate_vector(input.active_candidates_after_round, input.candidate_count, "active_candidates_after_round");
    validate_candidate_vector(input.activated_candidates, input.candidate_count, "activated_candidates");
    validate_candidate_vector(input.deactivated_candidates, input.candidate_count, "deactivated_candidates");
    validate_candidate_vector(input.selected_candidates, input.candidate_count, "selected_candidates");
    validate_candidate_vector(input.protected_selected_candidates, input.candidate_count, "protected_selected_candidates");

    CvarTailScoreRoundDiagnostics diagnostics;
    diagnostics.round_index = input.round_index;
    diagnostics.risk_measure = input.risk_measure;
    diagnostics.cvar_beta = input.cvar_beta;
    diagnostics.risk_threshold = input.risk_threshold;
    diagnostics.candidate_count = input.candidate_count;
    diagnostics.active_count_before_round =
        static_cast<int>(input.active_candidates_before_round.size());
    diagnostics.active_count_after_round =
        static_cast<int>(input.active_candidates_after_round.size());
    diagnostics.activated_candidates = input.activated_candidates;
    diagnostics.deactivated_candidates = input.deactivated_candidates;
    diagnostics.selected_candidates = input.selected_candidates;
    diagnostics.protected_selected_candidates = input.protected_selected_candidates;

    const auto empirical_tail_ids =
        empirical_tail_scenario_ids(input.scenario_losses_by_id, input.cvar_beta);
    const auto excess_tail_ids =
        excess_tail_scenario_ids(input.cvar_excess_by_id, input.tolerance);
    diagnostics.tail_definition_used = "empirical-high-loss";
    if (!excess_tail_ids.empty()) {
        diagnostics.tail_definition_used += "+positive-cvar-excess";
    }
    diagnostics.tail_scenario_ids = empirical_tail_ids;
    diagnostics.tail_scenario_count = static_cast<int>(empirical_tail_ids.size());
    if (empirical_tail_ids.empty()) {
        diagnostics.notes.push_back(
            "No finite scenario losses were available; empirical tail diagnostics use zero tail scores.");
    }
    if (input.cvar_excess_by_id.empty()) {
        diagnostics.notes.push_back(
            "CVaR excess values were unavailable; excess-tail scores use an empty tail scenario set.");
    } else if (excess_tail_ids.empty()) {
        diagnostics.notes.push_back(
            "No scenario had positive CVaR excess above tolerance.");
    }

    const auto empirical_tail_cuts =
        filter_cuts_by_scenario_ids(input.accumulated_cuts, empirical_tail_ids);
    const auto excess_tail_cuts =
        filter_cuts_by_scenario_ids(input.accumulated_cuts, excess_tail_ids);
    const auto recent_tail_cuts =
        filter_cuts_by_scenario_ids(input.recent_cuts, empirical_tail_ids);
    if (empirical_tail_ids.size() > 0 && empirical_tail_cuts.empty()) {
        diagnostics.notes.push_back(
            "No accumulated Benders cuts were generated by empirical tail scenarios.");
    }
    if (empirical_tail_ids.size() > 0 && recent_tail_cuts.empty()) {
        diagnostics.notes.push_back(
            "No recent-stage Benders cuts were generated by empirical tail scenarios.");
    }

    const auto generic_scores = score_map_from(
        input.candidate_count,
        input.eligible_compact_indices,
        input.accumulated_cuts,
        input.scenario_probability_by_id);
    const auto empirical_scores = score_map_from(
        input.candidate_count,
        input.eligible_compact_indices,
        empirical_tail_cuts,
        input.scenario_probability_by_id);
    const auto excess_scores = score_map_from(
        input.candidate_count,
        input.eligible_compact_indices,
        excess_tail_cuts,
        input.scenario_probability_by_id);
    const auto recent_scores = score_map_from(
        input.candidate_count,
        input.eligible_compact_indices,
        recent_tail_cuts,
        input.scenario_probability_by_id);

    const auto generic_ranked = ranked_descending(generic_scores);
    const auto empirical_ranked = ranked_descending(empirical_scores);
    const auto excess_ranked = ranked_descending(excess_scores);
    const auto recent_ranked = ranked_descending(recent_scores);
    const auto generic_ranks = rank_map_from(generic_ranked);
    const auto empirical_ranks = rank_map_from(empirical_ranked);
    const auto excess_ranks = rank_map_from(excess_ranked);

    diagnostics.top_generic_candidates = take_top(generic_ranked, top_k);
    diagnostics.top_tail_empirical_candidates = take_top(empirical_ranked, top_k);
    diagnostics.top_tail_excess_candidates = take_top(excess_ranked, top_k);
    diagnostics.top_recent_tail_candidates = take_top(recent_ranked, top_k);
    diagnostics.bottom_generic_active_candidates = take_bottom_active(
        generic_scores,
        generic_ranks,
        input.active_candidates_before_round,
        top_k);
    diagnostics.bottom_tail_empirical_active_candidates = take_bottom_active(
        empirical_scores,
        empirical_ranks,
        input.active_candidates_before_round,
        top_k);
    diagnostics.bottom_tail_excess_active_candidates = take_bottom_active(
        excess_scores,
        excess_ranks,
        input.active_candidates_before_round,
        top_k);

    diagnostics.top_k_overlap_generic_tail = tailScoreTopKOverlap(
        diagnostics.top_generic_candidates,
        diagnostics.top_tail_empirical_candidates);
    diagnostics.activated_tail_top_k_overlap = overlap_with_candidates(
        input.activated_candidates,
        diagnostics.top_tail_empirical_candidates);
    diagnostics.deactivated_tail_bottom_k_overlap = overlap_with_candidates(
        input.deactivated_candidates,
        diagnostics.bottom_tail_empirical_active_candidates);
    diagnostics.deactivated_tail_top_k_warning_count = overlap_with_candidates(
        input.deactivated_candidates,
        diagnostics.top_tail_empirical_candidates);
    diagnostics.selected_tail_top_k_overlap = overlap_with_candidates(
        input.selected_candidates,
        diagnostics.top_tail_empirical_candidates);

    diagnostics.pearson_generic_tail = pearson_correlation(
        score_vector(generic_scores, input.candidate_count),
        score_vector(empirical_scores, input.candidate_count));
    diagnostics.spearman_generic_tail = spearman_correlation(
        generic_scores,
        empirical_scores,
        input.candidate_count);
    diagnostics.candidate_events = candidate_events(
        input,
        generic_scores,
        empirical_scores,
        excess_scores,
        recent_scores,
        generic_ranks,
        empirical_ranks,
        excess_ranks,
        top_k);

    if (diagnostics.top_k_overlap_generic_tail == 0 && top_k > 0) {
        diagnostics.notes.push_back(
            "Generic and empirical-tail top-K candidate sets have zero overlap.");
    }
    if (diagnostics.deactivated_tail_top_k_warning_count > 0) {
        diagnostics.notes.push_back(
            "At least one deactivated candidate is in the empirical-tail top-K set.");
    }
    return diagnostics;
}

}  // namespace firebreak::benders
