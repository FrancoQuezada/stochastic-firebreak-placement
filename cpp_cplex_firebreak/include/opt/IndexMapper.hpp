#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace firebreak::opt {

class IndexMapper {
public:
    void build_from_nodes(const std::vector<int>& original_nodes);

    int to_index(int original_node) const;
    int to_node(int index) const;
    bool contains_node(int original_node) const;
    int size() const;
    const std::vector<int>& original_nodes() const;

private:
    std::vector<int> original_nodes_;
    std::unordered_map<int, int> node_to_index_;
};

}  // namespace firebreak::opt

