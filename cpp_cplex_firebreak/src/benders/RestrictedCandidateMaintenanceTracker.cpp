#include "benders/RestrictedCandidateMaintenanceTracker.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace firebreak::benders {

namespace {

std::vector<int> sorted_unique_validated(
    int candidate_count,
    const std::vector<int>& candidates,
    const char* context) {
    std::vector<int> sorted = candidates;
    for (const int candidate : sorted) {
        if (candidate < 0 || candidate >= candidate_count) {
            throw std::invalid_argument(
                std::string(context) + " candidate id " + std::to_string(candidate) +
                " is outside [0, " + std::to_string(candidate_count - 1) + "].");
        }
    }
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return sorted;
}

std::vector<std::pair<int, double>> scores_for_candidates_with_zero_default(
    int candidate_count,
    const std::vector<int>& candidates,
    const std::vector<std::pair<int, double>>& scores) {
    std::vector<double> score_by_candidate(static_cast<std::size_t>(candidate_count), 0.0);
    std::vector<int> seen(static_cast<std::size_t>(candidate_count), 0);
    for (const auto& [candidate, score] : scores) {
        if (candidate < 0 || candidate >= candidate_count) {
            throw std::invalid_argument(
                "Maintenance score candidate id " + std::to_string(candidate) +
                " is outside [0, " + std::to_string(candidate_count - 1) + "].");
        }
        if (seen[static_cast<std::size_t>(candidate)] != 0) {
            throw std::invalid_argument(
                "Maintenance scores contain duplicate candidate id " +
                std::to_string(candidate) + ".");
        }
        seen[static_cast<std::size_t>(candidate)] = 1;
        score_by_candidate[static_cast<std::size_t>(candidate)] = score;
    }

    std::vector<std::pair<int, double>> out;
    out.reserve(candidates.size());
    for (const int candidate : candidates) {
        if (candidate < 0 || candidate >= candidate_count) {
            throw std::invalid_argument(
                "Maintenance candidate id " + std::to_string(candidate) +
                " is outside [0, " + std::to_string(candidate_count - 1) + "].");
        }
        out.push_back({candidate, score_by_candidate[static_cast<std::size_t>(candidate)]});
    }
    return out;
}

}  // namespace

RestrictedCandidateMaintenanceTracker::RestrictedCandidateMaintenanceTracker(
    int candidate_count,
    const std::vector<int>& initial_active_candidates)
    : candidate_count_(candidate_count),
      active_age_(static_cast<std::size_t>(std::max(candidate_count, 0)), 0),
      last_activated_round_(static_cast<std::size_t>(std::max(candidate_count, 0)), -1),
      last_deactivated_round_(static_cast<std::size_t>(std::max(candidate_count, 0)), -1),
      state_change_count_(static_cast<std::size_t>(std::max(candidate_count, 0)), 0),
      activation_count_(static_cast<std::size_t>(std::max(candidate_count, 0)), 0),
      deactivation_count_(static_cast<std::size_t>(std::max(candidate_count, 0)), 0) {
    if (candidate_count_ <= 0) {
        throw std::invalid_argument("RestrictedCandidateMaintenanceTracker requires candidate_count > 0.");
    }
    const auto initial_active =
        sorted_unique_validated(candidate_count_, initial_active_candidates, "Initial maintenance active set");
    for (const int candidate : initial_active) {
        active_age_[static_cast<std::size_t>(candidate)] = 0;
        last_activated_round_[static_cast<std::size_t>(candidate)] = -1;
    }
}

int RestrictedCandidateMaintenanceTracker::currentRound() const {
    return current_round_;
}

int RestrictedCandidateMaintenanceTracker::activeAge(int candidate) const {
    validateCandidate(candidate, "activeAge");
    return active_age_[static_cast<std::size_t>(candidate)];
}

int RestrictedCandidateMaintenanceTracker::lastActivatedRound(int candidate) const {
    validateCandidate(candidate, "lastActivatedRound");
    return last_activated_round_[static_cast<std::size_t>(candidate)];
}

int RestrictedCandidateMaintenanceTracker::lastDeactivatedRound(int candidate) const {
    validateCandidate(candidate, "lastDeactivatedRound");
    return last_deactivated_round_[static_cast<std::size_t>(candidate)];
}

int RestrictedCandidateMaintenanceTracker::stateChangeCount(int candidate) const {
    validateCandidate(candidate, "stateChangeCount");
    return state_change_count_[static_cast<std::size_t>(candidate)];
}

int RestrictedCandidateMaintenanceTracker::activationCount(int candidate) const {
    validateCandidate(candidate, "activationCount");
    return activation_count_[static_cast<std::size_t>(candidate)];
}

int RestrictedCandidateMaintenanceTracker::deactivationCount(int candidate) const {
    validateCandidate(candidate, "deactivationCount");
    return deactivation_count_[static_cast<std::size_t>(candidate)];
}

int RestrictedCandidateMaintenanceTracker::maxStateChangeCount() const {
    return state_change_count_.empty()
        ? 0
        : *std::max_element(state_change_count_.begin(), state_change_count_.end());
}

double RestrictedCandidateMaintenanceTracker::averageStateChangeCount() const {
    if (state_change_count_.empty()) {
        return 0.0;
    }
    const int total =
        std::accumulate(state_change_count_.begin(), state_change_count_.end(), 0);
    return static_cast<double>(total) / static_cast<double>(state_change_count_.size());
}

int RestrictedCandidateMaintenanceTracker::totalReactivationBlockedByCooldown() const {
    return total_reactivation_blocked_by_cooldown_;
}

int RestrictedCandidateMaintenanceTracker::totalOscillationEvents() const {
    return total_oscillation_events_;
}

std::vector<std::pair<int, double>> RestrictedCandidateMaintenanceTracker::filterActivationScores(
    const std::vector<std::pair<int, double>>& inactive_scores,
    int reactivation_cooldown_rounds,
    RestrictedCandidateMaintenanceDecision& decision) {
    std::vector<std::pair<int, double>> filtered;
    filtered.reserve(inactive_scores.size());
    for (const auto& [candidate, score] : inactive_scores) {
        validateCandidate(candidate, "maintenance activation score");
        if (reactivationBlockedByCooldown(candidate, reactivation_cooldown_rounds)) {
            ++decision.reactivation_blocked_by_cooldown_count;
            ++decision.protected_cooldown_count;
            ++total_reactivation_blocked_by_cooldown_;
            continue;
        }
        filtered.push_back({candidate, score});
    }
    return filtered;
}

std::vector<int> RestrictedCandidateMaintenanceTracker::selectDeactivationCandidates(
    const RestrictedCandidateManager& manager,
    const std::vector<std::pair<int, double>>& active_scores,
    const std::vector<int>& selected_candidates,
    const std::vector<int>& newly_activated,
    const RestrictedCandidateMaintenanceOptions& options,
    int requested_deactivation_count,
    RestrictedCandidateMaintenanceDecision& decision) const {
    return selectDeactivationCandidates(
        manager,
        active_scores,
        selected_candidates,
        newly_activated,
        {},
        options,
        requested_deactivation_count,
        decision);
}

std::vector<int> RestrictedCandidateMaintenanceTracker::selectDeactivationCandidates(
    const RestrictedCandidateManager& manager,
    const std::vector<std::pair<int, double>>& active_scores,
    const std::vector<int>& selected_candidates,
    const std::vector<int>& newly_activated,
    const std::vector<int>& tail_protected_candidates,
    const RestrictedCandidateMaintenanceOptions& options,
    int requested_deactivation_count,
    RestrictedCandidateMaintenanceDecision& decision) const {
    if (requested_deactivation_count <= 0) {
        return {};
    }
    if (options.min_active_size < manager.budget()) {
        throw std::invalid_argument("Maintenance min_active_size must be at least the firebreak budget.");
    }

    const int floor = std::max(manager.budget(), options.min_active_size);
    const int max_deactivations_allowed = std::max(0, manager.activeCount() - floor);
    const int target_count = std::min(requested_deactivation_count, max_deactivations_allowed);
    if (target_count <= 0) {
        return {};
    }

    const auto selected = sorted_unique_validated(
        candidate_count_,
        selected_candidates,
        "Maintenance selected protected set");
    const auto activated_now = sorted_unique_validated(
        candidate_count_,
        newly_activated,
        "Maintenance newly activated set");
    const auto tail_protected = sorted_unique_validated(
        candidate_count_,
        tail_protected_candidates,
        "Maintenance tail-protected set");
    std::unordered_set<int> selected_set(selected.begin(), selected.end());
    std::unordered_set<int> activated_now_set(activated_now.begin(), activated_now.end());
    std::unordered_set<int> tail_protected_set(tail_protected.begin(), tail_protected.end());
    decision.tail_protected_candidates = tail_protected;

    std::vector<int> eligible;
    eligible.reserve(manager.activeCandidates().size());
    for (const int candidate : manager.activeCandidates()) {
        validateCandidate(candidate, "maintenance active candidate");
        if (options.protect_selected_candidates && selected_set.count(candidate) > 0) {
            ++decision.protected_selected_count;
            decision.selected_candidates_protected.push_back(candidate);
            continue;
        }
        if (activated_now_set.count(candidate) > 0) {
            ++decision.protected_newly_activated_count;
            continue;
        }
        if (active_age_[static_cast<std::size_t>(candidate)] < options.deactivation_min_age) {
            ++decision.protected_min_age_count;
            continue;
        }
        if (tail_protected_set.count(candidate) > 0) {
            ++decision.protected_tail_count;
            continue;
        }
        eligible.push_back(candidate);
    }

    decision.deactivation_candidate_count = static_cast<int>(eligible.size());
    auto scored = scores_for_candidates_with_zero_default(
        candidate_count_,
        eligible,
        active_scores);
    std::sort(
        scored.begin(),
        scored.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second < rhs.second;
            }
            return lhs.first < rhs.first;
        });
    decision.deactivation_scores_considered = scored;
    if (static_cast<int>(scored.size()) > target_count) {
        scored.resize(static_cast<std::size_t>(target_count));
    }

    std::vector<int> selected_for_deactivation;
    selected_for_deactivation.reserve(scored.size());
    for (const auto& [candidate, score] : scored) {
        (void)score;
        selected_for_deactivation.push_back(candidate);
    }
    return selected_for_deactivation;
}

void RestrictedCandidateMaintenanceTracker::recordActivated(const std::vector<int>& candidates) {
    const auto sorted = sorted_unique_validated(candidate_count_, candidates, "Maintenance activation record");
    for (const int candidate : sorted) {
        active_age_[static_cast<std::size_t>(candidate)] = 0;
        last_activated_round_[static_cast<std::size_t>(candidate)] = current_round_;
        ++activation_count_[static_cast<std::size_t>(candidate)];
        ++state_change_count_[static_cast<std::size_t>(candidate)];
        if (last_deactivated_round_[static_cast<std::size_t>(candidate)] >= 0) {
            ++total_oscillation_events_;
        }
    }
}

void RestrictedCandidateMaintenanceTracker::recordDeactivated(const std::vector<int>& candidates) {
    const auto sorted = sorted_unique_validated(candidate_count_, candidates, "Maintenance deactivation record");
    for (const int candidate : sorted) {
        active_age_[static_cast<std::size_t>(candidate)] = 0;
        last_deactivated_round_[static_cast<std::size_t>(candidate)] = current_round_;
        ++deactivation_count_[static_cast<std::size_t>(candidate)];
        ++state_change_count_[static_cast<std::size_t>(candidate)];
    }
}

void RestrictedCandidateMaintenanceTracker::completeRound(const RestrictedCandidateManager& manager) {
    for (const int candidate : manager.activeCandidates()) {
        validateCandidate(candidate, "maintenance round completion");
        ++active_age_[static_cast<std::size_t>(candidate)];
    }
    ++current_round_;
}

void RestrictedCandidateMaintenanceTracker::validateCandidate(int candidate, const char* context) const {
    if (candidate < 0 || candidate >= candidate_count_) {
        throw std::invalid_argument(
            std::string(context) + " candidate id " + std::to_string(candidate) +
            " is outside [0, " + std::to_string(candidate_count_ - 1) + "].");
    }
}

bool RestrictedCandidateMaintenanceTracker::reactivationBlockedByCooldown(int candidate, int cooldown_rounds) const {
    validateCandidate(candidate, "reactivation cooldown");
    if (cooldown_rounds < 0) {
        throw std::invalid_argument("Reactivation cooldown rounds must be nonnegative.");
    }
    const int last_deactivated = last_deactivated_round_[static_cast<std::size_t>(candidate)];
    if (last_deactivated < 0) {
        return false;
    }
    return current_round_ - last_deactivated <= cooldown_rounds;
}

}  // namespace firebreak::benders
