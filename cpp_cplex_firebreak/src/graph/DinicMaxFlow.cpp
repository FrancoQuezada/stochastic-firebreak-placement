#include "graph/DinicMaxFlow.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>

namespace firebreak::graph {
namespace {

int validate_node_count(int node_count) {
    if (node_count < 0) {
        throw std::invalid_argument("DinicMaxFlow node count cannot be negative.");
    }
    return node_count;
}

}  // namespace

DinicMaxFlow::DinicMaxFlow(int node_count)
    : adjacency_(static_cast<std::size_t>(validate_node_count(node_count))),
      level_(static_cast<std::size_t>(validate_node_count(node_count)), -1),
      next_edge_(static_cast<std::size_t>(validate_node_count(node_count)), 0) {}

void DinicMaxFlow::addEdge(int from, int to, double capacity) {
    validateNode(from);
    validateNode(to);
    if (capacity < -kEpsilon) {
        throw std::invalid_argument("DinicMaxFlow edge capacity cannot be negative.");
    }

    const double sanitized_capacity = std::max(0.0, capacity);
    Edge forward{to, static_cast<int>(adjacency_[static_cast<std::size_t>(to)].size()), sanitized_capacity};
    Edge reverse{from, static_cast<int>(adjacency_[static_cast<std::size_t>(from)].size()), 0.0};
    adjacency_[static_cast<std::size_t>(from)].push_back(forward);
    adjacency_[static_cast<std::size_t>(to)].push_back(reverse);
}

double DinicMaxFlow::maxFlow(int source, int sink) {
    validateNode(source);
    validateNode(sink);
    if (source == sink) {
        return 0.0;
    }

    phases_ = 0;
    double flow = 0.0;
    while (buildLevelGraph(source, sink)) {
        ++phases_;
        std::fill(next_edge_.begin(), next_edge_.end(), 0);
        while (true) {
            const double pushed = sendFlow(source, sink, std::numeric_limits<double>::infinity());
            if (pushed <= kEpsilon) {
                break;
            }
            flow += pushed;
        }
    }
    return flow;
}

std::vector<char> DinicMaxFlow::reachableFromSourceInResidual(int source) const {
    validateNode(source);

    std::vector<char> reachable(adjacency_.size(), 0);
    std::queue<int> frontier;
    reachable[static_cast<std::size_t>(source)] = 1;
    frontier.push(source);

    while (!frontier.empty()) {
        const int node = frontier.front();
        frontier.pop();
        for (const Edge& edge : adjacency_[static_cast<std::size_t>(node)]) {
            if (edge.capacity <= kEpsilon) {
                continue;
            }
            if (!reachable[static_cast<std::size_t>(edge.to)]) {
                reachable[static_cast<std::size_t>(edge.to)] = 1;
                frontier.push(edge.to);
            }
        }
    }
    return reachable;
}

int DinicMaxFlow::phases() const {
    return phases_;
}

bool DinicMaxFlow::buildLevelGraph(int source, int sink) {
    std::fill(level_.begin(), level_.end(), -1);
    std::queue<int> frontier;
    level_[static_cast<std::size_t>(source)] = 0;
    frontier.push(source);

    while (!frontier.empty()) {
        const int node = frontier.front();
        frontier.pop();
        for (const Edge& edge : adjacency_[static_cast<std::size_t>(node)]) {
            if (edge.capacity <= kEpsilon || level_[static_cast<std::size_t>(edge.to)] != -1) {
                continue;
            }
            level_[static_cast<std::size_t>(edge.to)] = level_[static_cast<std::size_t>(node)] + 1;
            frontier.push(edge.to);
        }
    }

    return level_[static_cast<std::size_t>(sink)] != -1;
}

double DinicMaxFlow::sendFlow(int node, int sink, double pushed) {
    if (node == sink || pushed <= kEpsilon) {
        return pushed;
    }

    auto& edges = adjacency_[static_cast<std::size_t>(node)];
    for (int& edge_index = next_edge_[static_cast<std::size_t>(node)];
         edge_index < static_cast<int>(edges.size());
         ++edge_index) {
        Edge& edge = edges[static_cast<std::size_t>(edge_index)];
        if (edge.capacity <= kEpsilon ||
            level_[static_cast<std::size_t>(edge.to)] != level_[static_cast<std::size_t>(node)] + 1) {
            continue;
        }

        const double sent = sendFlow(edge.to, sink, std::min(pushed, edge.capacity));
        if (sent <= kEpsilon) {
            continue;
        }

        edge.capacity -= sent;
        Edge& reverse_edge = adjacency_[static_cast<std::size_t>(edge.to)]
                                      [static_cast<std::size_t>(edge.reverse_index)];
        reverse_edge.capacity += sent;
        return sent;
    }
    return 0.0;
}

void DinicMaxFlow::validateNode(int node) const {
    if (node < 0 || node >= static_cast<int>(adjacency_.size())) {
        throw std::out_of_range("DinicMaxFlow node index is out of range.");
    }
}

}  // namespace firebreak::graph
