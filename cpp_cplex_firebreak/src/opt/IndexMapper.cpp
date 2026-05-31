#include "opt/IndexMapper.hpp"

#include <algorithm>
#include <stdexcept>

namespace firebreak::opt {

void IndexMapper::build_from_nodes(const std::vector<int>& original_nodes) {
    original_nodes_ = original_nodes;
    std::sort(original_nodes_.begin(), original_nodes_.end());
    original_nodes_.erase(std::unique(original_nodes_.begin(), original_nodes_.end()), original_nodes_.end());

    node_to_index_.clear();
    for (std::size_t i = 0; i < original_nodes_.size(); ++i) {
        const int node = original_nodes_[i];
        if (node <= 0) {
            throw std::runtime_error("Optimization node IDs must be positive.");
        }
        node_to_index_[node] = static_cast<int>(i);
    }
}

int IndexMapper::to_index(int original_node) const {
    const auto it = node_to_index_.find(original_node);
    if (it == node_to_index_.end()) {
        throw std::runtime_error("Original node is not in the optimization index map: " + std::to_string(original_node));
    }
    return it->second;
}

int IndexMapper::to_node(int index) const {
    if (index < 0 || index >= size()) {
        throw std::runtime_error("Optimization index is out of range: " + std::to_string(index));
    }
    return original_nodes_[static_cast<std::size_t>(index)];
}

bool IndexMapper::contains_node(int original_node) const {
    return node_to_index_.find(original_node) != node_to_index_.end();
}

int IndexMapper::size() const {
    return static_cast<int>(original_nodes_.size());
}

const std::vector<int>& IndexMapper::original_nodes() const {
    return original_nodes_;
}

}  // namespace firebreak::opt

