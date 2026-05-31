#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "benders/FppStrengthening.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/FppSaaCplexModel.hpp"

namespace {

firebreak::opt::OptimizationInstance make_structure_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});

    opt.eligible_indices = {1};
    opt.eligible_original_nodes = {2};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.message_filename = "synthetic.csv";
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

void test_variable_and_constraint_counts() {
    const auto opt = make_structure_instance();
    const auto structure = firebreak::solver::analyze_fpp_saa_model_structure(opt);

    assert(structure.x_variable_count == 3);
    assert(structure.y_variable_count == 1);
    assert(structure.total_variable_count == 4);
    assert(structure.budget_constraint_count == 1);
    assert(structure.ignition_constraint_count == 1);
    assert(structure.propagation_constraint_count == 2);
    assert(structure.total_constraint_count == 4);
}

void test_cvar_variable_and_constraint_counts() {
    const auto opt = make_structure_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::CVaR;
    config.cvarBeta = 0.9;
    const auto structure = firebreak::solver::analyze_fpp_saa_model_structure(opt, config);

    assert(structure.x_variable_count == 3);
    assert(structure.y_variable_count == 1);
    assert(structure.risk_threshold_variable_count == 1);
    assert(structure.cvar_excess_variable_count == 1);
    assert(structure.total_variable_count == 6);
    assert(structure.budget_constraint_count == 1);
    assert(structure.ignition_constraint_count == 1);
    assert(structure.propagation_constraint_count == 2);
    assert(structure.cvar_excess_constraint_count == 1);
    assert(structure.total_constraint_count == 5);
}

void test_mean_cvar_variable_and_constraint_counts() {
    const auto opt = make_structure_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    config.cvarBeta = 0.75;
    config.cvarLambda = 0.5;
    const auto structure = firebreak::solver::analyze_fpp_saa_model_structure(opt, config);

    assert(structure.risk_threshold_variable_count == 1);
    assert(structure.cvar_excess_variable_count == 1);
    assert(structure.cvar_excess_constraint_count == 1);
    assert(structure.total_variable_count == 6);
    assert(structure.total_constraint_count == 5);
}

void test_eligible_target_uses_y_variable() {
    const auto opt = make_structure_instance();
    const auto structure = firebreak::solver::analyze_fpp_saa_model_structure(opt);

    assert(structure.y_indices == std::vector<int>{1});
    assert(structure.has_y_for_node_index(1));
    assert(!structure.has_y_for_node_index(0));
    assert(!structure.has_y_for_node_index(2));

    const auto& first = structure.propagation_constraints[0];
    assert(first.u_index == 0);
    assert(first.v_index == 1);
    assert(first.target_has_firebreak_variable);
    assert(first.target_y_position == 0);
}

void test_noneligible_target_has_no_y_variable() {
    const auto opt = make_structure_instance();
    const auto structure = firebreak::solver::analyze_fpp_saa_model_structure(opt);

    const auto& second = structure.propagation_constraints[1];
    assert(second.u_index == 1);
    assert(second.v_index == 2);
    assert(!second.target_has_firebreak_variable);
    assert(second.target_y_position == -1);
}

#ifdef FIREBREAK_WITH_CPLEX
firebreak::opt::OptimizationInstance make_tiny_solve_instance() {
    auto opt = make_structure_instance();
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};
    return opt;
}

firebreak::opt::OptimizationInstance make_two_scenario_risk_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic-risk";
    opt.alpha = 2.0 / 3.0;
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
    first.message_filename = "synthetic_1.csv";
    first.observed_node_indices = {0, 1, 2};
    first.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    first.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 2;
    second.probability = 0.5;
    second.ignition_index = 0;
    second.ignition_original_node = 1;
    second.message_filename = "synthetic_2.csv";
    second.observed_node_indices = {0, 1, 2};
    second.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
    second.arcs.push_back(firebreak::opt::CompactArc{2, 1, 3, 2});

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.5, 0.5};
    opt.total_arcs = first.arcs.size() + second.arcs.size();
    return opt;
}

void test_optional_tiny_cplex_solve() {
    const auto opt = make_tiny_solve_instance();
    firebreak::solver::FppSaaCplexModel model;
    const auto result = model.solve(opt, 30.0, 0.0, 1, false);

    assert(std::fabs(result.objective_value - 1.0) < 1.0e-6);
    assert(std::find(
        result.selected_firebreak_original_nodes.begin(),
        result.selected_firebreak_original_nodes.end(),
        2) != result.selected_firebreak_original_nodes.end());
}

void test_optional_cvar_cplex_solve() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::solver::FppSaaCplexModel model;
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::CVaR;
    config.cvarBeta = 0.5;
    firebreak::benders::FppStrengtheningOptions strengthening;
    strengthening.use_coverage_llbi = true;
    strengthening.use_path_llbi = true;

    const auto result = model.solve(
        opt,
        30.0,
        0.0,
        1,
        false,
        nullptr,
        nullptr,
        nullptr,
        config,
        &strengthening);

    assert(result.status.find("Optimal") != std::string::npos);
    assert(result.risk_measure == "cvar");
    assert(result.coverage_llbi_enabled);
    assert(result.path_llbi_enabled);
    assert(result.coverage_llbi_num_zeta_vars == 0);
    assert(result.path_llbi_num_b_vars == 0);
    assert(std::fabs(result.objective_value - 2.0) < 1.0e-6);
    assert(std::fabs(result.expected_loss_component - 1.5) < 1.0e-6);
    assert(std::fabs(result.cvar_loss_component - 2.0) < 1.0e-6);
    assert(result.risk_threshold_value >= 1.0 - 1.0e-6);
    assert(result.risk_threshold_value <= 2.0 + 1.0e-6);
    assert(result.selected_firebreak_original_nodes.size() == 1);
}

void test_optional_mean_cvar_cplex_solve() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::solver::FppSaaCplexModel model;
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    config.cvarBeta = 0.5;
    config.cvarLambda = 0.5;
    firebreak::benders::FppStrengtheningOptions strengthening;
    strengthening.use_coverage_llbi = true;
    strengthening.use_path_llbi = true;

    const auto result = model.solve(
        opt,
        30.0,
        0.0,
        1,
        false,
        nullptr,
        nullptr,
        nullptr,
        config,
        &strengthening);

    assert(result.status.find("Optimal") != std::string::npos);
    assert(result.risk_measure == "mean-cvar");
    assert(result.coverage_llbi_enabled);
    assert(result.path_llbi_enabled);
    assert(result.coverage_llbi_num_zeta_vars == 0);
    assert(result.path_llbi_num_b_vars == 0);
    assert(std::fabs(result.objective_value - 1.75) < 1.0e-6);
    assert(std::fabs(result.expected_loss_component - 1.5) < 1.0e-6);
    assert(std::fabs(result.cvar_loss_component - 2.0) < 1.0e-6);
    assert(result.risk_threshold_value >= 1.0 - 1.0e-6);
    assert(result.risk_threshold_value <= 2.0 + 1.0e-6);
    assert(result.selected_firebreak_original_nodes.size() == 1);
}
#endif

}  // namespace

int main() {
    test_variable_and_constraint_counts();
    test_cvar_variable_and_constraint_counts();
    test_mean_cvar_variable_and_constraint_counts();
    test_eligible_target_uses_y_variable();
    test_noneligible_target_has_no_y_variable();
#ifdef FIREBREAK_WITH_CPLEX
    test_optional_tiny_cplex_solve();
    test_optional_cvar_cplex_solve();
    test_optional_mean_cvar_cplex_solve();
#endif
    std::cout << "All FPP-SAA model structure tests passed.\n";
    return 0;
}
