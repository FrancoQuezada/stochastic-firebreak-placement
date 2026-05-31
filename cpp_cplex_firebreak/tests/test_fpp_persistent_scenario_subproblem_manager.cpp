#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "benders/FppPersistentScenarioSubproblemManager.hpp"
#include "benders/FppScenarioSubproblem.hpp"
#include "opt/OptimizationInstance.hpp"
#include "solver/CplexEnvironment.hpp"

namespace {

template <typename Fn>
void assert_throws(Fn fn, const std::string& label) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        std::cerr << "Expected exception was not thrown: " << label << "\n";
    }
    assert(threw);
}

firebreak::opt::OptimizationInstance make_two_scenario_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_persistent_subproblem";
    opt.alpha = 0.5;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};

    firebreak::opt::OptimizationScenario first;
    first.scenario_id = 10;
    first.probability = 0.5;
    first.ignition_index = 0;
    first.ignition_original_node = 1;
    first.observed_node_indices = {0, 1, 2};
    first.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    first.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 20;
    second.probability = 0.5;
    second.ignition_index = 0;
    second.ignition_original_node = 1;
    second.observed_node_indices = {0, 1, 2};
    second.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
    second.arcs.push_back(firebreak::opt::CompactArc{2, 1, 3, 2});

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.5, 0.5};
    opt.total_arcs = first.arcs.size() + second.arcs.size();
    return opt;
}

void test_non_cplex_behavior() {
#ifndef FIREBREAK_WITH_CPLEX
    const auto opt = make_two_scenario_instance();
    firebreak::benders::FppPersistentScenarioSubproblemManager manager(opt, false);
    const auto diagnostics = manager.diagnostics();
    assert(!diagnostics.persistent_subproblems_enabled);
    assert(diagnostics.scenario_count == 2);
    assert_throws(
        [&] {
            (void)manager.solveScenario(0, {0, 0});
        },
        "non-CPLEX persistent subproblem solve");
    std::cout << "Skipping persistent subproblem solve checks because CPLEX is not enabled.\n";
#endif
}

#ifdef FIREBREAK_WITH_CPLEX

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-7);
}

void assert_cut_close(
    const firebreak::benders::BendersCut& actual,
    const firebreak::benders::BendersCut& expected) {
    assert(actual.scenario_id == expected.scenario_id);
    assert_close(actual.rhs_constant, expected.rhs_constant);
    assert_close(actual.subproblem_objective, expected.subproblem_objective);
    assert(actual.coefficients_by_compact_index.size() ==
           expected.coefficients_by_compact_index.size());
    for (std::size_t i = 0; i < actual.coefficients_by_compact_index.size(); ++i) {
        assert(actual.coefficients_by_compact_index[i].first ==
               expected.coefficients_by_compact_index[i].first);
        assert_close(
            actual.coefficients_by_compact_index[i].second,
            expected.coefficients_by_compact_index[i].second);
    }
    assert(actual.ybar_compact_values.size() == expected.ybar_compact_values.size());
    for (std::size_t i = 0; i < actual.ybar_compact_values.size(); ++i) {
        assert(actual.ybar_compact_values[i].first == expected.ybar_compact_values[i].first);
        assert_close(actual.ybar_compact_values[i].second, expected.ybar_compact_values[i].second);
    }
}

void test_build_once_and_update_fixed_y_values() {
    const auto opt = make_two_scenario_instance();
    firebreak::benders::FppPersistentScenarioSubproblemManager manager(opt, false);

    auto diagnostics = manager.diagnostics();
    assert(diagnostics.persistent_subproblems_enabled);
    assert(diagnostics.scenario_count == 2);
    assert(diagnostics.subproblem_model_build_count == 2);
    assert(diagnostics.subproblem_model_rebuild_count == 0);
    assert(diagnostics.subproblem_fixed_y_update_count == 0);
    assert(diagnostics.subproblem_solve_count == 0);

    firebreak::benders::FppScenarioSubproblem one_shot;
    const std::vector<std::vector<int>> ybars = {
        {0, 0},
        {1, 0},
        {0, 1},
    };

    int expected_solves = 0;
    for (const auto& ybar : ybars) {
        for (int scenario = 0; scenario < 2; ++scenario) {
            const auto persistent = manager.solveScenario(scenario, ybar);
            const auto rebuilt = one_shot.solve(opt, scenario, ybar, false);
            ++expected_solves;

            assert(persistent.scenario_id == rebuilt.scenario_id);
            assert_close(persistent.objective_value, rebuilt.objective_value);
            assert(persistent.duals_for_y_copy.size() == rebuilt.duals_for_y_copy.size());
            for (std::size_t i = 0; i < persistent.duals_for_y_copy.size(); ++i) {
                assert_close(persistent.duals_for_y_copy[i], rebuilt.duals_for_y_copy[i]);
            }
            assert_cut_close(persistent.benders_cut, rebuilt.benders_cut);
        }
    }

    diagnostics = manager.diagnostics();
    assert(diagnostics.subproblem_model_build_count == 2);
    assert(diagnostics.subproblem_model_rebuild_count == 0);
    assert(diagnostics.subproblem_fixed_y_update_count == expected_solves);
    assert(diagnostics.subproblem_solve_count == expected_solves);
    assert(diagnostics.subproblem_total_build_time >= 0.0);
    assert(diagnostics.subproblem_total_update_time >= 0.0);
    assert(diagnostics.subproblem_total_solve_time >= 0.0);
    assert(diagnostics.subproblem_average_update_time >= 0.0);
    assert(diagnostics.subproblem_average_solve_time >= 0.0);
}

void test_invalid_ybar_fails() {
    const auto opt = make_two_scenario_instance();
    firebreak::benders::FppPersistentScenarioSubproblemManager manager(opt, false);
    assert_throws(
        [&] {
            (void)manager.solveScenario(0, {0});
        },
        "wrong ybar size");
    assert_throws(
        [&] {
            (void)manager.solveScenario(0, {0, 2});
        },
        "nonbinary ybar");
    assert_throws(
        [&] {
            (void)manager.solveScenario(2, {0, 0});
        },
        "scenario out of range");
}

#endif

}  // namespace

int main() {
    test_non_cplex_behavior();
#ifdef FIREBREAK_WITH_CPLEX
    test_build_once_and_update_fixed_y_values();
    test_invalid_ybar_fails();
#endif
    std::cout << "All FPP persistent scenario subproblem manager tests passed.\n";
    return 0;
}
