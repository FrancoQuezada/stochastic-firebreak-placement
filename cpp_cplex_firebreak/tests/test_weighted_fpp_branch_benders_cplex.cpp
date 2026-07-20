#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "benders/FppBendersSolver.hpp"
#include "benders/FppBranchBendersSolver.hpp"
#include "benders/FppPersistentScenarioSubproblemManager.hpp"
#include "benders/FppScenarioSubproblem.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/FppSaaCplexModel.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_branch_instance(bool homogeneous = false) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_branch_benders_branch";
    opt.alpha = 0.2;
    opt.n_cells = 5;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 4};
    opt.eligible_original_nodes = {2, 5};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
        firebreak::opt::CompactArc{0, 4, 1, 5},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
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
              {2, 1.0, 1.0, 0},
              {3, 1.0, 1.0, 0},
              {4, 1.0, 1.0, 0},
              {5, 10.0, 10.0, 1},
          };
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        homogeneous ? "homogeneous" : "heterogeneous",
        71,
        false,
        records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

firebreak::opt::OptimizationInstance make_weighted_risk_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_branch_benders_risk";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {1, 2};
    opt.eligible_original_nodes = {2, 3};

    firebreak::opt::OptimizationScenario first;
    first.scenario_id = 1;
    first.probability = 0.5;
    first.ignition_index = 0;
    first.ignition_original_node = 1;
    first.observed_node_indices = {0, 1};
    first.arcs = {firebreak::opt::CompactArc{0, 1, 1, 2}};

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 2;
    second.probability = 0.5;
    second.ignition_index = 0;
    second.ignition_original_node = 1;
    second.observed_node_indices = {0, 2};
    second.arcs = {firebreak::opt::CompactArc{0, 2, 1, 3}};

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.5, 0.5};
    opt.total_arcs = 2;
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        72,
        false,
        {
            {1, 1.0, 1.0, 0},
            {2, 2.0, 2.0, 0},
            {3, 8.0, 8.0, 1},
        });
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

std::vector<double> compact_y_from_eligible(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<int>& eligible_y) {
    std::vector<double> compact_y(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < eligible_y.size(); ++pos) {
        compact_y[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            static_cast<double>(eligible_y[pos]);
    }
    return compact_y;
}

double exact_weighted_recourse(
    const firebreak::opt::OptimizationInstance& opt,
    const std::vector<int>& eligible_y) {
    std::vector<int> selected;
    for (std::size_t pos = 0; pos < eligible_y.size(); ++pos) {
        if (eligible_y[pos]) {
            selected.push_back(opt.eligible_indices[pos]);
        }
    }
    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    return evaluator.evaluate(selected, false).expected_weighted_burn_loss;
}

firebreak::solver::ModelResult solve_callback(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config,
    bool use_root_user_cuts,
    bool use_lifted_lower_bounds = false) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    options.use_root_user_cuts = use_root_user_cuts;
    options.use_lifted_lower_bounds = use_lifted_lower_bounds;
    options.root_user_cut_max_rounds = 2;
    options.root_user_cut_tolerance = 1.0e-7;
    return solver.solve(opt, options);
}

void compare_direct_explicit_callback(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config,
    bool use_root_user_cuts) {
    firebreak::solver::FppSaaCplexModel direct;
    const auto direct_result =
        direct.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, risk_config);

    firebreak::benders::FppBendersSolver explicit_benders;
    firebreak::benders::FppBendersOptions benders_options;
    benders_options.max_iterations = 20;
    benders_options.tolerance = 1.0e-7;
    benders_options.time_limit_seconds = 30.0;
    benders_options.mip_gap = 0.0;
    benders_options.threads = 1;
    benders_options.risk_config = risk_config;
    const auto explicit_result = explicit_benders.solve(opt, benders_options);

    const auto callback_result = solve_callback(opt, risk_config, use_root_user_cuts);

    assert(callback_result.status == "Optimal");
    assert_close(callback_result.objective_value, direct_result.objective_value);
    assert_close(callback_result.objective_value, explicit_result.objective_value);
    assert_close(callback_result.expected_loss_component, direct_result.expected_loss_component);
    assert_close(callback_result.expected_loss_component, explicit_result.expected_loss_component);
    if (risk_config.type != firebreak::risk::RiskMeasureType::Expected) {
        assert_close(callback_result.cvar_loss_component, direct_result.cvar_loss_component);
        assert_close(callback_result.cvar_loss_component, explicit_result.cvar_loss_component);
    }
    assert(callback_result.branch_benders_enabled);
    assert(callback_result.branch_benders_subproblems_solved > 0);
    assert(callback_result.branch_benders_max_cut_violation <= 1.0e-6);
    assert(callback_result.objective_metric.find("weighted_") == 0);

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate(
        callback_result.selected_firebreak_indices,
        false,
        risk_config.cvarBeta);
    double eval_objective = recourse.expected_weighted_burn_loss;
    if (risk_config.type == firebreak::risk::RiskMeasureType::CVaR) {
        eval_objective = recourse.weighted_loss_statistics.cvar;
    } else if (risk_config.type == firebreak::risk::RiskMeasureType::MeanCVaR) {
        eval_objective =
            (1.0 - risk_config.cvarLambda) * recourse.expected_weighted_burn_loss +
            risk_config.cvarLambda * recourse.weighted_loss_statistics.cvar;
    }
    assert_close(callback_result.objective_value, eval_objective);
}

void test_persistent_weighted_subproblem_reuse() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppPersistentScenarioSubproblemManager manager(opt, false);
    firebreak::benders::FppScenarioSubproblem fresh;

    const std::vector<std::vector<int>> ybars = {{0, 0}, {1, 0}, {0, 1}};
    for (const auto& ybar : ybars) {
        const auto persistent = manager.solveScenario(0, ybar);
        const auto one_shot = fresh.solve(opt, 0, ybar, false);
        const double exact = exact_weighted_recourse(opt, ybar);
        assert_close(persistent.objective_value, one_shot.objective_value);
        assert_close(persistent.objective_value, exact);
        assert_close(
            persistent.benders_cut.evaluateAt(compact_y_from_eligible(opt, ybar)),
            persistent.objective_value);
    }

    const auto diagnostics = manager.diagnostics();
    assert(diagnostics.persistent_subproblems_enabled);
    assert(diagnostics.subproblem_model_build_count == 1);
    assert(diagnostics.subproblem_solve_count == 3);
    assert(diagnostics.weight_map_hash == opt.cell_weight_map.deterministic_hash);
}

void test_integer_cut_validity_by_enumeration() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppPersistentScenarioSubproblemManager manager(opt, false);
    const auto result = manager.solveScenario(0, {0, 1});
    assert_close(result.benders_cut.evaluateAt(compact_y_from_eligible(opt, {0, 1})), 4.0);

    for (int y0 = 0; y0 <= 1; ++y0) {
        for (int y1 = 0; y1 <= 1; ++y1) {
            if (y0 + y1 > opt.budget) {
                continue;
            }
            const std::vector<int> ybar = {y0, y1};
            const double cut_rhs =
                result.benders_cut.evaluateAt(compact_y_from_eligible(opt, ybar));
            const double exact = exact_weighted_recourse(opt, ybar);
            assert(cut_rhs <= exact + 1.0e-6);
        }
    }
}

void test_root_fractional_cut_validity() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppPersistentScenarioSubproblemManager manager(opt, false);
    const std::vector<double> fractional = {0.5, 0.0};
    const auto result = manager.solveScenarioFractional(0, fractional);
    std::vector<double> compact_fractional(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < fractional.size(); ++pos) {
        compact_fractional[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            fractional[pos];
    }
    assert_close(result.benders_cut.evaluateAt(compact_fractional), result.objective_value);

    for (int y0 = 0; y0 <= 1; ++y0) {
        for (int y1 = 0; y1 <= 1; ++y1) {
            if (y0 + y1 > opt.budget) {
                continue;
            }
            const std::vector<int> ybar = {y0, y1};
            const double cut_rhs =
                result.benders_cut.evaluateAt(compact_y_from_eligible(opt, ybar));
            const double exact = exact_weighted_recourse(opt, ybar);
            assert(cut_rhs <= exact + 1.0e-6);
        }
    }
}

void test_homogeneous_regression() {
    auto implicit = make_weighted_branch_instance(true);
    implicit.cell_weight_map = firebreak::core::LandscapeWeightMap();
    implicit.compact_cell_weights.clear();
    const auto explicit_homogeneous = make_weighted_branch_instance(true);

    firebreak::benders::FppPersistentScenarioSubproblemManager implicit_manager(implicit, false);
    firebreak::benders::FppPersistentScenarioSubproblemManager explicit_manager(explicit_homogeneous, false);
    const auto a = implicit_manager.solveScenario(0, {0, 1});
    const auto b = explicit_manager.solveScenario(0, {0, 1});
    assert_close(a.objective_value, b.objective_value);
    assert_close(a.benders_cut.rhs_constant, b.benders_cut.rhs_constant);
    assert(a.benders_cut.coefficients_by_compact_index.size() ==
           b.benders_cut.coefficients_by_compact_index.size());
    for (std::size_t i = 0; i < a.benders_cut.coefficients_by_compact_index.size(); ++i) {
        assert(a.benders_cut.coefficients_by_compact_index[i].first ==
               b.benders_cut.coefficients_by_compact_index[i].first);
        assert_close(
            a.benders_cut.coefficients_by_compact_index[i].second,
            b.benders_cut.coefficients_by_compact_index[i].second);
    }

    compare_direct_explicit_callback(implicit, firebreak::risk::RiskMeasureConfig(), false);
    compare_direct_explicit_callback(explicit_homogeneous, firebreak::risk::RiskMeasureConfig(), false);
}

void test_expected_cvar_mean_cvar_callback_equivalence() {
    compare_direct_explicit_callback(
        make_weighted_branch_instance(false),
        firebreak::risk::RiskMeasureConfig(),
        false);
    compare_direct_explicit_callback(
        make_weighted_branch_instance(false),
        firebreak::risk::RiskMeasureConfig(),
        true);

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    compare_direct_explicit_callback(make_weighted_risk_instance(), cvar, false);
    compare_direct_explicit_callback(make_weighted_risk_instance(), cvar, true);

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.5;
    compare_direct_explicit_callback(make_weighted_risk_instance(), mean_cvar, false);
    compare_direct_explicit_callback(make_weighted_risk_instance(), mean_cvar, true);
}

void test_weighted_standard_llbi_cplex() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::solver::FppSaaCplexModel direct;
    const auto direct_result = direct.solve(
        opt,
        30.0,
        0.0,
        1,
        false);
    const auto llbi_result =
        solve_callback(opt, firebreak::risk::RiskMeasureConfig(), false, true);
    assert(llbi_result.status == "Optimal");
    assert_close(llbi_result.objective_value, direct_result.objective_value);
    assert(llbi_result.benders_use_lifted_lower_bounds);
    assert(llbi_result.benders_lifted_lower_bound_weighted);
    assert(llbi_result.benders_lifted_lower_bound_count ==
           static_cast<int>(opt.scenarios.size()));
    assert(llbi_result.benders_lifted_lower_bound_constraints_added ==
           llbi_result.benders_lifted_lower_bound_count);
    assert(llbi_result.benders_lifted_lower_bound_scenarios_precomputed ==
           static_cast<int>(opt.scenarios.size()));
    assert(llbi_result.benders_lifted_lower_bound_singletons_evaluated ==
           static_cast<int>(opt.scenarios.size() * opt.eligible_indices.size()));
    assert(llbi_result.benders_lifted_lower_bound_validity_mode ==
           "downstream-union-bound");
    assert(!llbi_result.benders_lifted_lower_bound_weight_map_hash.empty());
}

}  // namespace

int main() {
#ifndef FIREBREAK_WITH_CPLEX
    std::cout << "Skipping weighted FPP Branch-Benders callback tests because CPLEX is not enabled.\n";
    return 0;
#else
    test_persistent_weighted_subproblem_reuse();
    test_integer_cut_validity_by_enumeration();
    test_root_fractional_cut_validity();
    test_homogeneous_regression();
    test_expected_cvar_mean_cvar_callback_equivalence();
    test_weighted_standard_llbi_cplex();
    std::cout << "All weighted FPP Branch-Benders callback tests passed.\n";
    return 0;
#endif
}
