#pragma once

#include <utility>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

struct BurnFrequencyCandidateScore {
    int candidate = -1;
    int compact_index = -1;
    int original_node = 0;
    double score = 0.0;
    int scenarios_burned = 0;
};

class BurnFrequencyCandidateScorer {
public:
    std::vector<BurnFrequencyCandidateScore> scoreDetailed(
        const opt::OptimizationInstance& opt) const;

    std::vector<std::pair<int, double>> scoreCandidates(
        const opt::OptimizationInstance& opt) const;
};

std::vector<int> makeInitialActiveSetFromBurnFrequency(
    const opt::OptimizationInstance& opt,
    int initial_size);

std::vector<std::pair<int, double>> topBurnFrequencyCandidates(
    const std::vector<std::pair<int, double>>& scores,
    int limit);

}  // namespace firebreak::benders
