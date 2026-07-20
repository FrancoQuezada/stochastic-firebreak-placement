#include <cassert>
#include <iostream>
#include <vector>

#include "benders/FppStrengthening.hpp"
#include "core/LandscapeWeightMap.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_conditional_zero_benefit";
    opt.alpha = 1.0;
    opt.n_cells = 5;
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 2, 3, 4};
    opt.eligible_original_nodes = {2, 3, 4, 5};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs = {
        {0, 1, 1, 2},
        {1, 2, 2, 3},
        {0, 3, 1, 4},
        {3, 4, 4, 5},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();

    const std::vector<firebreak::core::LandscapeWeightRecord> records = {
        {1, 1.0, 1.0, 0},
        {2, 0.01, 0.01, 0},
        {3, 100.0, 100.0, 1},
        {4, 5.0, 5.0, 1},
        {5, 50.0, 50.0, 1},
    };
    opt.cell_weight_map =
        firebreak::core::make_landscape_weight_map("heterogeneous", 602, false, records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

bool contains(const std::vector<int>& values, int needle) {
    for (const int value : values) {
        if (value == needle) {
            return true;
        }
    }
    return false;
}

void test_positive_zero_benefit_detection() {
    const auto opt = make_instance();
    const auto result =
        firebreak::benders::detect_fpp_conditional_zero_benefit_candidates(opt, {1}, true);
    assert(result.enabled);
    assert(result.structural_weight_safe);
    assert(result.candidates_checked == 3);
    assert(result.fixings_attempted == 3);
    assert(result.fixings_applied == 0);
    assert(result.variables_fixed_zero == 0);
    assert(contains(result.zero_benefit_candidate_compact_nodes, 2));
    assert(!contains(result.zero_benefit_candidate_compact_nodes, 3));
    assert(!contains(result.zero_benefit_candidate_compact_nodes, 4));
}

void test_negative_reachable_candidate_is_not_fixed() {
    const auto opt = make_instance();
    const auto result =
        firebreak::benders::detect_fpp_conditional_zero_benefit_candidates(opt, {3}, true);
    assert(!contains(result.zero_benefit_candidate_compact_nodes, 1));
    assert(!contains(result.zero_benefit_candidate_compact_nodes, 2));
    assert(contains(result.zero_benefit_candidate_compact_nodes, 4));
}

void test_fixed_versus_incumbent_distinction() {
    const auto opt = make_instance();
    const auto no_fixed =
        firebreak::benders::detect_fpp_conditional_zero_benefit_candidates(opt, {}, true);
    assert(no_fixed.zero_benefit_candidate_compact_nodes.empty());

    const auto fixed_one =
        firebreak::benders::detect_fpp_conditional_zero_benefit_candidates(opt, {1}, true);
    assert(contains(fixed_one.zero_benefit_candidate_compact_nodes, 2));
}

void test_ignition_convention() {
    auto opt = make_instance();
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};
    const auto result =
        firebreak::benders::detect_fpp_conditional_zero_benefit_candidates(opt, {0}, true);
    assert(!contains(result.zero_benefit_candidate_compact_nodes, 1));
    assert(!contains(result.zero_benefit_candidate_compact_nodes, 2));
}

}  // namespace

int main() {
    test_positive_zero_benefit_detection();
    test_negative_reachable_candidate_is_not_fixed();
    test_fixed_versus_incumbent_distinction();
    test_ignition_convention();
    std::cout << "All weighted conditional zero-benefit fixing tests passed.\n";
    return 0;
}
