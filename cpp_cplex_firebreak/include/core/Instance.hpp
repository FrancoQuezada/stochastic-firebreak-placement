#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/Scenario.hpp"

namespace firebreak::core {

struct Instance {
    std::string landscape_name;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;

    int rows = 0;
    int cols = 0;
    int n_cells = 0;
    bool has_forest_size = false;

    bool available_nodes_known = false;
    std::vector<int> available_nodes;

    std::vector<Scenario> scenarios;

    std::size_t num_scenarios() const;
    std::size_t num_available_nodes() const;
    int min_scenario_id() const;
    int max_scenario_id() const;
};

}  // namespace firebreak::core
