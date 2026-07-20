#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
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
    opt.landscape_name = "weighted_combinatorial_cplex";
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
        homogeneous ? 641 : 642,
        false,
        records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

firebreak::benders::FppCombinatorialBendersOptions baseline_options() {
    firebreak::benders::FppCombinatorialBendersOptions options;
    options.enabled = true;
    options.lift_mode = firebreak::benders::FppCombinatorialBendersLiftMode::None;
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
    bool combinatorial) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    if (combinatorial) {
        options.combinatorial_options = baseline_options();
    }
    return solver.solve(opt, options);
}

void compare_expected_cvar_mean_cvar() {
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

    for (const auto& config : configs) {
        const auto direct = solve_direct(opt, config);
        const auto lp_callback = solve_branch(opt, config, false);
        const auto combinatorial = solve_branch(opt, config, true);
        assert(direct.status == "Optimal");
        assert(lp_callback.status == "Optimal");
        assert(combinatorial.status == "Optimal");
        assert_close(combinatorial.objective_value, direct.objective_value);
        assert_close(combinatorial.objective_value, lp_callback.objective_value);
        assert_close(combinatorial.expected_loss_component, direct.expected_loss_component);
        if (config.type != firebreak::risk::RiskMeasureType::Expected) {
            assert_close(combinatorial.cvar_loss_component, direct.cvar_loss_component);
        }
        assert(combinatorial.combinatorial_benders_enabled);
        assert(combinatorial.combinatorial_benders_weighted);
        assert(combinatorial.combinatorial_benders_mode == "baseline-integer-exact-no-lifting");
        assert(combinatorial.combinatorial_benders_fractional_separation_enabled == false);
        assert(combinatorial.combinatorial_benders_initial_cuts_enabled == false);
        assert(combinatorial.combinatorial_benders_lifting_enabled == false);
        assert(combinatorial.combinatorial_benders_scenario_sampling_enabled == false);
        assert(combinatorial.combinatorial_benders_cut_sampling_ratio == 1.0);
        assert(combinatorial.combinatorial_benders_weighted_recourse_evaluations > 0);
        assert(combinatorial.combinatorial_benders_cuts_tight_at_incumbent > 0);
        assert(combinatorial.combinatorial_benders_max_tightness_error <= 1.0e-6);
        assert(combinatorial.branch_benders_max_cut_violation <= 1.0e-6);
    }
}

void test_homogeneous_regression() {
    auto implicit = make_instance(true);
    implicit.cell_weight_map = firebreak::core::LandscapeWeightMap();
    implicit.compact_cell_weights.clear();
    const auto explicit_unit = make_instance(true);
    const auto implicit_result =
        solve_branch(implicit, firebreak::risk::RiskMeasureConfig(), true);
    const auto explicit_result =
        solve_branch(explicit_unit, firebreak::risk::RiskMeasureConfig(), true);
    assert(implicit_result.status == "Optimal");
    assert(explicit_result.status == "Optimal");
    assert_close(implicit_result.objective_value, explicit_result.objective_value);
    assert(!implicit_result.combinatorial_benders_weighted);
    assert(!explicit_result.combinatorial_benders_weighted);
}

void test_phase6c1_option_rejection() {
    const auto opt = make_instance(false);
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.combinatorial_options = baseline_options();
    options.combinatorial_options.cut_sampling_ratio = 0.5;
    bool threw = false;
    try {
        (void)solver.solve(opt, options);
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("Phase 6C1") != std::string::npos;
    }
    assert(threw);
}

}  // namespace

int main() {
#ifndef FIREBREAK_WITH_CPLEX
    std::cout << "Skipping weighted FPP combinatorial CPLEX tests because CPLEX is not enabled.\n";
    return 0;
#else
    compare_expected_cvar_mean_cvar();
    test_homogeneous_regression();
    test_phase6c1_option_rejection();
    std::cout << "All weighted FPP combinatorial CPLEX tests passed.\n";
    return 0;
#endif
}
