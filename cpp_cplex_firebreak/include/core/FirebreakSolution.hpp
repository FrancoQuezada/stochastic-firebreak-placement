#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace firebreak::core {

class FirebreakSolution {
public:
    FirebreakSolution() = default;
    explicit FirebreakSolution(const std::vector<int>& selected_nodes);

    static FirebreakSolution from_csv(const std::string& csv);

    bool contains(int node) const;
    std::size_t size() const;
    const std::vector<int>& selected_nodes() const;
    std::vector<int> sorted_nodes() const;

private:
    std::vector<int> selected_nodes_;
    std::unordered_set<int> selected_set_;
};

}  // namespace firebreak::core

