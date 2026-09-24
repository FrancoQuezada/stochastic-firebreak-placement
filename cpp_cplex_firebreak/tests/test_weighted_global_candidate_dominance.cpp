#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include "benders/FppStrengthening.hpp"
#include "core/LandscapeWeightMap.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance(
    const std::vector<firebreak::opt::CompactArc>& scenario_one_arcs,
    const std::vector<firebreak::opt::CompactArc>& scenario_two_arcs = {}) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_global_candidate_dominance";
    opt.alpha = 1.0;
    opt.n_cells = 5;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 2, 3};
    opt.eligible_original_nodes = {2, 3, 4};

    firebreak::opt::OptimizationScenario s1;
    s1.scenario_id = 1;
    s1.probability = scenario_two_arcs.empty() ? 1.0 : 0.5;
    s1.ignition_index = 0;
    s1.ignition_original_node = 1;
    s1.observed_node_indices = {0, 1, 2, 3, 4};
    s1.arcs = scenario_one_arcs;
    opt.scenarios.push_back(s1);

    if (!scenario_two_arcs.empty()) {
        firebreak::opt::OptimizationScenario s2 = s1;
        s2.scenario_id = 2;
        s2.probability = 0.5;
        s2.arcs = scenario_two_arcs;
        opt.scenarios.push_back(s2);
    }

    opt.scenario_probabilities.assign(opt.scenarios.size(), 1.0 / opt.scenarios.size());
    opt.total_arcs = scenario_one_arcs.size() + scenario_two_arcs.size();
    return opt;
}

void attach_weights(firebreak::opt::OptimizationInstance& opt, const std::vector<double>& weights) {
    std::vector<firebreak::core::LandscapeWeightRecord> records;
    for (int compact = 0; compact < static_cast<int>(weights.size()); ++compact) {
        records.push_back({
            opt.node_mapper.to_node(compact),
            weights[static_cast<std::size_t>(compact)],
            weights[static_cast<std::size_t>(compact)],
            0});
    }
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        601,
        false,
        records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
}

void test_strict_set_inclusion_dominance() {
    auto opt = make_instance({
        {0, 1, 1, 2},
        {1, 2, 2, 3},
        {2, 3, 3, 4},
        {3, 4, 4, 5},
    });
    attach_weights(opt, {0.01, 100.0, 1.0, 50.0, 10.0});

    const auto result = firebreak::benders::apply_fpp_global_dominance_preprocessing(opt, true);
    assert(result.enabled);
    assert(result.structural_weight_safe);
    assert(result.original_candidate_count == 3);
    assert(result.candidates_removed == 2);
    assert(result.post_candidate_count == 1);
    assert((result.kept_candidate_compact_nodes == std::vector<int>{1}));
    assert((result.removed_candidate_compact_nodes == std::vector<int>{2, 3}));
}

void test_weight_map_invariance() {
    auto heterogeneous = make_instance({
        {0, 1, 1, 2},
        {1, 2, 2, 3},
        {2, 3, 3, 4},
    });
    attach_weights(heterogeneous, {1.0, 0.01, 100.0, 10.0, 1.0});

    auto clustered = heterogeneous;
    attach_weights(clustered, {10.0, 10.0, 0.1, 0.1, 50.0});

    const auto h = firebreak::benders::apply_fpp_global_dominance_preprocessing(heterogeneous, true);
    const auto c = firebreak::benders::apply_fpp_global_dominance_preprocessing(clustered, true);
    assert(h.kept_candidate_compact_nodes == c.kept_candidate_compact_nodes);
    assert(h.removed_candidate_compact_nodes == c.removed_candidate_compact_nodes);
}

void test_non_inclusion_and_cardinality_counterexamples() {
    auto opt = make_instance({
        {0, 1, 1, 2},
        {1, 3, 2, 4},
        {0, 2, 1, 3},
        {2, 4, 3, 5},
    });
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};
    attach_weights(opt, {1.0, 100.0, 0.1, 100.0, 0.1});

    const auto result = firebreak::benders::apply_fpp_global_dominance_preprocessing(opt, true);
    assert(result.candidates_removed == 0);
    assert((result.kept_candidate_compact_nodes == std::vector<int>{1, 2}));
}

void test_multiple_scenario_failure_blocks_global_dominance() {
    auto opt = make_instance(
        {
            {0, 1, 1, 2},
            {1, 2, 2, 3},
        },
        {
            {0, 2, 1, 3},
            {2, 1, 3, 2},
        });
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};
    attach_weights(opt, {1.0, 1.0, 100.0, 1.0, 1.0});

    const auto result = firebreak::benders::apply_fpp_global_dominance_preprocessing(opt, true);
    assert(result.candidates_removed == 0);
    assert((result.kept_candidate_compact_nodes == std::vector<int>{1, 2}));
}

void test_duplicate_representative_is_deterministic() {
    auto opt = make_instance({
        {0, 1, 1, 2},
        {0, 2, 1, 3},
    });
    opt.eligible_indices = {2, 1, 1};
    opt.eligible_original_nodes = {3, 2, 2};
    attach_weights(opt, {1.0, 3.0, 5.0, 1.0, 1.0});

    const auto result = firebreak::benders::apply_fpp_global_dominance_preprocessing(opt, true);
    assert(result.equivalence_classes == 1);
    assert(result.candidates_removed == 1);
    assert((result.kept_candidate_compact_nodes == std::vector<int>{2, 1}));
}

}  // namespace

int main() {
    test_strict_set_inclusion_dominance();
    test_weight_map_invariance();
    test_non_inclusion_and_cardinality_counterexamples();
    test_multiple_scenario_failure_blocks_global_dominance();
    test_duplicate_representative_is_deterministic();
    std::cout << "All weighted global candidate dominance tests passed.\n";
    return 0;
}
