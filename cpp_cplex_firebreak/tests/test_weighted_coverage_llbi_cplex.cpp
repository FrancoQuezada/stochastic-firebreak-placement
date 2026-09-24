#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppBendersSolver.hpp"
#include "benders/FppBranchBendersSolver.hpp"
#include "benders/FppStrengthening.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/FppSaaCplexModel.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_instance(bool homogeneous = false) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_coverage_llbi_cplex";
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
        homogeneous ? 621 : 622,
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

firebreak::solver::ModelResult solve_callback(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config,
    bool coverage,
    bool standard_llbi,
    bool root_cuts) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    options.use_lifted_lower_bounds = standard_llbi;
    options.use_root_user_cuts = root_cuts;
    options.root_user_cut_max_rounds = 2;
    options.root_user_cut_tolerance = 1.0e-7;
    options.strengthening_options.use_coverage_llbi = coverage;
    return solver.solve(opt, options);
}

firebreak::solver::ModelResult solve_explicit(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config,
    bool coverage) {
    firebreak::benders::FppBendersSolver solver;
    firebreak::benders::FppBendersOptions options;
    options.max_iterations = 20;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    options.strengthening_options.use_coverage_llbi = coverage;
    return solver.solve(opt, options);
}

void assert_coverage_diagnostics(const firebreak::solver::ModelResult& result) {
    assert(result.coverage_llbi_enabled);
    assert(result.coverage_llbi_weighted);
    assert(!result.coverage_llbi_weight_map_hash.empty());
    assert(result.coverage_llbi_scenarios_precomputed > 0);
    assert(result.coverage_llbi_baseline_cells > 0);
    assert(result.coverage_llbi_auxiliary_variables == result.coverage_llbi_num_zeta_vars);
    assert(result.coverage_llbi_linking_constraints > 0);
    assert(result.coverage_llbi_loss_constraints > 0);
    assert(result.coverage_llbi_total_incidence_terms > 0);
    assert(result.coverage_llbi_validity_mode ==
           "per-cell-capped-downstream-coverage-bound");
}

void compare_callback_variants(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config) {
    const auto direct = solve_direct(opt, risk_config);
    const auto baseline = solve_callback(opt, risk_config, false, false, false);
    const auto coverage = solve_callback(opt, risk_config, true, false, false);
    const auto both = solve_callback(opt, risk_config, true, true, false);
    const auto root = solve_callback(opt, risk_config, true, false, true);
    const auto explicit_coverage = solve_explicit(opt, risk_config, true);

    assert(baseline.status == "Optimal");
    assert(coverage.status == "Optimal");
    assert(both.status == "Optimal");
    assert(root.status == "Optimal");
    assert(explicit_coverage.status == "Optimal");
    assert_close(baseline.objective_value, direct.objective_value);
    assert_close(coverage.objective_value, direct.objective_value);
    assert_close(both.objective_value, direct.objective_value);
    assert_close(root.objective_value, direct.objective_value);
    assert_close(explicit_coverage.objective_value, direct.objective_value);
    assert_coverage_diagnostics(coverage);
    assert_coverage_diagnostics(both);
    assert_coverage_diagnostics(root);
    assert_coverage_diagnostics(explicit_coverage);
}

void test_expected_cvar_mean_cvar_equivalence() {
    const auto opt = make_weighted_instance(false);
    compare_callback_variants(opt, firebreak::risk::RiskMeasureConfig());

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    compare_callback_variants(opt, cvar);

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.4;
    compare_callback_variants(opt, mean_cvar);
}

void test_homogeneous_regression() {
    auto implicit = make_weighted_instance(true);
    implicit.cell_weight_map = firebreak::core::LandscapeWeightMap();
    implicit.compact_cell_weights.clear();
    const auto explicit_unit = make_weighted_instance(true);

    const auto implicit_result =
        solve_callback(implicit, firebreak::risk::RiskMeasureConfig(), true, false, false);
    const auto explicit_result =
        solve_callback(explicit_unit, firebreak::risk::RiskMeasureConfig(), true, false, false);
    assert_close(implicit_result.objective_value, explicit_result.objective_value);
    assert(implicit_result.coverage_llbi_num_zeta_vars ==
           explicit_result.coverage_llbi_num_zeta_vars);
    assert(implicit_result.coverage_llbi_num_constraints ==
           explicit_result.coverage_llbi_num_constraints);
    assert(!implicit_result.coverage_llbi_weighted);
    assert(!explicit_result.coverage_llbi_weighted);
}

}  // namespace

int main() {
#ifndef FIREBREAK_WITH_CPLEX
    std::cout << "Skipping weighted CoverageLLBI CPLEX tests because CPLEX is not enabled.\n";
    return 0;
#else
    test_expected_cvar_mean_cvar_equivalence();
    test_homogeneous_regression();
    std::cout << "All weighted CoverageLLBI CPLEX tests passed.\n";
    return 0;
#endif
}
