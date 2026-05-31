#pragma once

#include <utility>
#include <vector>

namespace firebreak::benders {

struct CandidateActivationRound {
    int round = 0;
    std::vector<int> activated;
    int activeCountAfter = 0;
};

struct CandidateDeactivationRound {
    int round = 0;
    std::vector<int> deactivated;
    int activeCountAfter = 0;
};

class RestrictedCandidateManager {
public:
    RestrictedCandidateManager(
        int candidate_count,
        int budget,
        const std::vector<int>& initial_active_candidate_ids);

    int candidateCount() const;
    int budget() const;
    int activeCount() const;
    int inactiveCount() const;
    double activeFraction() const;

    bool isActive(int candidate) const;
    bool isInactive(int candidate) const;

    const std::vector<int>& activeCandidates() const;
    const std::vector<int>& inactiveCandidates() const;
    const std::vector<CandidateActivationRound>& activationHistory() const;

    std::vector<int> activeMaskAsIntVector() const;
    std::vector<double> upperBounds() const;

    bool containsAllSelected(const std::vector<int>& selected) const;

    std::vector<int> activateCandidates(const std::vector<int>& candidates);
    std::vector<int> activateTopK(const std::vector<std::pair<int, double>>& scores, int k);
    std::vector<int> deactivateCandidates(const std::vector<int>& candidates);
    std::vector<int> deactivateBottomK(
        const std::vector<std::pair<int, double>>& scores,
        int k,
        const std::vector<int>& protected_candidates = {});
    void activateAll();

    bool allActive() const;
    const std::vector<CandidateDeactivationRound>& deactivationHistory() const;

private:
    int candidate_count_ = 0;
    int budget_ = 0;
    std::vector<int> active_mask_;
    std::vector<int> active_candidates_;
    std::vector<int> inactive_candidates_;
    std::vector<CandidateActivationRound> activation_history_;
    std::vector<CandidateDeactivationRound> deactivation_history_;

    void validateCandidateId(int candidate, const char* context) const;
    void rebuildCandidateLists();
    void recordActivationRound(const std::vector<int>& newly_activated);
    void recordDeactivationRound(const std::vector<int>& newly_deactivated);
};

std::vector<int> makeInitialActiveSetFromList(
    int candidate_count,
    int budget,
    const std::vector<int>& ids);

std::vector<int> makeInitialActiveSetFromScores(
    int candidate_count,
    int budget,
    const std::vector<std::pair<int, double>>& scores,
    int initial_size);

}  // namespace firebreak::benders
