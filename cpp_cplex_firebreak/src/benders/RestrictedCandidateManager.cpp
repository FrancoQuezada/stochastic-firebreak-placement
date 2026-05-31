#include "benders/RestrictedCandidateManager.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace firebreak::benders {

namespace {

void validate_counts(int candidate_count, int budget) {
    if (candidate_count <= 0) {
        throw std::invalid_argument("RestrictedCandidateManager requires candidate_count > 0.");
    }
    if (budget < 0) {
        throw std::invalid_argument("RestrictedCandidateManager requires budget >= 0.");
    }
    if (budget > candidate_count) {
        throw std::invalid_argument("RestrictedCandidateManager requires budget <= candidate_count.");
    }
}

void validate_candidate_id(int candidate_count, int candidate, const char* context) {
    if (candidate < 0 || candidate >= candidate_count) {
        throw std::invalid_argument(
            std::string(context) + " candidate id " + std::to_string(candidate) +
            " is outside [0, " + std::to_string(candidate_count - 1) + "].");
    }
}

std::vector<int> sorted_unique_validated_ids(
    int candidate_count,
    const std::vector<int>& ids,
    const char* context) {
    std::vector<int> sorted = ids;
    for (const int id : sorted) {
        validate_candidate_id(candidate_count, id, context);
    }

    std::sort(sorted.begin(), sorted.end());
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument(
            std::string(context) + " contains duplicate candidate id " + std::to_string(*duplicate) + ".");
    }
    return sorted;
}

std::vector<int> sorted_validated_ids_collapsed(
    int candidate_count,
    const std::vector<int>& ids,
    const char* context) {
    std::vector<int> sorted = ids;
    for (const int id : sorted) {
        validate_candidate_id(candidate_count, id, context);
    }
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return sorted;
}

std::vector<std::pair<int, double>> sorted_scores_descending(
    int candidate_count,
    const std::vector<std::pair<int, double>>& scores,
    const char* context) {
    std::unordered_set<int> seen;
    seen.reserve(scores.size());
    for (const auto& [candidate, score] : scores) {
        (void)score;
        validate_candidate_id(candidate_count, candidate, context);
        if (!seen.insert(candidate).second) {
            throw std::invalid_argument(
                std::string(context) + " contains duplicate candidate id " + std::to_string(candidate) + ".");
        }
    }

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

std::vector<std::pair<int, double>> sorted_scores_ascending(
    int candidate_count,
    const std::vector<std::pair<int, double>>& scores,
    const char* context) {
    std::unordered_set<int> seen;
    seen.reserve(scores.size());
    for (const auto& [candidate, score] : scores) {
        (void)score;
        validate_candidate_id(candidate_count, candidate, context);
        if (!seen.insert(candidate).second) {
            throw std::invalid_argument(
                std::string(context) + " contains duplicate candidate id " + std::to_string(candidate) + ".");
        }
    }

    std::vector<std::pair<int, double>> sorted = scores;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second < rhs.second;
            }
            return lhs.first < rhs.first;
        });
    return sorted;
}

}  // namespace

RestrictedCandidateManager::RestrictedCandidateManager(
    int candidate_count,
    int budget,
    const std::vector<int>& initial_active_candidate_ids)
    : candidate_count_(candidate_count),
      budget_(budget),
      active_mask_(static_cast<std::size_t>(std::max(candidate_count, 0)), 0) {
    validate_counts(candidate_count_, budget_);

    const std::vector<int> initial_active = sorted_unique_validated_ids(
        candidate_count_,
        initial_active_candidate_ids,
        "Initial active candidate set");

    if (static_cast<int>(initial_active.size()) < budget_) {
        throw std::invalid_argument("Initial active candidate set size must be at least the firebreak budget.");
    }

    for (const int candidate : initial_active) {
        active_mask_[static_cast<std::size_t>(candidate)] = 1;
    }
    rebuildCandidateLists();
}

int RestrictedCandidateManager::candidateCount() const {
    return candidate_count_;
}

int RestrictedCandidateManager::budget() const {
    return budget_;
}

int RestrictedCandidateManager::activeCount() const {
    return static_cast<int>(active_candidates_.size());
}

int RestrictedCandidateManager::inactiveCount() const {
    return static_cast<int>(inactive_candidates_.size());
}

double RestrictedCandidateManager::activeFraction() const {
    return static_cast<double>(activeCount()) / static_cast<double>(candidate_count_);
}

bool RestrictedCandidateManager::isActive(int candidate) const {
    validateCandidateId(candidate, "isActive");
    return active_mask_[static_cast<std::size_t>(candidate)] != 0;
}

bool RestrictedCandidateManager::isInactive(int candidate) const {
    validateCandidateId(candidate, "isInactive");
    return !isActive(candidate);
}

const std::vector<int>& RestrictedCandidateManager::activeCandidates() const {
    return active_candidates_;
}

const std::vector<int>& RestrictedCandidateManager::inactiveCandidates() const {
    return inactive_candidates_;
}

const std::vector<CandidateActivationRound>& RestrictedCandidateManager::activationHistory() const {
    return activation_history_;
}

const std::vector<CandidateDeactivationRound>& RestrictedCandidateManager::deactivationHistory() const {
    return deactivation_history_;
}

std::vector<int> RestrictedCandidateManager::activeMaskAsIntVector() const {
    return active_mask_;
}

std::vector<double> RestrictedCandidateManager::upperBounds() const {
    std::vector<double> bounds;
    bounds.reserve(active_mask_.size());
    for (const int active : active_mask_) {
        bounds.push_back(active != 0 ? 1.0 : 0.0);
    }
    return bounds;
}

bool RestrictedCandidateManager::containsAllSelected(const std::vector<int>& selected) const {
    for (const int candidate : selected) {
        validateCandidateId(candidate, "containsAllSelected");
        if (!isActive(candidate)) {
            return false;
        }
    }
    return true;
}

std::vector<int> RestrictedCandidateManager::activateCandidates(const std::vector<int>& candidates) {
    const std::vector<int> requested = sorted_validated_ids_collapsed(
        candidate_count_,
        candidates,
        "Activation candidate list");

    std::vector<int> newly_activated;
    for (const int candidate : requested) {
        if (active_mask_[static_cast<std::size_t>(candidate)] == 0) {
            active_mask_[static_cast<std::size_t>(candidate)] = 1;
            newly_activated.push_back(candidate);
        }
    }

    if (!newly_activated.empty()) {
        rebuildCandidateLists();
        recordActivationRound(newly_activated);
    }
    return newly_activated;
}

std::vector<int> RestrictedCandidateManager::activateTopK(
    const std::vector<std::pair<int, double>>& scores,
    int k) {
    if (k <= 0) {
        throw std::invalid_argument("activateTopK requires k > 0.");
    }

    const auto sorted_scores = sorted_scores_descending(candidate_count_, scores, "activateTopK scores");
    std::vector<int> selected;
    selected.reserve(static_cast<std::size_t>(std::min(k, inactiveCount())));
    for (const auto& [candidate, score] : sorted_scores) {
        (void)score;
        if (static_cast<int>(selected.size()) >= k) {
            break;
        }
        if (active_mask_[static_cast<std::size_t>(candidate)] == 0) {
            selected.push_back(candidate);
        }
    }

    return activateCandidates(selected);
}

std::vector<int> RestrictedCandidateManager::deactivateCandidates(const std::vector<int>& candidates) {
    const std::vector<int> requested = sorted_validated_ids_collapsed(
        candidate_count_,
        candidates,
        "Deactivation candidate list");

    std::vector<int> newly_deactivated;
    for (const int candidate : requested) {
        if (active_mask_[static_cast<std::size_t>(candidate)] != 0) {
            newly_deactivated.push_back(candidate);
        }
    }

    if (activeCount() - static_cast<int>(newly_deactivated.size()) < budget_) {
        throw std::invalid_argument(
            "Deactivation would reduce the active candidate set below the firebreak budget.");
    }

    for (const int candidate : newly_deactivated) {
        active_mask_[static_cast<std::size_t>(candidate)] = 0;
    }

    if (!newly_deactivated.empty()) {
        rebuildCandidateLists();
        recordDeactivationRound(newly_deactivated);
    }
    return newly_deactivated;
}

std::vector<int> RestrictedCandidateManager::deactivateBottomK(
    const std::vector<std::pair<int, double>>& scores,
    int k,
    const std::vector<int>& protected_candidates) {
    if (k <= 0) {
        throw std::invalid_argument("deactivateBottomK requires k > 0.");
    }

    std::unordered_set<int> protected_set;
    protected_set.reserve(protected_candidates.size());
    for (const int candidate : protected_candidates) {
        validateCandidateId(candidate, "deactivateBottomK protected candidates");
        protected_set.insert(candidate);
    }

    const auto sorted_scores = sorted_scores_ascending(candidate_count_, scores, "deactivateBottomK scores");

    std::vector<std::pair<int, double>> candidates_by_score;
    candidates_by_score.reserve(active_candidates_.size());
    for (const int candidate : active_candidates_) {
        if (protected_set.count(candidate) > 0) {
            continue;
        }
        double score = 0.0;
        const auto score_it = std::find_if(
            sorted_scores.begin(),
            sorted_scores.end(),
            [candidate](const auto& item) {
                return item.first == candidate;
            });
        if (score_it != sorted_scores.end()) {
            score = score_it->second;
        }
        candidates_by_score.push_back({candidate, score});
    }
    std::sort(
        candidates_by_score.begin(),
        candidates_by_score.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second < rhs.second;
            }
            return lhs.first < rhs.first;
        });
    if (static_cast<int>(candidates_by_score.size()) > k) {
        candidates_by_score.resize(static_cast<std::size_t>(k));
    }

    const int allowed_deactivations =
        std::max(0, activeCount() - budget_);
    if (static_cast<int>(candidates_by_score.size()) > allowed_deactivations) {
        candidates_by_score.resize(static_cast<std::size_t>(allowed_deactivations));
    }

    std::vector<int> selected;
    selected.reserve(candidates_by_score.size());
    for (const auto& [candidate, score] : candidates_by_score) {
        (void)score;
        selected.push_back(candidate);
    }
    return deactivateCandidates(selected);
}

void RestrictedCandidateManager::activateAll() {
    std::vector<int> newly_activated = inactive_candidates_;
    if (newly_activated.empty()) {
        return;
    }

    for (const int candidate : newly_activated) {
        active_mask_[static_cast<std::size_t>(candidate)] = 1;
    }
    rebuildCandidateLists();
    recordActivationRound(newly_activated);
}

bool RestrictedCandidateManager::allActive() const {
    return inactive_candidates_.empty();
}

void RestrictedCandidateManager::validateCandidateId(int candidate, const char* context) const {
    validate_candidate_id(candidate_count_, candidate, context);
}

void RestrictedCandidateManager::rebuildCandidateLists() {
    active_candidates_.clear();
    inactive_candidates_.clear();
    active_candidates_.reserve(active_mask_.size());
    inactive_candidates_.reserve(active_mask_.size());
    for (int candidate = 0; candidate < candidate_count_; ++candidate) {
        if (active_mask_[static_cast<std::size_t>(candidate)] != 0) {
            active_candidates_.push_back(candidate);
        } else {
            inactive_candidates_.push_back(candidate);
        }
    }
}

void RestrictedCandidateManager::recordActivationRound(const std::vector<int>& newly_activated) {
    if (newly_activated.empty()) {
        return;
    }

    CandidateActivationRound round;
    round.round = static_cast<int>(activation_history_.size());
    round.activated = newly_activated;
    round.activeCountAfter = activeCount();
    activation_history_.push_back(round);
}

void RestrictedCandidateManager::recordDeactivationRound(const std::vector<int>& newly_deactivated) {
    if (newly_deactivated.empty()) {
        return;
    }

    CandidateDeactivationRound round;
    round.round = static_cast<int>(deactivation_history_.size());
    round.deactivated = newly_deactivated;
    round.activeCountAfter = activeCount();
    deactivation_history_.push_back(round);
}

std::vector<int> makeInitialActiveSetFromList(
    int candidate_count,
    int budget,
    const std::vector<int>& ids) {
    validate_counts(candidate_count, budget);
    const std::vector<int> active = sorted_unique_validated_ids(candidate_count, ids, "Initial active list");
    if (static_cast<int>(active.size()) < budget) {
        throw std::invalid_argument("Initial active list size must be at least the firebreak budget.");
    }
    return active;
}

std::vector<int> makeInitialActiveSetFromScores(
    int candidate_count,
    int budget,
    const std::vector<std::pair<int, double>>& scores,
    int initial_size) {
    validate_counts(candidate_count, budget);
    if (initial_size < budget) {
        throw std::invalid_argument("Initial candidate size must be at least the firebreak budget.");
    }
    if (initial_size > candidate_count) {
        throw std::invalid_argument("Initial candidate size must be <= candidate_count.");
    }

    const auto sorted_scores = sorted_scores_descending(candidate_count, scores, "Initial candidate scores");
    if (static_cast<int>(sorted_scores.size()) < initial_size) {
        throw std::invalid_argument("Initial candidate scores do not contain enough unique candidate ids.");
    }

    std::vector<int> active;
    active.reserve(static_cast<std::size_t>(initial_size));
    for (int index = 0; index < initial_size; ++index) {
        active.push_back(sorted_scores[static_cast<std::size_t>(index)].first);
    }
    std::sort(active.begin(), active.end());
    return active;
}

}  // namespace firebreak::benders
