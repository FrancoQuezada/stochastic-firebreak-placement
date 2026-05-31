#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/FppLiftedLowerBound.hpp"
#include "benders/FppScenarioSubproblem.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include "benders/FppBendersSolver.hpp"
#include "benders/FppBendersMaster.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/FppSaaCplexModel.hpp"
#endif

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-6);
}

firebreak::opt::OptimizationInstance make_path_instance(int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_path";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = budget;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    opt.total_arcs = scenario.arcs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_dag_alternate_path_instance(int budget) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_dag_alternate";
    opt.alpha = 0.5;
    opt.n_cells = 4;
    opt.budget = budget;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {0, 1, 2, 3};
    opt.eligible_original_nodes = {1, 2, 3, 4};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 3, 2, 4});
    scenario.arcs.push_back(firebreak::opt::CompactArc{2, 3, 3, 4});

    opt.total_arcs = scenario.arcs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

#ifdef FIREBREAK_WITH_CPLEX
firebreak::opt::OptimizationInstance make_alternative_optima_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_branch";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});

    opt.total_arcs = scenario.arcs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_two_scenario_risk_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_fpp_benders_risk";
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
    first.observed_node_indices = {0, 1, 2};
    first.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    first.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    firebreak::opt::OptimizationScenario second;
    second.scenario_id = 2;
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
#endif

void test_subproblem_structure_counts_and_convention() {
    const auto opt = make_path_instance(1);
    const auto structure = firebreak::benders::analyze_fpp_scenario_subproblem_structure(opt, 0);

    assert(structure.scenario_id == 1);
    assert(structure.x_variable_count == 3);
    assert(structure.y_copy_variable_count == 3);
    assert(structure.total_variable_count == 6);
    assert(structure.y_fix_constraint_count == 3);
    assert(structure.ignition_constraint_count == 1);
    assert(structure.propagation_constraint_count == 2);
    assert(structure.total_constraint_count == 6);
    assert(structure.x_variables_are_continuous);
    assert(structure.y_copy_variables_are_continuous);
    assert(structure.objective_is_sum_x);

    assert(structure.propagation_constraints.size() == 2);
    assert(structure.propagation_constraints[0].u_index == 0);
    assert(structure.propagation_constraints[0].v_index == 1);
    assert(structure.propagation_constraints[0].target_has_firebreak_variable);
    assert(structure.propagation_constraints[0].target_y_position == 1);
}

void test_benders_cut_algebra() {
    const double q_value = 5.0;
    const std::vector<double> ybar = {0.0, 1.0, 0.0};
    const std::vector<double> pi = {0.0, -2.0, 1.0};

    double dual_dot_ybar = 0.0;
    firebreak::benders::BendersCut cut;
    cut.subproblem_objective = q_value;
    for (int i = 0; i < 3; ++i) {
        cut.coefficients_by_compact_index.push_back({i, pi[static_cast<std::size_t>(i)]});
        dual_dot_ybar += pi[static_cast<std::size_t>(i)] * ybar[static_cast<std::size_t>(i)];
    }
    cut.rhs_constant = q_value - dual_dot_ybar;

    assert_close(cut.rhs_constant, 7.0);
    assert_close(cut.evaluateAt(ybar), q_value);
}

void test_fpp_lifted_lower_bound_algebra() {
    const auto opt = make_path_instance(1);
    const auto inequality = firebreak::benders::build_fpp_lifted_lower_bound_for_scenario(opt, 0);

    assert_close(inequality.f_empty, 3.0);
    assert_close(inequality.rhs_constant, 3.0);
    assert(inequality.nonzero_coefficients == 2);
    assert((inequality.coefficients_by_compact_index[0] == std::pair<int, double>{1, -2.0}));
    assert((inequality.coefficients_by_compact_index[1] == std::pair<int, double>{2, -1.0}));
    assert_close(inequality.evaluateAt(std::vector<int>{0, 1, 0}, opt), 1.0);
}

void test_fpp_lifted_lower_bound_tree_singletons_are_exact() {
    const auto opt = make_path_instance(1);
    for (const int compact_index : opt.eligible_indices) {
        std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
        selected[static_cast<std::size_t>(compact_index)] = 1;
        const double true_singleton =
            firebreak::benders::evaluate_fixed_y_fpp_loss(opt, 0, selected).burned_area;
        const double optimistic =
            firebreak::benders::evaluate_optimistic_singleton_fpp_loss(opt, 0, compact_index).burned_area;
        assert_close(optimistic, true_singleton);
    }
}

void test_fpp_lifted_lower_bound_dag_is_conservative() {
    const auto opt = make_dag_alternate_path_instance(1);
    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    selected[1] = 1;
    const double true_singleton =
        firebreak::benders::evaluate_fixed_y_fpp_loss(opt, 0, selected).burned_area;
    const double optimistic =
        firebreak::benders::evaluate_optimistic_singleton_fpp_loss(opt, 0, 1).burned_area;
    assert(optimistic <= true_singleton + 1.0e-9);
    assert(optimistic < true_singleton);
}

void test_fpp_lifted_lower_bound_exhaustive_validity() {
    {
        const auto validation =
            firebreak::benders::validate_fpp_lifted_lower_bound_exhaustive(
                make_path_instance(2),
                0,
                2);
        assert(validation.valid);
    }
    {
        const auto validation =
            firebreak::benders::validate_fpp_lifted_lower_bound_exhaustive(
                make_dag_alternate_path_instance(2),
                0,
                2);
        assert(validation.valid);
    }
}

#ifdef FIREBREAK_WITH_CPLEX
void compare_benders_to_monolithic(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config = firebreak::risk::RiskMeasureConfig(),
    bool use_lifted_lower_bounds = false) {
    firebreak::solver::FppSaaCplexModel monolithic;
    const auto monolithic_result =
        monolithic.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, risk_config);

    firebreak::benders::FppBendersSolver benders;
    firebreak::benders::FppBendersOptions options;
    options.max_iterations = 20;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    options.use_lifted_lower_bounds = use_lifted_lower_bounds;
    const auto benders_result = benders.solve(opt, options);

    assert(benders_result.status == "Optimal");
    assert_close(benders_result.objective_value, monolithic_result.objective_value);
    assert_close(benders_result.expected_loss_component, monolithic_result.expected_loss_component);
    if (risk_config.type != firebreak::risk::RiskMeasureType::Expected) {
        assert_close(benders_result.cvar_loss_component, monolithic_result.cvar_loss_component);
    }
    assert(benders_result.benders_final_max_cut_violation <= 1.0e-6);
    assert(!benders_result.benders_iteration_log.empty());
    assert(benders_result.benders_master_solve_time_sec >= 0.0);
    assert(benders_result.benders_subproblem_time_sec >= 0.0);
    assert(benders_result.benders_subproblems_solved > 0);
    assert(benders_result.benders_average_subproblem_time_sec >= 0.0);
    assert(benders_result.benders_max_subproblem_time_sec >= 0.0);
    assert(static_cast<int>(benders_result.selected_firebreak_indices.size()) <= opt.budget);
    assert(benders_result.benders_use_lifted_lower_bounds == use_lifted_lower_bounds);
    if (use_lifted_lower_bounds) {
        assert(benders_result.benders_lifted_lower_bound_count ==
               static_cast<int>(opt.scenarios.size()));
        assert(!benders_result.benders_lifted_lower_bounds.empty());
    }
    for (const auto& log : benders_result.benders_iteration_log) {
        assert(log.subproblems_attempted == log.subproblems_solved);
        assert(log.subproblems_solved > 0);
        assert(log.average_subproblem_time_sec >= 0.0);
        assert(log.max_subproblem_time_sec >= 0.0);
    }
}

void test_cvar_master_structure() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::CVaR;
    config.cvarBeta = 0.5;

    firebreak::benders::FppBendersMaster expected_master;
    expected_master.initialize(opt);
    assert(expected_master.getNumVariables() == 4);
    assert(expected_master.getNumConstraints() == 3);

    firebreak::benders::FppBendersMaster cvar_master;
    cvar_master.initialize(opt, config);
    assert(cvar_master.getNumVariables() == 7);
    assert(cvar_master.getNumConstraints() == 5);
}

void test_mean_cvar_master_structure() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    config.cvarBeta = 0.5;
    config.cvarLambda = 0.5;

    firebreak::benders::FppBendersMaster master;
    master.initialize(opt, config);
    assert(master.getNumVariables() == 7);
    assert(master.getNumConstraints() == 5);
}

void test_optional_cplex_subproblem_solve_and_cut_tightness() {
    const auto opt = make_path_instance(1);
    firebreak::benders::FppScenarioSubproblem subproblem;
    const auto result = subproblem.solve(opt, 0, {0, 1, 0}, false);

    assert(!result.status.empty());
    assert_close(result.objective_value, 1.0);
    assert(result.duals_for_y_copy.size() == 3);

    std::vector<double> compact_y = {0.0, 1.0, 0.0};
    assert_close(result.benders_cut.evaluateAt(compact_y), result.objective_value);
}

void test_tiny_fpp_benders_matches_monolithic() {
    const auto opt = make_path_instance(1);
    compare_benders_to_monolithic(opt);
    compare_benders_to_monolithic(opt, firebreak::risk::RiskMeasureConfig(), true);
}

void test_zero_budget() {
    const auto opt = make_path_instance(0);
    firebreak::benders::FppBendersSolver benders;
    firebreak::benders::FppBendersOptions options;
    options.max_iterations = 20;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    const auto result = benders.solve(opt, options);

    assert(result.status == "Optimal");
    assert_close(result.objective_value, 3.0);
    assert(result.selected_firebreak_indices.empty());
    assert(result.benders_final_max_cut_violation <= 1.0e-6);
}

void test_alternative_optima_objective() {
    const auto opt = make_alternative_optima_instance();
    compare_benders_to_monolithic(opt);
}

void test_cvar_benders_matches_monolithic() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::CVaR;
    config.cvarBeta = 0.5;
    compare_benders_to_monolithic(opt, config);
    compare_benders_to_monolithic(opt, config, true);

    firebreak::benders::FppBendersSolver benders;
    firebreak::benders::FppBendersOptions options;
    options.max_iterations = 20;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = config;
    const auto result = benders.solve(opt, options);
    assert_close(result.objective_value, 2.0);
    assert_close(result.expected_loss_component, 1.5);
    assert_close(result.cvar_loss_component, 2.0);
}

void test_mean_cvar_benders_matches_monolithic() {
    const auto opt = make_two_scenario_risk_instance();
    firebreak::risk::RiskMeasureConfig config;
    config.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    config.cvarBeta = 0.5;
    config.cvarLambda = 0.5;
    compare_benders_to_monolithic(opt, config);
    compare_benders_to_monolithic(opt, config, true);

    firebreak::benders::FppBendersSolver benders;
    firebreak::benders::FppBendersOptions options;
    options.max_iterations = 20;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = config;
    const auto result = benders.solve(opt, options);
    assert_close(result.objective_value, 1.75);
    assert_close(result.expected_loss_component, 1.5);
    assert_close(result.cvar_loss_component, 2.0);
}
#endif

}  // namespace

int main() {
    test_subproblem_structure_counts_and_convention();
    test_benders_cut_algebra();
    test_fpp_lifted_lower_bound_algebra();
    test_fpp_lifted_lower_bound_tree_singletons_are_exact();
    test_fpp_lifted_lower_bound_dag_is_conservative();
    test_fpp_lifted_lower_bound_exhaustive_validity();
#ifdef FIREBREAK_WITH_CPLEX
    test_cvar_master_structure();
    test_mean_cvar_master_structure();
    test_optional_cplex_subproblem_solve_and_cut_tightness();
    test_tiny_fpp_benders_matches_monolithic();
    test_zero_budget();
    test_alternative_optima_objective();
    test_cvar_benders_matches_monolithic();
    test_mean_cvar_benders_matches_monolithic();
#else
    std::cout << "Skipping tiny FPP Benders solve tests because CPLEX is not enabled.\n";
#endif
    std::cout << "All FPP Benders tests passed.\n";
    return 0;
}
