#pragma once

#include <string>
#include <vector>

#include "core/LandscapeWeightMap.hpp"
#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"

namespace firebreak::eval {

struct WeightedLossStatistics {
    double expected = 0.0;
    double variance = 0.0;
    double standard_deviation = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double var = 0.0;
    double cvar = 0.0;
    double beta = 0.9;
    int tail_count = 0;
};

struct ScenarioRecourseResult {
    int scenario_id = 0;
    double probability = 0.0;
    double burned_count = 0.0;
    std::size_t burned_cell_count = 0;
    double weighted_burn_loss = 0.0;
    std::size_t high_value_cells_burned = 0;
    double high_value_weight_burned = 0.0;
    double percentage_landscape_value_burned = 0.0;
    double percentage_high_value_weight_burned = 0.0;
    std::vector<int> burned_nodes;
    std::vector<int> reached_nodes;
    std::vector<int> burned_original_cell_ids;
    std::vector<int> reached_original_cell_ids;
};

struct FppRecourseResult {
    double expected_burned_area = 0.0;
    double expected_weighted_burn_loss = 0.0;
    double expected_high_value_weight_burned = 0.0;
    double expected_percentage_landscape_value_burned = 0.0;
    double expected_percentage_high_value_weight_burned = 0.0;
    double total_landscape_weight = 0.0;
    std::size_t total_high_value_cells = 0;
    double total_high_value_weight = 0.0;
    std::string weight_profile = "homogeneous";
    std::string weight_map_hash;
    WeightedLossStatistics weighted_loss_statistics;
    std::vector<ScenarioRecourseResult> scenarios;
    std::vector<std::string> warnings;
};

WeightedLossStatistics compute_weighted_loss_statistics(
    const std::vector<risk::WeightedLoss>& losses,
    double beta = 0.9);

class FppRecourseEvaluator {
public:
    explicit FppRecourseEvaluator(const opt::OptimizationInstance& instance);
    FppRecourseEvaluator(
        const opt::OptimizationInstance& instance,
        const core::LandscapeWeightMap& weight_map);

    FppRecourseResult evaluate(
        const std::vector<int>& selected_firebreak_compact_nodes,
        bool store_node_sets = false,
        double cvar_beta = 0.9) const;

    FppRecourseResult evaluateFromBinaryVector(
        const std::vector<char>& y_selected,
        bool store_node_sets = false,
        double cvar_beta = 0.9) const;

    bool isBurned(int scenario_index, int compact_node) const;
    bool isReached(int scenario_index, int compact_node) const;

private:
    const opt::OptimizationInstance& instance_;
    int node_count_ = 0;
    std::vector<std::vector<std::vector<int>>> adjacency_by_scenario_;
    std::vector<char> eligible_;
    std::vector<double> compact_weights_;
    std::vector<int> compact_cluster_ids_;
    double total_landscape_weight_ = 0.0;
    std::size_t total_high_value_cells_ = 0;
    double total_high_value_weight_ = 0.0;
    std::string weight_profile_ = "homogeneous";
    std::string weight_map_hash_;

    mutable bool has_last_evaluation_ = false;
    mutable std::vector<std::vector<char>> last_burned_by_scenario_;
    mutable std::vector<std::vector<char>> last_reached_by_scenario_;
};

}  // namespace firebreak::eval
