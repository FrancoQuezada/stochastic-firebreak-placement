#pragma once

#include <string>
#include <unordered_set>

#include "heuristics/CumulativePropagationGraph.hpp"

namespace firebreak::heuristics {

enum class GreedyMetricType {
    DPV3,
    DPV2,
    Betweenness,
    Closeness,
};

GreedyMetricType parseGreedyMetricType(const std::string& value);
std::string greedyMethodName(GreedyMetricType metric);
std::string greedyObjectiveMetricName(GreedyMetricType metric);
std::string greedyMetricFormulaNote(GreedyMetricType metric);

double scoreCandidate(
    const CumulativePropagationGraph& graph,
    int candidate,
    const std::unordered_set<int>& blocked_nodes,
    GreedyMetricType metric);

}  // namespace firebreak::heuristics
