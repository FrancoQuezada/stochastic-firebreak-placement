#include "benders/CandidateBoundController.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace firebreak::benders {

CandidateBoundController::CandidateBoundController(int candidate_count)
    : candidate_count_(candidate_count),
      upper_bounds_(static_cast<std::size_t>(candidate_count > 0 ? candidate_count : 0), 0.0) {
    if (candidate_count_ <= 0) {
        throw std::invalid_argument("CandidateBoundController requires candidate_count > 0.");
    }
}

int CandidateBoundController::candidateCount() const {
    return candidate_count_;
}

void CandidateBoundController::apply(const RestrictedCandidateManager& manager) {
    if (manager.candidateCount() != candidate_count_) {
        throw std::invalid_argument(
            "CandidateBoundController cannot apply a manager with a different candidate count.");
    }
    upper_bounds_ = manager.upperBounds();
}

void CandidateBoundController::setUpperBoundsFromActiveMask(const std::vector<int>& active_mask) {
    if (static_cast<int>(active_mask.size()) != candidate_count_) {
        throw std::invalid_argument("Active mask size must match CandidateBoundController candidate count.");
    }

    std::vector<double> bounds;
    bounds.reserve(active_mask.size());
    for (const int active : active_mask) {
        if (active != 0 && active != 1) {
            throw std::invalid_argument("Active mask entries must be 0 or 1.");
        }
        bounds.push_back(active == 1 ? 1.0 : 0.0);
    }
    upper_bounds_ = bounds;
}

const std::vector<double>& CandidateBoundController::upperBounds() const {
    return upper_bounds_;
}

double CandidateBoundController::upperBound(int candidate) const {
    validateCandidateId(candidate);
    return upper_bounds_[static_cast<std::size_t>(candidate)];
}

int CandidateBoundController::activeUpperBoundCount() const {
    int count = 0;
    for (const double value : upper_bounds_) {
        validateBinaryUpperBound(value);
        if (value == 1.0) {
            ++count;
        }
    }
    return count;
}

int CandidateBoundController::inactiveUpperBoundCount() const {
    int count = 0;
    for (const double value : upper_bounds_) {
        validateBinaryUpperBound(value);
        if (value == 0.0) {
            ++count;
        }
    }
    return count;
}

bool CandidateBoundController::isConsistentWith(const RestrictedCandidateManager& manager) const {
    if (manager.candidateCount() != candidate_count_) {
        return false;
    }
    return upper_bounds_ == manager.upperBounds();
}

std::vector<int> CandidateBoundController::indicesWithUpperBoundOne() const {
    std::vector<int> indices;
    for (int candidate = 0; candidate < candidate_count_; ++candidate) {
        const double value = upper_bounds_[static_cast<std::size_t>(candidate)];
        validateBinaryUpperBound(value);
        if (value == 1.0) {
            indices.push_back(candidate);
        }
    }
    return indices;
}

std::vector<int> CandidateBoundController::indicesWithUpperBoundZero() const {
    std::vector<int> indices;
    for (int candidate = 0; candidate < candidate_count_; ++candidate) {
        const double value = upper_bounds_[static_cast<std::size_t>(candidate)];
        validateBinaryUpperBound(value);
        if (value == 0.0) {
            indices.push_back(candidate);
        }
    }
    return indices;
}

void CandidateBoundController::validateCandidateId(int candidate) const {
    if (candidate < 0 || candidate >= candidate_count_) {
        throw std::invalid_argument(
            "CandidateBoundController candidate id " + std::to_string(candidate) +
            " is outside [0, " + std::to_string(candidate_count_ - 1) + "].");
    }
}

void CandidateBoundController::validateBinaryUpperBound(double value) const {
    if (value != 0.0 && value != 1.0) {
        throw std::logic_error("CandidateBoundController upper bounds must be exactly 0.0 or 1.0.");
    }
}

}  // namespace firebreak::benders
