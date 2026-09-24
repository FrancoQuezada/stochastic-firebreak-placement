#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "core/LandscapeWeightMap.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/FppCutReachabilityCplexModel.hpp"
#include "solver/FppSaaCplexModel.hpp"
#include "solver/FppWeightedLossUtils.hpp"
#include "solver/WarmStart.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_branch_instance(bool weighted) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted-branch";
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
    scenario.message_filename = "weighted_branch.csv";
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

    const std::vector<firebreak::core::LandscapeWeightRecord> records = weighted
        ? std::vector<firebreak::core::LandscapeWeightRecord>{
              {1, 1.0, 1.0, 0},
              {2, 1.0, 1.0, 0},
              {3, 1.0, 1.0, 0},
              {4, 1.0, 1.0, 0},
              {5, 10.0, 10.0, 1},
          }
        : std::vector<firebreak::core::LandscapeWeightRecord>{
              {1, 1.0, 1.0, 0},
              {2, 1.0, 1.0, 0},
              {3, 1.0, 1.0, 0},
              {4, 1.0, 1.0, 0},
              {5, 1.0, 1.0, 0},
          };
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        weighted ? "heterogeneous" : "homogeneous",
        11,
        false,
        records);
    opt.compact_cell_weights = firebreak::core::build_compact_weight_vector(
        opt.cell_weight_map,
        opt.node_mapper);
    return opt;
}

#ifdef FIREBREAK_WITH_CPLEX
firebreak::opt::OptimizationInstance make_risk_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted-risk";
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
    first.message_filename = "risk_1.csv";
    first.observed_node_indices = {0, 1};
    first.arcs = {firebreak::opt::CompactArc{0, 1, 1, 2}};

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 2;
    second.probability = 0.5;
    second.ignition_index = 0;
    second.ignition_original_node = 1;
    second.message_filename = "risk_2.csv";
    second.observed_node_indices = {0, 2};
    second.arcs = {firebreak::opt::CompactArc{0, 2, 1, 3}};

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.5, 0.5};
    opt.total_arcs = 2;
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        12,
        false,
        {
            {1, 1.0, 1.0, 0},
            {2, 2.0, 2.0, 0},
            {3, 8.0, 8.0, 1},
        });
    opt.compact_cell_weights = firebreak::core::build_compact_weight_vector(
        opt.cell_weight_map,
        opt.node_mapper);
    return opt;
}
#endif

void test_structure_coefficients() {
    const auto opt = make_branch_instance(true);
    const auto base = firebreak::solver::analyze_fpp_saa_model_structure(opt);
    assert(base.total_variable_count == 7);
    assert(base.total_constraint_count == 6);
    assert(base.scenario_loss_coefficients.size() == 5);
    assert(base.objective_x_coefficients.size() == 5);
    assert_close(base.scenario_loss_coefficients[4].coefficient, 10.0);
    assert_close(base.objective_x_coefficients[4].coefficient, 10.0);

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    const auto base_cvar = firebreak::solver::analyze_fpp_saa_model_structure(opt, cvar);
    assert(base_cvar.cvar_loss_coefficients.size() == 5);
    assert_close(base_cvar.cvar_loss_coefficients[4].coefficient, 10.0);

    const auto cut = firebreak::solver::analyze_fpp_cut_reachability_model_structure(opt, cvar);
    assert(cut.x_variable_count == 5);
    assert(cut.q_variable_count == 5);
    assert(cut.scenario_loss_coefficients.size() == 5);
    assert(cut.objective_q_coefficients.size() == 5);
    assert_close(cut.scenario_loss_coefficients[4].coefficient, 10.0);
    for (const auto& coeff : cut.objective_q_coefficients) {
        assert_close(coeff.coefficient, 0.0);
    }
}

void test_missing_weight_rejected() {
    auto opt = make_branch_instance(true);
    opt.compact_cell_weights.pop_back();
    bool threw = false;
    try {
        (void)firebreak::solver::analyze_fpp_saa_model_structure(opt);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

#ifdef FIREBREAK_WITH_CPLEX
void test_weighted_expected_changes_solution() {
    const auto unit = make_branch_instance(false);
    const auto weighted = make_branch_instance(true);
    firebreak::solver::FppSaaCplexModel model;

    const auto unit_result = model.solve(unit, 30.0, 0.0, 1, false);
    const auto weighted_result = model.solve(weighted, 30.0, 0.0, 1, false);

    assert_close(unit_result.objective_value, 2.0);
    assert(std::find(
        unit_result.selected_firebreak_original_nodes.begin(),
        unit_result.selected_firebreak_original_nodes.end(),
        2) != unit_result.selected_firebreak_original_nodes.end());

    assert_close(weighted_result.objective_value, 4.0);
    assert(std::find(
        weighted_result.selected_firebreak_original_nodes.begin(),
        weighted_result.selected_firebreak_original_nodes.end(),
        5) != weighted_result.selected_firebreak_original_nodes.end());
}

void test_base_cut_weighted_equivalence_and_evaluator_validation() {
    const auto opt = make_branch_instance(true);
    firebreak::solver::FppSaaCplexModel base_model;
    firebreak::solver::FppCutReachabilityCplexModel cut_model;

    const auto base = base_model.solve(opt, 30.0, 0.0, 1, false);
    const auto cut = cut_model.solve(opt, 30.0, 0.0, 1, false);

    assert_close(base.objective_value, 4.0);
    assert_close(cut.objective_value, 4.0);
    assert_close(base.objective_value, cut.objective_value);
    assert(cut.objective_validation_passed);

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate(cut.selected_firebreak_indices, true);
    assert_close(recourse.expected_weighted_burn_loss, cut.objective_value);
}

void test_weighted_cvar_and_mean_cvar() {
    const auto opt = make_risk_instance();
    firebreak::solver::FppSaaCplexModel model;

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    const auto cvar_result = model.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, cvar);
    assert_close(cvar_result.objective_value, 3.0);
    assert_close(cvar_result.expected_loss_component, 2.0);
    assert_close(cvar_result.cvar_loss_component, 3.0);
    assert(std::find(
        cvar_result.selected_firebreak_original_nodes.begin(),
        cvar_result.selected_firebreak_original_nodes.end(),
        3) != cvar_result.selected_firebreak_original_nodes.end());

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.5;
    const auto mean_result =
        model.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, mean_cvar);
    assert_close(mean_result.objective_value, 2.5);
    assert_close(mean_result.expected_loss_component, 2.0);
    assert_close(mean_result.cvar_loss_component, 3.0);
}

void test_ignition_and_reached_firebreak_conventions() {
    auto opt = make_branch_instance(true);
    opt.cell_weight_map = firebreak::core::make_landscape_weight_map(
        "heterogeneous",
        13,
        false,
        {
            {1, 100.0, 100.0, 1},
            {2, 50.0, 50.0, 1},
            {3, 1.0, 1.0, 0},
            {4, 1.0, 1.0, 0},
            {5, 1.0, 1.0, 0},
        });
    opt.compact_cell_weights = firebreak::core::build_compact_weight_vector(
        opt.cell_weight_map,
        opt.node_mapper);

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate({1}, true);
    assert_close(recourse.expected_weighted_burn_loss, 101.0);
    assert(evaluator.isReached(0, 1));
    assert(!evaluator.isBurned(0, 1));
}

void test_weighted_cut_warm_start_validation() {
    const auto opt = make_branch_instance(true);
    firebreak::solver::WarmStart warm_start;
    warm_start.enabled = true;
    warm_start.source_path = "weighted-manual-start";
    warm_start.original_node_ids = {5};
    warm_start.compact_indices = {4};
    warm_start.status = "valid";

    const auto start_values =
        firebreak::solver::build_fpp_cut_reachability_mip_start_values(
            opt,
            warm_start.compact_indices);
    assert(start_values.feasible);
    assert_close(start_values.recourse_objective, 4.0);

    firebreak::solver::FppCutReachabilityCplexModel cut_model;
    const auto result = cut_model.solve(opt, 30.0, 0.0, 1, false, &warm_start);
    assert(result.warm_start_used);
    assert(result.mip_start_accepted);
    assert(result.objective_validation_passed);
    assert_close(result.objective_value, 4.0);
}
#endif

}  // namespace

int main() {
    test_structure_coefficients();
    test_missing_weight_rejected();
#ifdef FIREBREAK_WITH_CPLEX
    test_weighted_expected_changes_solution();
    test_base_cut_weighted_equivalence_and_evaluator_validation();
    test_weighted_cvar_and_mean_cvar();
    test_ignition_and_reached_firebreak_conventions();
    test_weighted_cut_warm_start_validation();
#endif
    std::cout << "All weighted FPP-SAA model tests passed.\n";
    return 0;
}
