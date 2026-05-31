#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "cuts/NodeSeparatorMinCut.hpp"

namespace {

firebreak::opt::OptimizationInstance make_opt_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    const std::vector<int>& eligible_compact_nodes = {},
    bool use_explicit_eligible = false) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = static_cast<int>(original_nodes.size());
    opt.node_mapper.build_from_nodes(original_nodes);

    if (use_explicit_eligible) {
        opt.eligible_indices = eligible_compact_nodes;
        for (const int compact_node : eligible_compact_nodes) {
            opt.eligible_original_nodes.push_back(opt.node_mapper.to_node(compact_node));
        }
    } else {
        for (const int original_node : original_nodes) {
            opt.eligible_original_nodes.push_back(original_node);
            opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
        }
    }

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = original_nodes.front();
    scenario.message_filename = "synthetic.csv";
    scenario.arcs = arcs;
    for (int i = 0; i < static_cast<int>(original_nodes.size()); ++i) {
        scenario.observed_node_indices.push_back(i);
    }

    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

bool contains_node(const std::vector<int>& nodes, int node) {
    return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-7);
}

void assert_singleton_in(const std::vector<int>& separator, const std::vector<int>& allowed_nodes) {
    assert(separator.size() == 1);
    assert(contains_node(allowed_nodes, separator.front()));
}

void test_chain_separator() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    });
    firebreak::cuts::NodeSeparatorMinCut separator(opt);

    std::vector<double> ybar(4, 0.0);
    auto result = separator.compute(0, 3, ybar);
    assert(result.feasible);
    assert_close(result.cut_capacity, 1.0);
    assert_singleton_in(result.separator_compact_nodes, {1, 2, 3});

    ybar[1] = 0.7;
    result = separator.compute(0, 3, ybar);
    assert(result.feasible);
    assert_close(result.cut_capacity, 0.3);
    assert((result.separator_compact_nodes == std::vector<int>{1}));
}

void test_branching_independent_target() {
    const auto opt = make_opt_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 4, 3, 5},
    });
    firebreak::cuts::NodeSeparatorMinCut separator(opt);

    const auto result = separator.compute(0, 3, std::vector<double>(5, 0.0));
    assert(result.feasible);
    assert_close(result.cut_capacity, 1.0);
    assert_singleton_in(result.separator_compact_nodes, {1, 3});
}

void test_two_parallel_paths_target_allowed() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    });
    firebreak::cuts::NodeSeparatorMinCut separator(opt);

    const auto result = separator.compute(0, 3, std::vector<double>(4, 0.0));
    assert(result.feasible);
    assert_close(result.cut_capacity, 1.0);
    assert((result.separator_compact_nodes == std::vector<int>{3}));
}

void test_noneligible_nodes_are_not_returned() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, {2}, true);
    firebreak::cuts::NodeSeparatorMinCut separator(opt);

    auto result = separator.compute(0, 2, std::vector<double>(3, 0.0));
    assert(result.feasible);
    assert_close(result.cut_capacity, 1.0);
    assert((result.separator_compact_nodes == std::vector<int>{2}));
    assert(!contains_node(result.separator_compact_nodes, 1));

    const auto no_eligible = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, {}, true);
    firebreak::cuts::NodeSeparatorMinCut no_eligible_separator(no_eligible);

    result = no_eligible_separator.compute(0, 2, std::vector<double>(3, 0.0));
    assert(!result.feasible);
    assert(result.separator_compact_nodes.empty());
}

void test_root_target_is_skipped() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    });
    firebreak::cuts::NodeSeparatorMinCut separator(opt);

    const auto result = separator.compute(0, 0, std::vector<double>(3, 0.0));
    assert(!result.feasible);
    assert(result.separator_compact_nodes.empty());
}

void test_unobserved_target_is_rejected() {
    auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    });
    opt.scenarios[0].observed_node_indices = {0, 1};
    firebreak::cuts::NodeSeparatorMinCut separator(opt);

    const auto result = separator.compute(0, 2, std::vector<double>(3, 0.0));
    assert(!result.feasible);
    assert(result.separator_compact_nodes.empty());
}

void test_cyclic_graph() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 1, 3, 2},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    });
    firebreak::cuts::NodeSeparatorMinCut separator(opt);

    const auto result = separator.compute(0, 3, std::vector<double>(4, 0.0));
    assert(result.feasible);
    assert_close(result.cut_capacity, 1.0);
    assert(!result.separator_compact_nodes.empty());
    assert(!contains_node(result.separator_compact_nodes, 0));
}

void test_batch_helper() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    });
    firebreak::cuts::NodeSeparatorMinCut separator(opt);

    const auto results = separator.computeForTargets(0, {1, 2}, std::vector<double>(3, 0.0));
    assert(results.size() == 2);
    assert(results[0].feasible);
    assert(results[1].feasible);
}

}  // namespace

int main() {
    test_chain_separator();
    test_branching_independent_target();
    test_two_parallel_paths_target_allowed();
    test_noneligible_nodes_are_not_returned();
    test_root_target_is_skipped();
    test_unobserved_target_is_rejected();
    test_cyclic_graph();
    test_batch_helper();
    std::cout << "Node separator min-cut tests passed.\n";
    return 0;
}
