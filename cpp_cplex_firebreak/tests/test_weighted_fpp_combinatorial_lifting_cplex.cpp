#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "benders/FppBranchBendersSolver.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/FppSaaCplexModel.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_instance(bool homogeneous = false) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_combinatorial_lifting_cplex";
    opt.alpha = 0.25;
    opt.n_cells = 5;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 2, 4};
    opt.eligible_original_nodes = {2, 3, 5};

    firebreak::opt::OptimizationScenario first;
    first.scenario_id = 1;
    first.probability = 0.5;
    first.ignition_index = 0;
    first.ignition_original_node = 1;
    first.observed_node_indices = {0, 1, 2, 3, 4};
    first.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
        firebreak::opt::CompactArc{3, 4, 4, 5},
    };

    firebreak::opt::OptimizationScenario second = first;
    second.scenario_id = 2;
    second.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
        firebreak::opt::CompactArc{0, 4, 1, 5},
    };

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.5, 0.5};
    opt.total_arcs = first.arcs.size() + second.arcs.size();
    const auto records = homogeneous
        ? std::vector<firebreak::core::LandscapeWeightRecord>{
              {1, 1.0, 1.0, 0},
              {2, 1.0, 1.0, 0},
              {3, 1.0, 1.0, 0},
              {4, 1.0, 1.0, 0},
              {5, 1.0, 1.0, 0},
          }
        : std::vector<firebreak::core::LandscapeWeightRecord>{
              {1, 10.0, 10.0, 1},
              {2, 2.0, 2.0, 0},
              {3, 50.0, 50.0, 2},
              {4, 7.0, 7.0, 0},
              {5, 31.0, 31.0, 3},
          };
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        homogeneous ? "homogeneous" : "heterogeneous",
        homogeneous ? 62041 : 62042,
        false,
        records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

firebreak::benders::FppCombinatorialBendersOptions combinatorial_options(
    firebreak::benders::FppCombinatorialBendersLiftMode lift_mode) {
    firebreak::benders::FppCombinatorialBendersOptions options;
    options.enabled = true;
    options.lift_mode = lift_mode;
    options.scenario_order =
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending;
    options.cut_sampling_ratio = 1.0;
    options.separate_fractional = false;
    options.initial_cuts = false;
    return options;
}

firebreak::solver::ModelResult solve_direct(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config) {
    firebreak::solver::FppSaaCplexModel direct;
    return direct.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, risk_config);
}

firebreak::solver::ModelResult solve_branch(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config,
    firebreak::benders::FppCombinatorialBendersLiftMode lift_mode,
    bool combinatorial,
    bool initial_cuts = false,
    bool fractional_cuts = false) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    if (combinatorial) {
        options.combinatorial_options = combinatorial_options(lift_mode);
        options.combinatorial_options.initial_cuts = initial_cuts;
        options.combinatorial_options.separate_fractional = fractional_cuts;
    }
    return solver.solve(opt, options);
}

void compare_lift_modes() {
    const auto opt = make_instance(false);
    std::vector<firebreak::risk::RiskMeasureConfig> configs;
    configs.push_back(firebreak::risk::RiskMeasureConfig());
    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    configs.push_back(cvar);
    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.5;
    configs.push_back(mean_cvar);

    const std::vector<firebreak::benders::FppCombinatorialBendersLiftMode> modes = {
        firebreak::benders::FppCombinatorialBendersLiftMode::None,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        firebreak::benders::FppCombinatorialBendersLiftMode::Posterior,
    };
    for (const auto& config : configs) {
        const auto direct = solve_direct(opt, config);
        const auto lp_callback = solve_branch(
            opt,
            config,
            firebreak::benders::FppCombinatorialBendersLiftMode::None,
            false);
        assert(direct.status == "Optimal");
        assert(lp_callback.status == "Optimal");
        for (const auto mode : modes) {
            const auto result = solve_branch(opt, config, mode, true);
            assert(result.status == "Optimal");
            assert_close(result.objective_value, direct.objective_value);
            assert_close(result.objective_value, lp_callback.objective_value);
            assert(result.combinatorial_benders_enabled);
            assert(result.combinatorial_benders_weighted);
            assert(result.combinatorial_benders_lift_mode ==
                   firebreak::benders::to_string(mode));
            assert(result.combinatorial_lifting_mode ==
                   firebreak::benders::to_string(mode));
            assert(result.combinatorial_lifting_failures == 0);
            assert(result.combinatorial_max_baseline_tightness_error <= 1.0e-6);
            assert(result.combinatorial_max_lifted_tightness_error <= 1.0e-6);
            if (mode == firebreak::benders::FppCombinatorialBendersLiftMode::None) {
                assert(!result.combinatorial_lifting_enabled);
                assert(result.combinatorial_lifting_attempts == 0);
            } else {
                assert(result.combinatorial_lifting_enabled);
                assert(result.combinatorial_lifting_attempts > 0);
                assert(result.combinatorial_lifting_successes ==
                       result.combinatorial_lifting_attempts);
                assert(result.combinatorial_lifted_cuts_dominating_baseline >=
                       result.combinatorial_lifting_attempts);
            }
        }
    }
}

void test_homogeneous_regression_for_lifting() {
    auto implicit = make_instance(true);
    implicit.cell_weight_map = firebreak::core::LandscapeWeightMap();
    implicit.compact_cell_weights.clear();
    const auto explicit_unit = make_instance(true);
    for (const auto mode : {
             firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
             firebreak::benders::FppCombinatorialBendersLiftMode::Posterior,
         }) {
        const auto implicit_result =
            solve_branch(implicit, firebreak::risk::RiskMeasureConfig(), mode, true);
        const auto explicit_result =
            solve_branch(explicit_unit, firebreak::risk::RiskMeasureConfig(), mode, true);
        assert(implicit_result.status == "Optimal");
        assert(explicit_result.status == "Optimal");
        assert_close(implicit_result.objective_value, explicit_result.objective_value);
        assert(!implicit_result.combinatorial_benders_weighted);
        assert(!explicit_result.combinatorial_benders_weighted);
        assert(implicit_result.combinatorial_lifting_mode ==
               explicit_result.combinatorial_lifting_mode);
    }
}

void test_initial_and_fractional_combinatorial_cplex() {
    const auto opt = make_instance(false);
    const auto risk_config = firebreak::risk::RiskMeasureConfig();
    const auto direct = solve_direct(opt, risk_config);
    const auto lp_callback = solve_branch(
        opt,
        risk_config,
        firebreak::benders::FppCombinatorialBendersLiftMode::None,
        false);
    const auto result = solve_branch(
        opt,
        risk_config,
        firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
        true,
        true,
        true);
    assert(direct.status == "Optimal");
    assert(lp_callback.status == "Optimal");
    assert(result.status == "Optimal");
    assert_close(result.objective_value, direct.objective_value);
    assert_close(result.objective_value, lp_callback.objective_value);
    assert(result.combinatorial_benders_initial_cuts_enabled);
    assert(result.combinatorial_benders_fractional_separation_enabled);
    assert(result.combinatorial_initial_solutions_evaluated == 1);
    assert(result.combinatorial_initial_cuts_generated ==
           static_cast<int>(opt.scenarios.size()));
    assert(result.combinatorial_benders_initial_cuts_added > 0);
    assert(result.combinatorial_fractional_cuts_enabled);
    assert(result.combinatorial_fractional_validity_mode ==
           "weighted-fractional-path-activation-user-cut-convex-hull-valid");
    assert(result.combinatorial_fractional_separation_calls >= 0);
    assert(!result.combinatorial_root_cuts_enabled);
    assert(!result.branch_benders_use_root_user_cuts);
    assert(result.combinatorial_lifting_failures == 0);
    assert(result.combinatorial_fractional_max_tightness_error >= 0.0);
}

}  // namespace

int main() {
#ifndef FIREBREAK_WITH_CPLEX
    std::cout << "Skipping weighted FPP combinatorial lifting CPLEX tests because CPLEX is not enabled.\n";
    return 0;
#else
    compare_lift_modes();
    test_homogeneous_regression_for_lifting();
    test_initial_and_fractional_combinatorial_cplex();
    std::cout << "All weighted FPP combinatorial lifting CPLEX tests passed.\n";
    return 0;
#endif
}
