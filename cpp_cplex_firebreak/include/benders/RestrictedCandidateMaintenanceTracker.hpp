#pragma once

#include <utility>
#include <string>
#include <vector>

#include "benders/RestrictedCandidateManager.hpp"

namespace firebreak::benders {

struct RestrictedCandidateMaintenanceOptions {
    int min_active_size = 0;
    int max_active_size = 0;
    int deactivation_batch_size = 0;
    int deactivation_min_age = 1;
    int reactivation_cooldown_rounds = 1;
    bool protect_selected_candidates = true;
};

struct RestrictedCandidateMaintenanceDecision {
    int maintenance_round = 0;
    bool weighted = false;
    std::string weight_map_hash;
    int active_count_before_maintenance = 0;
    int active_count_after_activation = 0;
    int active_count_after_deactivation = 0;
    std::vector<int> activated;
    std::vector<int> deactivated;
    std::vector<int> selected_candidates_protected;
    std::vector<int> tail_protected_candidates;
    std::vector<int> top_activation_candidates;
    std::vector<std::pair<int, double>> top_activation_scores;
    std::vector<std::pair<int, double>> deactivation_scores_considered;
    int protected_selected_count = 0;
    int protected_min_age_count = 0;
    int protected_cooldown_count = 0;
    int protected_newly_activated_count = 0;
    int protected_tail_count = 0;
    int deactivation_candidate_count = 0;
    int reactivation_blocked_by_cooldown_count = 0;
    int oscillation_event_count = 0;
};

class RestrictedCandidateMaintenanceTracker {
public:
    RestrictedCandidateMaintenanceTracker(
        int candidate_count,
        const std::vector<int>& initial_active_candidates,
        std::string weight_map_hash = {});

    int currentRound() const;
    const std::string& weightMapHash() const;
    void setWeightMapHash(const std::string& weight_map_hash);
    int activeAge(int candidate) const;
    int lastActivatedRound(int candidate) const;
    int lastDeactivatedRound(int candidate) const;
    int stateChangeCount(int candidate) const;
    int activationCount(int candidate) const;
    int deactivationCount(int candidate) const;
    int maxStateChangeCount() const;
    double averageStateChangeCount() const;
    int totalReactivationBlockedByCooldown() const;
    int totalOscillationEvents() const;

    std::vector<std::pair<int, double>> filterActivationScores(
        const std::vector<std::pair<int, double>>& inactive_scores,
        int reactivation_cooldown_rounds,
        RestrictedCandidateMaintenanceDecision& decision);

    std::vector<int> selectDeactivationCandidates(
        const RestrictedCandidateManager& manager,
        const std::vector<std::pair<int, double>>& active_scores,
        const std::vector<int>& selected_candidates,
        const std::vector<int>& newly_activated,
        const RestrictedCandidateMaintenanceOptions& options,
        int requested_deactivation_count,
        RestrictedCandidateMaintenanceDecision& decision) const;

    std::vector<int> selectDeactivationCandidates(
        const RestrictedCandidateManager& manager,
        const std::vector<std::pair<int, double>>& active_scores,
        const std::vector<int>& selected_candidates,
        const std::vector<int>& newly_activated,
        const std::vector<int>& tail_protected_candidates,
        const RestrictedCandidateMaintenanceOptions& options,
        int requested_deactivation_count,
        RestrictedCandidateMaintenanceDecision& decision) const;

    void recordActivated(const std::vector<int>& candidates);
    void recordDeactivated(const std::vector<int>& candidates);
    void completeRound(const RestrictedCandidateManager& manager);

private:
    int candidate_count_ = 0;
    std::string weight_map_hash_;
    int current_round_ = 0;
    std::vector<int> active_age_;
    std::vector<int> last_activated_round_;
    std::vector<int> last_deactivated_round_;
    std::vector<int> state_change_count_;
    std::vector<int> activation_count_;
    std::vector<int> deactivation_count_;
    int total_reactivation_blocked_by_cooldown_ = 0;
    int total_oscillation_events_ = 0;

    void validateCandidate(int candidate, const char* context) const;
    bool reactivationBlockedByCooldown(int candidate, int cooldown_rounds) const;
};

}  // namespace firebreak::benders
