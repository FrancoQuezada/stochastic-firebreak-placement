#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::io {

struct FirebreakSolutionRecord {
    std::string method;
    std::string landscape;
    double alpha = 0.0;
    int budget = 0;
    std::vector<int> selected_firebreak_original_nodes;
    std::vector<int> selected_firebreak_indices;
    std::string objective_metric;
    std::vector<double> selected_firebreak_scores;
};

void save_firebreak_solution_json(
    const std::filesystem::path& output_path,
    const FirebreakSolutionRecord& record);

void save_firebreak_solution_csv(
    const std::filesystem::path& output_path,
    const std::vector<int>& selected_firebreak_original_nodes);

std::vector<int> load_firebreak_solution_csv(const std::filesystem::path& input_path);

}  // namespace firebreak::io
