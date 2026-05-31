#pragma once

#include <vector>

#include "benders/RestrictedCandidateManager.hpp"

namespace firebreak::benders {

class CandidateBoundController {
public:
    explicit CandidateBoundController(int candidate_count);

    int candidateCount() const;

    void apply(const RestrictedCandidateManager& manager);
    void setUpperBoundsFromActiveMask(const std::vector<int>& active_mask);

    const std::vector<double>& upperBounds() const;
    double upperBound(int candidate) const;

    int activeUpperBoundCount() const;
    int inactiveUpperBoundCount() const;

    bool isConsistentWith(const RestrictedCandidateManager& manager) const;

    std::vector<int> indicesWithUpperBoundOne() const;
    std::vector<int> indicesWithUpperBoundZero() const;

private:
    int candidate_count_ = 0;
    std::vector<double> upper_bounds_;

    void validateCandidateId(int candidate) const;
    void validateBinaryUpperBound(double value) const;
};

}  // namespace firebreak::benders
