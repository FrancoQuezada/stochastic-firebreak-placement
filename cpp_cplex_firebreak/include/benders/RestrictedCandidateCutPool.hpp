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
    std::string stage_name;
    BendersCut cut;
};

class RestrictedCandidateCutPool {
public:
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

    const std::vector<RestrictedCandidateCutRecord>& records() const;
    std::vector<BendersCut> cuts() const;
    std::map<int, int> cutsByRound() const;
    std::map<int, int> cutsByScenario() const;

private:
    std::vector<RestrictedCandidateCutRecord> records_;
    int duplicate_cuts_skipped_ = 0;
};

}  // namespace firebreak::benders
