#pragma once

#include <string>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::eval {

struct ScenarioRecourseResult {
    int scenario_id = 0;
    double probability = 0.0;
    double burned_count = 0.0;
    std::vector<int> burned_nodes;
    std::vector<int> reached_nodes;
};

struct FppRecourseResult {
    double expected_burned_area = 0.0;
    std::vector<ScenarioRecourseResult> scenarios;
    std::vector<std::string> warnings;
};

class FppRecourseEvaluator {
public:
    explicit FppRecourseEvaluator(const opt::OptimizationInstance& instance);

    FppRecourseResult evaluate(
        const std::vector<int>& selected_firebreak_compact_nodes,
        bool store_node_sets = false) const;

    FppRecourseResult evaluateFromBinaryVector(
        const std::vector<char>& y_selected,
        bool store_node_sets = false) const;

    bool isBurned(int scenario_index, int compact_node) const;
    bool isReached(int scenario_index, int compact_node) const;

private:
    const opt::OptimizationInstance& instance_;
    int node_count_ = 0;
    std::vector<std::vector<std::vector<int>>> adjacency_by_scenario_;
    std::vector<char> eligible_;

    mutable bool has_last_evaluation_ = false;
    mutable std::vector<std::vector<char>> last_burned_by_scenario_;
    mutable std::vector<std::vector<char>> last_reached_by_scenario_;
};

}  // namespace firebreak::eval
