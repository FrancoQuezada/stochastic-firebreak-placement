#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "opt/IndexMapper.hpp"

namespace firebreak::opt {

struct CompactArc {
    int u_index = -1;
    int v_index = -1;
    int u_original_node = 0;
    int v_original_node = 0;
};

struct DpvProductPair {
    int source_index = -1;
    int successor_index = -1;
    int descendant_index = -1;
};

struct DpvNodeIndexSet {
    int node_index = -1;
    std::vector<int> successor_indices;
    std::vector<int> descendant_indices;
};

struct DpvScenarioIndexData {
    bool descendants_include_self = true;
    std::vector<DpvNodeIndexSet> node_sets;
    std::vector<DpvProductPair> product_pairs;
    int nodes_with_nonempty_successors = 0;
    int nodes_with_nonempty_descendants = 0;

    std::size_t num_pairs() const;
};

struct OptimizationScenario {
    int scenario_id = 0;
    double probability = 0.0;
    int ignition_index = -1;
    int ignition_original_node = 0;
    std::vector<CompactArc> arcs;
    std::vector<int> observed_node_indices;
    std::string message_filename;
    DpvScenarioIndexData dpv;

    std::size_t num_arcs() const;
};

struct OptimizationInstance {
    std::string landscape_name;
    double alpha = 0.0;
    int budget = 0;
    int n_cells = 0;
    IndexMapper node_mapper;
    std::vector<int> eligible_indices;
    std::vector<int> eligible_original_nodes;
    std::vector<OptimizationScenario> scenarios;
    std::vector<double> scenario_probabilities;
    std::size_t total_arcs = 0;
    std::size_t total_dpv_pairs = 0;
    std::vector<std::string> metadata_notes;

    std::size_t num_scenarios() const;
    std::size_t num_eligible_nodes() const;
    std::size_t num_mapped_nodes() const;
};

}  // namespace firebreak::opt

