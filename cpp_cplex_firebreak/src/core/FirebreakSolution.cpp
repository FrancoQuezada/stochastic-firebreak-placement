#include "core/FirebreakSolution.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace firebreak::core {

namespace {

std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

int parse_positive_int(const std::string& token) {
    const std::string cleaned = trim(token);
    if (cleaned.empty()) {
        throw std::runtime_error("Empty firebreak node token.");
    }
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(cleaned, &consumed);
        if (consumed != cleaned.size() || value <= 0) {
            throw std::runtime_error("Invalid firebreak node token: " + cleaned);
        }
        return value;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid firebreak node token: " + cleaned);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Firebreak node token is out of range: " + cleaned);
    }
}

}  // namespace

FirebreakSolution::FirebreakSolution(const std::vector<int>& selected_nodes) {
    for (const int node : selected_nodes) {
        if (node <= 0) {
            throw std::runtime_error("Firebreak node IDs must be positive.");
        }
        if (selected_set_.insert(node).second) {
            selected_nodes_.push_back(node);
        }
    }
}

FirebreakSolution FirebreakSolution::from_csv(const std::string& csv) {
    const std::string cleaned = trim(csv);
    if (cleaned.empty()) {
        return FirebreakSolution();
    }
    if (cleaned.front() == ',' || cleaned.back() == ',') {
        throw std::runtime_error("Firebreak list cannot start or end with a comma.");
    }

    std::vector<int> nodes;
    std::stringstream stream(cleaned);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (trim(token).empty()) {
            throw std::runtime_error("Invalid empty token in firebreak list.");
        }
        nodes.push_back(parse_positive_int(token));
    }
    return FirebreakSolution(nodes);
}

bool FirebreakSolution::contains(int node) const {
    return selected_set_.find(node) != selected_set_.end();
}

std::size_t FirebreakSolution::size() const {
    return selected_nodes_.size();
}

const std::vector<int>& FirebreakSolution::selected_nodes() const {
    return selected_nodes_;
}

std::vector<int> FirebreakSolution::sorted_nodes() const {
    std::vector<int> nodes = selected_nodes_;
    std::sort(nodes.begin(), nodes.end());
    return nodes;
}

}  // namespace firebreak::core
