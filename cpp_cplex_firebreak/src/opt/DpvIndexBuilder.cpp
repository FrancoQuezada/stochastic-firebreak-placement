#include "opt/DpvIndexBuilder.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace firebreak::opt {

namespace {

std::vector<int> sorted_unique(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<int> closed_reachable_from(int source, const std::vector<std::vector<int>>& adjacency) {
    std::vector<int> descendants;
    std::vector<char> visited(adjacency.size(), 0);
    std::queue<int> frontier;

    visited[static_cast<std::size_t>(source)] = 1;
    frontier.push(source);

    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        descendants.push_back(current);

        for (const int next : adjacency[static_cast<std::size_t>(current)]) {
            if (next < 0 || next >= static_cast<int>(adjacency.size())) {
                throw std::runtime_error("DPV adjacency contains an out-of-range node index.");
            }
            if (!visited[static_cast<std::size_t>(next)]) {
                visited[static_cast<std::size_t>(next)] = 1;
                frontier.push(next);
            }
        }
    }

    std::sort(descendants.begin(), descendants.end());
    return descendants;
}

}  // namespace

DpvScenarioIndexData DpvIndexBuilder::build_for_scenario(
    const OptimizationScenario& scenario,
    const IndexMapper& node_mapper) const {
    const int node_count = node_mapper.size();
    DpvScenarioIndexData data;
    data.descendants_include_self = true;
    data.node_sets.reserve(static_cast<std::size_t>(node_count));

    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(node_count));
    for (const auto& arc : scenario.arcs) {
        if (arc.u_index < 0 || arc.u_index >= node_count || arc.v_index < 0 || arc.v_index >= node_count) {
            throw std::runtime_error("Scenario arc contains an out-of-range optimization index.");
        }
        adjacency[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
    }
    for (auto& successors : adjacency) {
        successors = sorted_unique(std::move(successors));
    }

    for (int source = 0; source < node_count; ++source) {
        DpvNodeIndexSet node_set;
        node_set.node_index = source;
        node_set.successor_indices = adjacency[static_cast<std::size_t>(source)];
        node_set.descendant_indices = closed_reachable_from(source, adjacency);

        if (!node_set.successor_indices.empty()) {
            ++data.nodes_with_nonempty_successors;
        }
        if (!node_set.descendant_indices.empty()) {
            ++data.nodes_with_nonempty_descendants;
        }

        for (const int successor : node_set.successor_indices) {
            for (const int descendant : node_set.descendant_indices) {
                data.product_pairs.push_back(DpvProductPair{source, successor, descendant});
            }
        }

        data.node_sets.push_back(std::move(node_set));
    }

    return data;
}

}  // namespace firebreak::opt

