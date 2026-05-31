#include <cassert>
#include <iostream>
#include <vector>

#include "opt/DpvIndexBuilder.hpp"
#include "opt/IndexMapper.hpp"
#include "opt/OptimizationInstance.hpp"

namespace {

const firebreak::opt::DpvNodeIndexSet& node_set(const firebreak::opt::DpvScenarioIndexData& data, int index) {
    for (const auto& item : data.node_sets) {
        if (item.node_index == index) {
            return item;
        }
    }
    assert(false && "missing node set");
    return data.node_sets.front();
}

std::size_t pair_count_for_source(const firebreak::opt::DpvScenarioIndexData& data, int source_index) {
    std::size_t count = 0;
    for (const auto& pair : data.product_pairs) {
        if (pair.source_index == source_index) {
            ++count;
        }
    }
    return count;
}

firebreak::opt::DpvScenarioIndexData build(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs) {
    firebreak::opt::IndexMapper mapper;
    mapper.build_from_nodes(original_nodes);

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.arcs = arcs;

    firebreak::opt::DpvIndexBuilder builder;
    return builder.build_for_scenario(scenario, mapper);
}

void test_chain_closed_descendants() {
    const auto data = build({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    });

    assert(data.descendants_include_self);
    assert((node_set(data, 0).successor_indices == std::vector<int>{1}));
    assert((node_set(data, 0).descendant_indices == std::vector<int>{0, 1, 2}));
    assert(pair_count_for_source(data, 0) == 3);
    assert(pair_count_for_source(data, 1) == 2);
    assert(pair_count_for_source(data, 2) == 0);
    assert(data.num_pairs() == 5);
}

void test_branch_descendants() {
    const auto data = build({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
    });

    assert((node_set(data, 0).successor_indices == std::vector<int>{1, 2}));
    assert((node_set(data, 0).descendant_indices == std::vector<int>{0, 1, 2}));
    assert(pair_count_for_source(data, 0) == 6);
    assert(data.num_pairs() == 6);
}

void test_isolated_node_generates_no_pairs() {
    const auto data = build({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
    });

    assert((node_set(data, 2).successor_indices == std::vector<int>{}));
    assert((node_set(data, 2).descendant_indices == std::vector<int>{2}));
    assert(pair_count_for_source(data, 2) == 0);
    assert(data.num_pairs() == 2);
}

}  // namespace

int main() {
    test_chain_closed_descendants();
    test_branch_descendants();
    test_isolated_node_generates_no_pairs();
    std::cout << "All DPV index builder tests passed.\n";
    return 0;
}

