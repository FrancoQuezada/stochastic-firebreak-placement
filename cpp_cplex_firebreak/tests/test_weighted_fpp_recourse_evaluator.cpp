#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/LandscapeWeightGenerator.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "opt/OptimizationInstance.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-9) {
    assert(std::fabs(actual - expected) <= tolerance);
}

template <typename Fn>
void expect_throw(Fn&& fn, const std::string& expected_message_fragment) {
    bool threw = false;
    try {
        fn();
    } catch (const std::runtime_error& exc) {
        threw = true;
        const std::string message = exc.what();
        assert(message.find(expected_message_fragment) != std::string::npos);
    }
    assert(threw);
}

firebreak::opt::OptimizationInstance make_opt_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    const std::vector<int>& ignition_compact_indices = {0}) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 0.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = 0;
    opt.node_mapper.build_from_nodes(original_nodes);
    for (const int original_node : original_nodes) {
        opt.eligible_original_nodes.push_back(original_node);
        opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
    }
    const double probability = 1.0 / static_cast<double>(ignition_compact_indices.size());
    for (std::size_t s = 0; s < ignition_compact_indices.size(); ++s) {
        firebreak::opt::OptimizationScenario scenario;
        scenario.scenario_id = static_cast<int>(s + 1);
        scenario.probability = probability;
        scenario.ignition_index = ignition_compact_indices[s];
        scenario.ignition_original_node = opt.node_mapper.to_node(scenario.ignition_index);
        scenario.message_filename = "synthetic.csv";
        scenario.arcs = arcs;
        for (int i = 0; i < opt.node_mapper.size(); ++i) {
            scenario.observed_node_indices.push_back(i);
        }
        opt.scenarios.push_back(std::move(scenario));
        opt.scenario_probabilities.push_back(probability);
    }
    opt.total_arcs = arcs.size() * ignition_compact_indices.size();
    return opt;
}

firebreak::core::LandscapeWeightMap make_weight_map(
    const std::vector<firebreak::core::LandscapeWeightRecord>& records,
    const std::string& profile = "manual") {
    return firebreak::core::make_landscape_weight_map(profile, 17, true, records);
}

void assert_unit_equivalence(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<int>& firebreaks) {
    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto result = evaluator.evaluate(firebreaks, true);
    assert_close(result.expected_weighted_burn_loss, result.expected_burned_area);
    for (const auto& scenario : result.scenarios) {
        assert_close(scenario.weighted_burn_loss, scenario.burned_count);
        assert(scenario.burned_cell_count == static_cast<std::size_t>(scenario.burned_count));
        assert(scenario.high_value_cells_burned == 0);
        assert_close(scenario.high_value_weight_burned, 0.0);
    }
}

void test_unit_weight_equivalence() {
    const auto chain = make_opt_instance({1, 2, 3, 4}, {
        {0, 1, 1, 2},
        {1, 2, 2, 3},
        {2, 3, 3, 4},
    });
    assert_unit_equivalence(chain, {});
    assert_unit_equivalence(chain, {1});
    assert_unit_equivalence(chain, {0});
    assert_unit_equivalence(chain, {3});

    const auto branching = make_opt_instance({1, 2, 3, 4, 5}, {
        {0, 1, 1, 2},
        {0, 2, 1, 3},
        {1, 3, 2, 4},
        {2, 4, 3, 5},
    });
    assert_unit_equivalence(branching, {1});

    const auto parallel = make_opt_instance({1, 2, 3, 4}, {
        {0, 1, 1, 2},
        {1, 3, 2, 4},
        {0, 2, 1, 3},
        {2, 3, 3, 4},
    });
    assert_unit_equivalence(parallel, {1});

    firebreak::opt::OptimizationInstance noneligible = make_opt_instance({1, 2, 3}, {
        {0, 1, 1, 2},
        {1, 2, 2, 3},
    });
    noneligible.eligible_indices = {0, 1};
    noneligible.eligible_original_nodes = {1, 2};
    assert_unit_equivalence(noneligible, {});
}

void test_manual_heterogeneous_losses() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        {0, 1, 1, 2},
        {1, 2, 2, 3},
        {2, 3, 3, 4},
    });
    const auto weights = make_weight_map({
        {1, 1.0, 1.0, 0},
        {2, 2.0, 2.0, 0},
        {3, 5.0, 5.0, 0},
        {4, 0.5, 0.5, 0},
    });
    firebreak::eval::FppRecourseEvaluator evaluator(opt, weights);
    auto result = evaluator.evaluate({}, true);
    assert_close(result.scenarios[0].weighted_burn_loss, 8.5);
    assert((result.scenarios[0].burned_original_cell_ids == std::vector<int>{1, 2, 3, 4}));

    result = evaluator.evaluate({1}, true);
    assert_close(result.scenarios[0].burned_count, 1.0);
    assert_close(result.scenarios[0].weighted_burn_loss, 1.0);
    assert((result.scenarios[0].reached_nodes == std::vector<int>{0, 1}));
}

void test_missing_weight_fails() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        {0, 1, 1, 2},
        {1, 2, 2, 3},
    });
    const auto weights = make_weight_map({
        {1, 1.0, 1.0, 0},
        {2, 2.0, 2.0, 0},
    });
    expect_throw(
        [&] {
            firebreak::eval::FppRecourseEvaluator evaluator(opt, weights);
            (void)evaluator.evaluate({});
        },
        "missing compact node");
}

void test_nonsequential_original_id_mapping() {
    const auto opt = make_opt_instance({100, 500, 300}, {
        {0, 1, 100, 300},
        {1, 2, 300, 500},
    });
    assert(opt.node_mapper.to_node(0) == 100);
    assert(opt.node_mapper.to_node(1) == 300);
    assert(opt.node_mapper.to_node(2) == 500);
    const auto weights = make_weight_map({
        {500, 7.0, 7.0, 0},
        {100, 1.0, 1.0, 0},
        {300, 3.0, 3.0, 0},
    });
    firebreak::eval::FppRecourseEvaluator evaluator(opt, weights);
    const auto result = evaluator.evaluate({}, true);
    assert_close(result.scenarios[0].weighted_burn_loss, 11.0);
    assert((result.scenarios[0].burned_original_cell_ids == std::vector<int>{100, 300, 500}));
}

void test_ignition_and_reached_firebreak_conventions() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        {0, 1, 1, 2},
        {1, 2, 2, 3},
    });
    const auto weights = make_weight_map({
        {1, 100.0, 100.0, 0},
        {2, 50.0, 50.0, 0},
        {3, 5.0, 5.0, 0},
    });
    firebreak::eval::FppRecourseEvaluator evaluator(opt, weights);
    auto result = evaluator.evaluate({0}, true);
    assert_close(result.scenarios[0].burned_count, 3.0);
    assert_close(result.scenarios[0].weighted_burn_loss, 155.0);

    result = evaluator.evaluate({1}, true);
    assert_close(result.scenarios[0].burned_count, 1.0);
    assert_close(result.scenarios[0].weighted_burn_loss, 100.0);
    assert((result.scenarios[0].reached_nodes == std::vector<int>{0, 1}));
    assert(!evaluator.isBurned(0, 1));
    assert(evaluator.isReached(0, 1));
}

void test_clustered_metrics_and_percentages() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        {0, 1, 1, 2},
        {0, 2, 1, 3},
        {2, 3, 3, 4},
    });
    const auto weights = make_weight_map({
        {1, 1.0, 1.0, 0},
        {2, 2.0, 2.0, 1},
        {3, 3.0, 3.0, 2},
        {4, 4.0, 4.0, 0},
    }, "clustered");
    firebreak::eval::FppRecourseEvaluator evaluator(opt, weights);
    auto result = evaluator.evaluate({1}, true);
    assert_close(result.total_landscape_weight, 10.0);
    assert(result.total_high_value_cells == 2);
    assert_close(result.total_high_value_weight, 5.0);
    assert_close(result.scenarios[0].weighted_burn_loss, 8.0);
    assert(result.scenarios[0].high_value_cells_burned == 1);
    assert_close(result.scenarios[0].high_value_weight_burned, 3.0);
    assert_close(result.scenarios[0].percentage_landscape_value_burned, 80.0);
    assert_close(result.scenarios[0].percentage_high_value_weight_burned, 60.0);

    const auto no_cluster_weights = make_weight_map({
        {1, 1.0, 1.0, 0},
        {2, 2.0, 2.0, 0},
        {3, 3.0, 3.0, 0},
        {4, 4.0, 4.0, 0},
    }, "heterogeneous");
    firebreak::eval::FppRecourseEvaluator no_cluster_evaluator(opt, no_cluster_weights);
    result = no_cluster_evaluator.evaluate({}, true);
    assert(result.total_high_value_cells == 0);
    assert_close(result.total_high_value_weight, 0.0);
    assert(result.scenarios[0].high_value_cells_burned == 0);
    assert_close(result.scenarios[0].high_value_weight_burned, 0.0);
    assert_close(result.scenarios[0].percentage_high_value_weight_burned, 0.0);
}

void test_weighted_statistics() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {}, {0, 1, 2, 3});
    const auto weights = make_weight_map({
        {1, 1.0, 1.0, 0},
        {2, 2.0, 2.0, 0},
        {3, 3.0, 3.0, 0},
        {4, 10.0, 10.0, 0},
    });
    firebreak::eval::FppRecourseEvaluator evaluator(opt, weights);
    const auto result = evaluator.evaluate({}, false, 0.75);
    assert_close(result.expected_weighted_burn_loss, 4.0);
    assert_close(result.weighted_loss_statistics.expected, 4.0);
    assert_close(result.weighted_loss_statistics.variance, 12.5);
    assert_close(result.weighted_loss_statistics.standard_deviation, std::sqrt(12.5));
    assert_close(result.weighted_loss_statistics.minimum, 1.0);
    assert_close(result.weighted_loss_statistics.maximum, 10.0);
    assert_close(result.weighted_loss_statistics.var, 3.0);
    assert_close(result.weighted_loss_statistics.cvar, 10.0);

    const auto tied_weights = make_weight_map({
        {1, 1.0, 1.0, 0},
        {2, 3.0, 3.0, 0},
        {3, 3.0, 3.0, 0},
        {4, 10.0, 10.0, 0},
    });
    firebreak::eval::FppRecourseEvaluator tied_evaluator(opt, tied_weights);
    const auto tied = tied_evaluator.evaluate({}, false, 0.75);
    assert_close(tied.weighted_loss_statistics.var, 3.0);
    assert_close(tied.weighted_loss_statistics.cvar, 10.0);
}

void test_generated_normalized_map_total_weight() {
    firebreak::core::LandscapeCellUniverse universe;
    universe.source = "synthetic";
    universe.rows = 2;
    universe.cols = 3;
    for (int id = 1; id <= 6; ++id) {
        universe.cells.push_back({id, (id - 1) / 3 + 1, (id - 1) % 3 + 1});
    }
    firebreak::core::LandscapeWeightGenerationConfig config;
    config.profile = "heterogeneous";
    config.seed = 123;
    config.normalize = true;
    const auto map = firebreak::core::generate_landscape_weight_map(universe, config);
    assert_close(map.total_weight, 6.0, 1.0e-9);
}

}  // namespace

int main() {
    test_unit_weight_equivalence();
    test_manual_heterogeneous_losses();
    test_missing_weight_fails();
    test_nonsequential_original_id_mapping();
    test_ignition_and_reached_firebreak_conventions();
    test_clustered_metrics_and_percentages();
    test_weighted_statistics();
    test_generated_normalized_map_total_weight();
    std::cout << "All weighted FPP recourse evaluator tests passed.\n";
    return 0;
}
