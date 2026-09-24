#include <cassert>
#include <cmath>
#include <iostream>
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
    opt.landscape_name = "weighted_combinatorial_sampling_cplex";
    opt.alpha = 0.25;
    opt.n_cells = 6;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5, 6});
    opt.eligible_indices = {1, 2, 4};
    opt.eligible_original_nodes = {2, 3, 5};

    auto make_scenario = [](int id, std::vector<firebreak::opt::CompactArc> arcs) {
        firebreak::opt::OptimizationScenario scenario;
        scenario.scenario_id = id;
        scenario.probability = 0.25;
        scenario.ignition_index = 0;
        scenario.ignition_original_node = 1;
        scenario.observed_node_indices = {0, 1, 2, 3, 4, 5};
        scenario.arcs = std::move(arcs);
        return scenario;
    };

    opt.scenarios = {
        make_scenario(10, {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 3, 2, 4},
            firebreak::opt::CompactArc{3, 5, 4, 6},
        }),
        make_scenario(20, {
            firebreak::opt::CompactArc{0, 2, 1, 3},
            firebreak::opt::CompactArc{2, 3, 3, 4},
            firebreak::opt::CompactArc{3, 5, 4, 6},
        }),
        make_scenario(30, {
            firebreak::opt::CompactArc{0, 4, 1, 5},
            firebreak::opt::CompactArc{4, 5, 5, 6},
        }),
        make_scenario(40, {
            firebreak::opt::CompactArc{0, 1, 1, 2},
            firebreak::opt::CompactArc{1, 2, 2, 3},
            firebreak::opt::CompactArc{2, 5, 3, 6},
            firebreak::opt::CompactArc{0, 4, 1, 5},
        }),
    };
    opt.scenario_probabilities.assign(opt.scenarios.size(), 0.25);
    for (const auto& scenario : opt.scenarios) {
        opt.total_arcs += scenario.arcs.size();
    }

    const auto records = homogeneous
        ? std::vector<firebreak::core::LandscapeWeightRecord>{
              {1, 1.0, 1.0, 0},
              {2, 1.0, 1.0, 0},
              {3, 1.0, 1.0, 0},
              {4, 1.0, 1.0, 0},
              {5, 1.0, 1.0, 0},
              {6, 1.0, 1.0, 0},
          }
        : std::vector<firebreak::core::LandscapeWeightRecord>{
              {1, 3.0, 3.0, 0},
              {2, 17.0, 17.0, 1},
              {3, 2.0, 2.0, 0},
              {4, 31.0, 31.0, 2},
              {5, 5.0, 5.0, 0},
              {6, 89.0, 89.0, 3},
          };
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        homogeneous ? "homogeneous" : "heterogeneous",
        homogeneous ? 62061 : 62062,
        false,
        records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
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
    bool combinatorial,
    firebreak::benders::FppCombinatorialBendersScenarioOrder order =
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending,
    double ratio = 1.0,
    firebreak::benders::FppCombinatorialBendersLiftMode lift =
        firebreak::benders::FppCombinatorialBendersLiftMode::None,
    bool initial = false,
    bool fractional = false) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    if (combinatorial) {
        options.combinatorial_options.enabled = true;
        options.combinatorial_options.scenario_order = order;
        options.combinatorial_options.cut_sampling_ratio = ratio;
        options.combinatorial_options.lift_mode = lift;
        options.combinatorial_options.initial_cuts = initial;
        options.combinatorial_options.separate_fractional = fractional;
    }
    return solver.solve(opt, options);
}

void assert_exact_result(
    const firebreak::solver::ModelResult& result,
    const firebreak::solver::ModelResult& direct,
    const firebreak::solver::ModelResult& lp,
    const firebreak::solver::ModelResult& full_combinatorial,
    int expected_sample_size,
    bool fallback_enabled) {
    assert(result.status == "Optimal");
    assert_close(result.objective_value, direct.objective_value);
    assert_close(result.objective_value, lp.objective_value);
    assert_close(result.objective_value, full_combinatorial.objective_value);
    assert(result.combinatorial_benders_enabled);
    assert(result.combinatorial_scenario_policy_exact);
    assert(!result.combinatorial_scenario_policy_heuristic);
    assert(result.combinatorial_full_verification_before_acceptance);
    assert(result.combinatorial_realized_sample_size == expected_sample_size);
    assert(result.combinatorial_sampling_exact_fallback == fallback_enabled);
    assert(result.combinatorial_lifting_failures == 0);
}

void test_expected_order_ratio_lift_matrix() {
    const auto opt = make_instance(false);
    const auto risk = firebreak::risk::RiskMeasureConfig();
    const auto direct = solve_direct(opt, risk);
    const auto lp = solve_branch(opt, risk, false);
    const auto full = solve_branch(opt, risk, true);
    assert(direct.status == "Optimal");
    assert(lp.status == "Optimal");
    assert(full.status == "Optimal");
    assert_exact_result(full, direct, lp, full, 4, false);

    for (const auto order : {
             firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending,
             firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
         }) {
        for (const double ratio : {1.0, 0.5, 0.1}) {
            for (const auto lift : {
                     firebreak::benders::FppCombinatorialBendersLiftMode::None,
                     firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
                     firebreak::benders::FppCombinatorialBendersLiftMode::Posterior,
                 }) {
                const auto result = solve_branch(opt, risk, true, order, ratio, lift);
                const int expected_sample_size =
                    firebreak::benders::fpp_combinatorial_realized_sample_size(
                        opt.scenarios.size(),
                        ratio);
                assert_exact_result(
                    result,
                    direct,
                    lp,
                    full,
                    expected_sample_size,
                    ratio < 1.0);
            }
        }
    }
}

void test_cvar_mean_cvar_initial_fractional_and_homogeneous() {
    const auto opt = make_instance(false);
    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.5;

    for (const auto risk : {cvar, mean_cvar}) {
        const auto direct = solve_direct(opt, risk);
        const auto lp = solve_branch(opt, risk, false);
        const auto full = solve_branch(opt, risk, true);
        const auto sampled = solve_branch(
            opt,
            risk,
            true,
            firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
            0.1,
            firebreak::benders::FppCombinatorialBendersLiftMode::Heuristic,
            true,
            true);
        assert_exact_result(sampled, direct, lp, full, 1, true);
        assert(sampled.combinatorial_benders_initial_cuts_enabled);
        assert(sampled.combinatorial_benders_fractional_separation_enabled);
        assert(sampled.combinatorial_fractional_validity_mode ==
               "weighted-fractional-path-activation-user-cut-convex-hull-valid");
    }

    auto implicit = make_instance(true);
    implicit.cell_weight_map = firebreak::core::LandscapeWeightMap();
    implicit.compact_cell_weights.clear();
    const auto explicit_unit = make_instance(true);
    const auto implicit_result = solve_branch(
        implicit,
        firebreak::risk::RiskMeasureConfig(),
        true,
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
        0.5);
    const auto explicit_result = solve_branch(
        explicit_unit,
        firebreak::risk::RiskMeasureConfig(),
        true,
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
        0.5);
    assert(implicit_result.status == "Optimal");
    assert(explicit_result.status == "Optimal");
    assert_close(implicit_result.objective_value, explicit_result.objective_value);
    assert(implicit_result.combinatorial_realized_sample_size ==
           explicit_result.combinatorial_realized_sample_size);
}

}  // namespace

int main() {
#ifndef FIREBREAK_WITH_CPLEX
    std::cout << "Skipping weighted FPP combinatorial sampling CPLEX tests because CPLEX is not enabled.\n";
    return 0;
#else
    test_expected_order_ratio_lift_matrix();
    test_cvar_mean_cvar_initial_fractional_and_homogeneous();
    std::cout << "All weighted FPP combinatorial sampling CPLEX tests passed.\n";
    return 0;
#endif
}
