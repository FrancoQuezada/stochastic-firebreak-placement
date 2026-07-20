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

firebreak::opt::OptimizationInstance make_weighted_instance(bool homogeneous = false) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_projected_coverage_llbi_cplex";
    opt.alpha = 0.2;
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
        firebreak::opt::CompactArc{0, 4, 1, 5},
    };

    firebreak::opt::OptimizationScenario second = first;
    second.scenario_id = 2;
    second.probability = 0.5;
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
              {1, 1.0, 1.0, 0},
              {2, 2.0, 2.0, 0},
              {3, 5.0, 5.0, 0},
              {4, 30.0, 30.0, 1},
              {5, 7.0, 7.0, 0},
          };
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        homogeneous ? "homogeneous" : "heterogeneous",
        homogeneous ? 63231 : 63232,
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

firebreak::solver::ModelResult solve_projected(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config,
    bool exp,
    bool root_cuts,
    bool dominance) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    options.use_root_user_cuts = root_cuts;
    options.root_user_cut_max_rounds = 2;
    options.root_user_cut_tolerance = 1.0e-7;
    options.strengthening_options.use_global_dominance_preprocessing = dominance;
    options.strengthening_options.projected_llbi_root_rounds = 3;
    options.strengthening_options.projected_llbi_max_cuts_per_round = 20;
    if (exp) {
        options.strengthening_options.use_projected_coverage_llbi_exp = true;
    } else {
        options.strengthening_options.use_projected_coverage_llbi_poly = true;
    }
    return solver.solve(opt, options);
}

void assert_projected_coverage_diagnostics(
    const firebreak::solver::ModelResult& result,
    bool exp,
    bool weighted) {
    assert(result.projected_coverage_llbi_enabled);
    assert(!result.projected_path_llbi_enabled);
    assert(result.projected_llbi_family == "coverage");
    assert(result.projected_llbi_strategy == (exp ? "exp" : "poly"));
    assert(result.projected_coverage_llbi_weighted == weighted);
    assert(!result.projected_coverage_llbi_mode.empty());
    assert(!result.projected_coverage_llbi_weight_map_hash.empty());
    assert(result.projected_coverage_llbi_scenarios_precomputed > 0);
    assert(result.projected_coverage_llbi_baseline_cells > 0);
    assert(result.projected_coverage_llbi_nonempty_coverage_sets > 0);
    assert(result.projected_coverage_llbi_total_incidence_terms > 0);
    assert(result.projected_coverage_llbi_cuts_generated >=
           result.projected_coverage_llbi_cuts_added);
    assert(result.projected_coverage_llbi_validity_mode.find("coverage-projection") !=
           std::string::npos);
}

void compare_projected_variants(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config) {
    const auto direct = solve_direct(opt, risk_config);
    const auto exp = solve_projected(opt, risk_config, true, false, false);
    const auto poly = solve_projected(opt, risk_config, false, false, false);
    const auto exp_root = solve_projected(opt, risk_config, true, true, false);
    const auto exp_dominance = solve_projected(opt, risk_config, true, false, true);

    assert(exp.status == "Optimal");
    assert(poly.status == "Optimal");
    assert(exp_root.status == "Optimal");
    assert(exp_dominance.status == "Optimal");
    assert_close(exp.objective_value, direct.objective_value);
    assert_close(poly.objective_value, direct.objective_value);
    assert_close(exp_root.objective_value, direct.objective_value);
    assert_close(exp_dominance.objective_value, direct.objective_value);
    assert_projected_coverage_diagnostics(exp, true, true);
    assert_projected_coverage_diagnostics(poly, false, true);
    assert_projected_coverage_diagnostics(exp_root, true, true);
    assert_projected_coverage_diagnostics(exp_dominance, true, true);
}

void test_expected_cvar_mean_cvar_equivalence() {
    const auto opt = make_weighted_instance(false);
    compare_projected_variants(opt, firebreak::risk::RiskMeasureConfig());

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    compare_projected_variants(opt, cvar);

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.4;
    compare_projected_variants(opt, mean_cvar);
}

void test_homogeneous_regression() {
    auto implicit = make_weighted_instance(true);
    implicit.cell_weight_map = firebreak::core::LandscapeWeightMap();
    implicit.compact_cell_weights.clear();
    const auto explicit_unit = make_weighted_instance(true);

    const auto implicit_result =
        solve_projected(implicit, firebreak::risk::RiskMeasureConfig(), true, false, false);
    const auto explicit_result =
        solve_projected(explicit_unit, firebreak::risk::RiskMeasureConfig(), true, false, false);
    assert_close(implicit_result.objective_value, explicit_result.objective_value);
    assert_projected_coverage_diagnostics(implicit_result, true, false);
    assert_projected_coverage_diagnostics(explicit_result, true, false);
}

void test_weighted_projected_path_still_rejected() {
    const auto opt = make_weighted_instance(false);
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.time_limit_seconds = 30.0;
    options.threads = 1;
    options.strengthening_options.use_projected_path_llbi_exp = true;
    bool threw = false;
    try {
        (void)solver.solve(opt, options);
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("projected PathLLBI") != std::string::npos;
    }
    assert(threw);
}

}  // namespace

int main() {
#ifndef FIREBREAK_WITH_CPLEX
    std::cout << "Skipping weighted projected CoverageLLBI CPLEX tests because CPLEX is not enabled.\n";
    return 0;
#else
    test_expected_cvar_mean_cvar_equivalence();
    test_homogeneous_regression();
    test_weighted_projected_path_still_rejected();
    std::cout << "All weighted projected CoverageLLBI CPLEX tests passed.\n";
    return 0;
#endif
}
