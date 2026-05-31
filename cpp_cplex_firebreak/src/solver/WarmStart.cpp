#include "solver/WarmStart.hpp"

#include <stdexcept>
#include <unordered_set>

#include "io/SolutionIO.hpp"

namespace firebreak::solver {

WarmStart prepare_warm_start_from_original_nodes(
    const std::vector<int>& original_node_ids,
    const opt::OptimizationInstance& opt,
    int budget,
    const std::string& source_path) {
    if (budget < 0) {
        throw std::runtime_error("Warm-start budget must be nonnegative.");
    }

    WarmStart warm_start;
    warm_start.enabled = true;
    warm_start.source_path = source_path;

    std::unordered_set<int> seen_original_nodes;
    std::vector<int> deduplicated_original_nodes;
    deduplicated_original_nodes.reserve(original_node_ids.size());

    for (const int original_node : original_node_ids) {
        if (!seen_original_nodes.insert(original_node).second) {
            warm_start.duplicate_original_node_ids.push_back(original_node);
            continue;
        }
        deduplicated_original_nodes.push_back(original_node);
    }

    const std::unordered_set<int> eligible_indices(
        opt.eligible_indices.begin(),
        opt.eligible_indices.end());

    for (const int original_node : deduplicated_original_nodes) {
        if (!opt.node_mapper.contains_node(original_node)) {
            warm_start.ignored_original_node_ids.push_back(original_node);
            continue;
        }

        const int compact_index = opt.node_mapper.to_index(original_node);
        if (eligible_indices.find(compact_index) == eligible_indices.end()) {
            warm_start.ignored_original_node_ids.push_back(original_node);
            continue;
        }

        warm_start.original_node_ids.push_back(original_node);
        warm_start.compact_indices.push_back(compact_index);
    }

    if (warm_start.compact_indices.size() > static_cast<std::size_t>(budget)) {
        for (std::size_t pos = static_cast<std::size_t>(budget); pos < warm_start.original_node_ids.size(); ++pos) {
            warm_start.trimmed_original_node_ids.push_back(warm_start.original_node_ids[pos]);
        }
        warm_start.original_node_ids.resize(static_cast<std::size_t>(budget));
        warm_start.compact_indices.resize(static_cast<std::size_t>(budget));
        warm_start.notes.push_back("Warm start was trimmed to the firebreak budget.");
    }

    if (!warm_start.duplicate_original_node_ids.empty()) {
        warm_start.notes.push_back("Duplicate warm-start node IDs were removed after the first occurrence.");
    }
    if (!warm_start.ignored_original_node_ids.empty()) {
        warm_start.notes.push_back("Some warm-start node IDs were ignored because they are unmapped or not eligible.");
    }
    if (warm_start.compact_indices.empty()) {
        warm_start.status = "NoValidNodes";
        warm_start.notes.push_back("Warm start contained no valid eligible nodes; solver will continue without a MIP start.");
    } else if (warm_start.compact_indices.size() < static_cast<std::size_t>(budget)) {
        warm_start.status = "Partial";
        warm_start.notes.push_back("Warm start has fewer valid nodes than the firebreak budget; using a partial MIP start.");
    } else {
        warm_start.status = "Ready";
    }

    return warm_start;
}

WarmStart load_warm_start_from_csv(
    const std::filesystem::path& input_path,
    const opt::OptimizationInstance& opt,
    int budget) {
    const auto original_node_ids = io::load_firebreak_solution_csv(input_path);
    return prepare_warm_start_from_original_nodes(
        original_node_ids,
        opt,
        budget,
        input_path.string());
}

}  // namespace firebreak::solver
