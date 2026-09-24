#pragma once

#include <map>
#include <string>
#include <vector>

#include "benders/BendersCut.hpp"

namespace firebreak::benders {

struct RestrictedCandidateCutRecord {
    int pool_index = -1;
    int scenario_id = 0;
    int round_index = 0;
    int active_candidate_count = 0;
    int creation_iteration = 0;
    double max_historical_violation = 0.0;
    int active_count = 0;
    int tight_count = 0;
    std::string objective_metric;
    std::string weight_map_hash;
    std::string stage_name;
    BendersCut cut;
};

class RestrictedCandidateCutPool {
public:
    void setWeightMapHash(const std::string& weight_map_hash);
    const std::string& weightMapHash() const;

    bool addCut(
        const BendersCut& cut,
        int round_index,
        const std::string& stage_name,
        int active_candidate_count);

    int addCuts(
        const std::vector<BendersCut>& cuts,
        int round_index,
        const std::string& stage_name,
        int active_candidate_count);

    int size() const;
    bool empty() const;
    int duplicateCutsSkipped() const;
    int evictions() const;
    int reinstantiations() const;
    int peakSize() const;

    const std::vector<RestrictedCandidateCutRecord>& records() const;
    std::vector<BendersCut> cuts() const;
    std::map<int, int> cutsByRound() const;
    std::map<int, int> cutsByScenario() const;

private:
    std::string weight_map_hash_;
    std::vector<RestrictedCandidateCutRecord> records_;
    int duplicate_cuts_skipped_ = 0;
    int evictions_ = 0;
    int reinstantiations_ = 0;
    int peak_size_ = 0;
};

}  // namespace firebreak::benders
