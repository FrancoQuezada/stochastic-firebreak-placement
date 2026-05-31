#include "heuristics/CumulativePropagationGraph.hpp"

#include <algorithm>
#include <queue>
#include <set>
#include <stdexcept>

namespace firebreak::heuristics {

namespace {

void ensure_node_in_range(int node, int node_count) {
    if (node < 0 || node >= node_count) {
        throw std::runtime_error("Cumulative propagation graph node index is out of range.");
    }
}

}  // namespace

void CumulativePropagationGraph::buildFromOptimizationInstance(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("Cannot build a cumulative propagation graph without mapped nodes.");
    }

    node_count_ = opt.node_mapper.size();
    nodes_.clear();
    original_nodes_.clear();
    eligible_nodes_ = opt.eligible_indices;
    successors_.assign(static_cast<std::size_t>(node_count_), {});
    predecessors_.assign(static_cast<std::size_t>(node_count_), {});
    weights_.assign(static_cast<std::size_t>(node_count_), {});
    arc_count_ = 0;

    nodes_.reserve(static_cast<std::size_t>(node_count_));
    original_nodes_.reserve(static_cast<std::size_t>(node_count_));
    for (int index = 0; index < node_count_; ++index) {
        nodes_.push_back(index);
        original_nodes_.push_back(opt.node_mapper.to_node(index));
    }

    for (const auto& scenario : opt.scenarios) {
        std::set<std::pair<int, int>> scenario_arcs;
        for (const auto& arc : scenario.arcs) {
            ensure_node_in_range(arc.u_index, node_count_);
            ensure_node_in_range(arc.v_index, node_count_);
            scenario_arcs.insert({arc.u_index, arc.v_index});
        }

        for (const auto& [u, v] : scenario_arcs) {
            auto& u_weights = weights_[static_cast<std::size_t>(u)];
            const bool new_arc = u_weights.find(v) == u_weights.end();
            u_weights[v] += 1.0;
            if (new_arc) {
                successors_[static_cast<std::size_t>(u)].push_back(v);
                predecessors_[static_cast<std::size_t>(v)].push_back(u);
                ++arc_count_;
            }
        }
    }

    for (auto& list : successors_) {
        std::sort(list.begin(), list.end());
    }
    for (auto& list : predecessors_) {
        std::sort(list.begin(), list.end());
    }
    std::sort(eligible_nodes_.begin(), eligible_nodes_.end());
}

const std::vector<int>& CumulativePropagationGraph::nodes() const {
    return nodes_;
}

const std::vector<int>& CumulativePropagationGraph::eligibleNodes() const {
    return eligible_nodes_;
}

const std::vector<int>& CumulativePropagationGraph::successors(int node) const {
    if (!hasNode(node)) {
        return empty_;
    }
    return successors_[static_cast<std::size_t>(node)];
}

const std::vector<int>& CumulativePropagationGraph::predecessors(int node) const {
    if (!hasNode(node)) {
        return empty_;
    }
    return predecessors_[static_cast<std::size_t>(node)];
}

bool CumulativePropagationGraph::hasNode(int node) const {
    return node >= 0 && node < node_count_;
}

int CumulativePropagationGraph::originalNode(int compact_index) const {
    ensure_node_in_range(compact_index, node_count_);
    return original_nodes_[static_cast<std::size_t>(compact_index)];
}

double CumulativePropagationGraph::arcWeight(int u, int v) const {
    if (!hasNode(u) || !hasNode(v)) {
        return 0.0;
    }
    const auto& u_weights = weights_[static_cast<std::size_t>(u)];
    const auto found = u_weights.find(v);
    if (found == u_weights.end()) {
        return 0.0;
    }
    return found->second;
}

double CumulativePropagationGraph::inverseArcWeight(int u, int v) const {
    const double weight = arcWeight(u, v);
    if (weight <= 0.0) {
        return 0.0;
    }
    return 1.0 / weight;
}

std::vector<int> CumulativePropagationGraph::reachableFrom(
    int node,
    const std::unordered_set<int>& blocked_nodes) const {
    if (!hasNode(node) || blocked_nodes.find(node) != blocked_nodes.end()) {
        return {};
    }

    std::vector<int> reachable;
    std::vector<char> visited(static_cast<std::size_t>(node_count_), 0);
    std::queue<int> frontier;
    visited[static_cast<std::size_t>(node)] = 1;
    frontier.push(node);

    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        reachable.push_back(current);

        for (const int next : successors(current)) {
            if (blocked_nodes.find(next) != blocked_nodes.end()) {
                continue;
            }
            if (!visited[static_cast<std::size_t>(next)]) {
                visited[static_cast<std::size_t>(next)] = 1;
                frontier.push(next);
            }
        }
    }

    std::sort(reachable.begin(), reachable.end());
    return reachable;
}

std::size_t CumulativePropagationGraph::numArcs() const {
    return arc_count_;
}

int CumulativePropagationGraph::numNodes() const {
    return node_count_;
}

}  // namespace firebreak::heuristics
