#include "benders/RestrictedCandidateCutPool.hpp"

#include <algorithm>
#include <cmath>

namespace firebreak::benders {

namespace {

std::vector<std::pair<int, double>> sorted_coefficients(const BendersCut& cut) {
    auto coefficients = cut.coefficients_by_compact_index;
    std::sort(
        coefficients.begin(),
        coefficients.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
    return coefficients;
}

bool nearly_equal(double lhs, double rhs) {
    return std::fabs(lhs - rhs) <= 1.0e-12;
}

bool duplicate_cut(const BendersCut& lhs, const BendersCut& rhs) {
    if (lhs.scenario_id != rhs.scenario_id ||
        !nearly_equal(lhs.rhs_constant, rhs.rhs_constant)) {
        return false;
    }

    const auto lhs_coefficients = sorted_coefficients(lhs);
    const auto rhs_coefficients = sorted_coefficients(rhs);
    if (lhs_coefficients.size() != rhs_coefficients.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs_coefficients.size(); ++index) {
        if (lhs_coefficients[index].first != rhs_coefficients[index].first ||
            !nearly_equal(lhs_coefficients[index].second, rhs_coefficients[index].second)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool RestrictedCandidateCutPool::addCut(
    const BendersCut& cut,
    int round_index,
    const std::string& stage_name,
    int active_candidate_count) {
    for (const auto& record : records_) {
        if (duplicate_cut(record.cut, cut)) {
            ++duplicate_cuts_skipped_;
            return false;
        }
    }

    RestrictedCandidateCutRecord record;
    record.pool_index = static_cast<int>(records_.size());
    record.scenario_id = cut.scenario_id;
    record.round_index = round_index;
    record.active_candidate_count = active_candidate_count;
    record.stage_name = stage_name;
    record.cut = cut;
    records_.push_back(record);
    return true;
}

int RestrictedCandidateCutPool::addCuts(
    const std::vector<BendersCut>& cuts,
    int round_index,
    const std::string& stage_name,
    int active_candidate_count) {
    int added = 0;
    for (const auto& cut : cuts) {
        if (addCut(cut, round_index, stage_name, active_candidate_count)) {
            ++added;
        }
    }
    return added;
}

int RestrictedCandidateCutPool::size() const {
    return static_cast<int>(records_.size());
}

bool RestrictedCandidateCutPool::empty() const {
    return records_.empty();
}

int RestrictedCandidateCutPool::duplicateCutsSkipped() const {
    return duplicate_cuts_skipped_;
}

const std::vector<RestrictedCandidateCutRecord>& RestrictedCandidateCutPool::records() const {
    return records_;
}

std::vector<BendersCut> RestrictedCandidateCutPool::cuts() const {
    std::vector<BendersCut> result;
    result.reserve(records_.size());
    for (const auto& record : records_) {
        result.push_back(record.cut);
    }
    return result;
}

std::map<int, int> RestrictedCandidateCutPool::cutsByRound() const {
    std::map<int, int> counts;
    for (const auto& record : records_) {
        ++counts[record.round_index];
    }
    return counts;
}

std::map<int, int> RestrictedCandidateCutPool::cutsByScenario() const {
    std::map<int, int> counts;
    for (const auto& record : records_) {
        ++counts[record.scenario_id];
    }
    return counts;
}

}  // namespace firebreak::benders
