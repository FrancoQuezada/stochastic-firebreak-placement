#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "benders/FppBendersSolver.hpp"
#include "benders/FppScenarioSubproblem.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/FppSaaCplexModel.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_weighted_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_benders_path";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {0, 1};
    opt.eligible_original_nodes = {1, 2};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs = {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        51,
        false,
        {
            {1, 5.0, 5.0, 1},
            {2, 7.0, 7.0, 1},
            {3, 11.0, 11.0, 0},
        });
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

firebreak::opt::OptimizationInstance make_weighted_branch_instance(bool homogeneous = false) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_benders_branch";
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
        52,
        false,
        records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

firebreak::opt::OptimizationInstance make_weighted_risk_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_benders_risk";
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
        53,
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

void compare_direct_and_benders(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config) {
    firebreak::solver::FppSaaCplexModel direct;
    const auto direct_result =
        direct.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, risk_config);

    firebreak::benders::FppBendersSolver benders;
    firebreak::benders::FppBendersOptions options;
    options.max_iterations = 20;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    const auto benders_result = benders.solve(opt, options);

    assert(benders_result.status == "Optimal");
    assert_close(benders_result.objective_value, direct_result.objective_value);
    assert_close(benders_result.expected_loss_component, direct_result.expected_loss_component);
    if (risk_config.type != firebreak::risk::RiskMeasureType::Expected) {
        assert_close(benders_result.cvar_loss_component, direct_result.cvar_loss_component);
    }
    assert(benders_result.benders_final_max_cut_violation <= 1.0e-6);
}

void test_weighted_subproblem_objective_and_cut_tightness() {
    const auto opt = make_weighted_path_instance();
    const auto structure = firebreak::benders::analyze_fpp_scenario_subproblem_structure(opt, 0);
    assert(structure.objective_x_coefficients.size() == 3);
    assert_close(structure.objective_x_coefficients[0].coefficient, 5.0);
    assert_close(structure.objective_x_coefficients[2].coefficient, 11.0);

    firebreak::benders::FppScenarioSubproblem subproblem;
    const auto reached_firebreak = subproblem.solve(opt, 0, {0, 1}, false);
    assert_close(reached_firebreak.objective_value, 5.0);
    assert_close(
        reached_firebreak.benders_cut.evaluateAt(compact_y_from_eligible(opt, {0, 1})),
        5.0);

    const auto ignition_selected = subproblem.solve(opt, 0, {1, 0}, false);
    assert_close(ignition_selected.objective_value, 23.0);

    const auto noneligible_burns = subproblem.solve(opt, 0, {0, 0}, false);
    assert_close(noneligible_burns.objective_value, 23.0);
}

void test_weighted_cut_validity_by_enumeration() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppScenarioSubproblem subproblem;
    const auto result = subproblem.solve(opt, 0, {0, 1}, false);
    assert_close(result.benders_cut.evaluateAt(compact_y_from_eligible(opt, {0, 1})), 4.0);

    for (int y0 = 0; y0 <= 1; ++y0) {
        for (int y1 = 0; y1 <= 1; ++y1) {
            if (y0 + y1 > opt.budget) {
                continue;
            }
            const std::vector<int> eligible_y = {y0, y1};
            const double cut_rhs =
                result.benders_cut.evaluateAt(compact_y_from_eligible(opt, eligible_y));
            const double exact = exact_weighted_recourse(opt, eligible_y);
            assert(cut_rhs <= exact + 1.0e-6);
        }
    }
}

void test_homogeneous_cut_regression() {
    const auto implicit = make_weighted_branch_instance(true);
    auto explicit_homogeneous = implicit;
    explicit_homogeneous.cell_weight_map =
        firebreak::core::make_homogeneous_weight_map(explicit_homogeneous.node_mapper.original_nodes());
    explicit_homogeneous.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(
            explicit_homogeneous.cell_weight_map,
            explicit_homogeneous.node_mapper);

    firebreak::benders::FppScenarioSubproblem subproblem;
    const auto a = subproblem.solve(implicit, 0, {0, 1}, false);
    const auto b = subproblem.solve(explicit_homogeneous, 0, {0, 1}, false);
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
}

void test_expected_cvar_mean_cvar_equivalence() {
    compare_direct_and_benders(make_weighted_branch_instance(false), firebreak::risk::RiskMeasureConfig());

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    compare_direct_and_benders(make_weighted_risk_instance(), cvar);

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.5;
    compare_direct_and_benders(make_weighted_risk_instance(), mean_cvar);
}

void test_iteration_limited_incumbent_is_weighted() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppBendersSolver benders;
    firebreak::benders::FppBendersOptions options;
    options.max_iterations = 1;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    const auto result = benders.solve(opt, options);

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate(result.selected_firebreak_indices, false);
    assert_close(result.objective_value, recourse.expected_weighted_burn_loss);
    assert(result.best_bound <= result.objective_value + 1.0e-6);
}

void test_missing_weight_rejected() {
    auto opt = make_weighted_branch_instance(false);
    opt.compact_cell_weights.pop_back();
    firebreak::benders::FppScenarioSubproblem subproblem;
    bool threw = false;
    try {
        (void)subproblem.solve(opt, 0, {0, 1}, false);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main() {
#ifndef FIREBREAK_WITH_CPLEX
    std::cout << "Skipping weighted FPP Benders CPLEX tests because CPLEX is not enabled.\n";
    return 0;
#else
    test_weighted_subproblem_objective_and_cut_tightness();
    test_weighted_cut_validity_by_enumeration();
    test_homogeneous_cut_regression();
    test_expected_cvar_mean_cvar_equivalence();
    test_iteration_limited_incumbent_is_weighted();
    test_missing_weight_rejected();
    std::cout << "All weighted FPP Benders CPLEX tests passed.\n";
    return 0;
#endif
}
