#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "benders/DpvLiftedLowerBound.hpp"
#include "opt/DpvIndexBuilder.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include "benders/DpvBendersSolver.hpp"
#include "solver/DpvSaaCplexModel.hpp"
#endif

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-6);
}

firebreak::opt::OptimizationInstance make_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_path";
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
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_parallel_choice_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_parallel_choice";
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

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_branch_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_branch";
    opt.alpha = 2.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 2;
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

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_alternate_path_counterexample_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_alternate_path_counterexample";
    opt.alpha = 2.0 / 5.0;
    opt.n_cells = 5;
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {0, 1, 2, 3, 4};
    opt.eligible_original_nodes = {1, 2, 3, 4, 5};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 3, 2, 4});
    scenario.arcs.push_back(firebreak::opt::CompactArc{2, 4, 3, 5});
    scenario.arcs.push_back(firebreak::opt::CompactArc{3, 4, 4, 5});

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_multiple_parent_dag_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic_multiple_parent_dag";
    opt.alpha = 2.0 / 5.0;
    opt.n_cells = 5;
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {0, 1, 2, 3, 4};
    opt.eligible_original_nodes = {1, 2, 3, 4, 5};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 3, 2, 4});
    scenario.arcs.push_back(firebreak::opt::CompactArc{2, 3, 3, 4});
    scenario.arcs.push_back(firebreak::opt::CompactArc{3, 4, 4, 5});

    firebreak::opt::DpvIndexBuilder dpv_builder;
    scenario.dpv = dpv_builder.build_for_scenario(scenario, opt.node_mapper);

    opt.total_arcs = scenario.arcs.size();
    opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_duplicate_product_pair_instance() {
    auto opt = make_path_instance();
    auto& scenario = opt.scenarios.front();

    for (const auto& pair : scenario.dpv.product_pairs) {
        if (pair.successor_index == 1 && pair.descendant_index == 2) {
            scenario.dpv.product_pairs.push_back(pair);
            opt.total_dpv_pairs = scenario.dpv.product_pairs.size();
            return opt;
        }
    }

    assert(false && "Expected path instance to contain product pair successor=1 descendant=2.");
    return opt;
}

std::vector<std::vector<int>> build_test_adjacency(
    const firebreak::opt::OptimizationScenario& scenario,
    int node_count) {
    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(node_count));
    for (const auto& arc : scenario.arcs) {
        adjacency[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
    }
    for (auto& successors : adjacency) {
        std::sort(successors.begin(), successors.end());
        successors.erase(std::unique(successors.begin(), successors.end()), successors.end());
    }
    return adjacency;
}

std::vector<int> closed_test_downstream_nodes(
    int source,
    const std::vector<std::vector<int>>& adjacency) {
    std::vector<char> visited(adjacency.size(), 0);
    std::vector<int> downstream;
    std::vector<int> stack = {source};
    visited[static_cast<std::size_t>(source)] = 1;

    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();
        downstream.push_back(current);
        for (const int next : adjacency[static_cast<std::size_t>(current)]) {
            if (!visited[static_cast<std::size_t>(next)]) {
                visited[static_cast<std::size_t>(next)] = 1;
                stack.push_back(next);
            }
        }
    }

    return downstream;
}

double slow_reference_optimistic_singleton_loss_for_test(
    const firebreak::opt::OptimizationInstance& opt,
    int scenario_position,
    int compact_index) {
    const int node_count = opt.node_mapper.size();
    const auto& scenario = opt.scenarios[static_cast<std::size_t>(scenario_position)];
    std::vector<char> selected(static_cast<std::size_t>(node_count), 0);
    auto empty_loss =
        firebreak::benders::evaluate_fixed_y_dpv_loss(opt, scenario_position, selected);
    if (compact_index == scenario.ignition_index) {
        return empty_loss.loss;
    }

    const auto adjacency = build_test_adjacency(scenario, node_count);
    for (const int downstream_node : closed_test_downstream_nodes(compact_index, adjacency)) {
        empty_loss.burned_by_compact_index[static_cast<std::size_t>(downstream_node)] = 0;
    }

    double loss = 0.0;
    for (const auto& pair : scenario.dpv.product_pairs) {
        if (empty_loss.burned_by_compact_index[static_cast<std::size_t>(pair.successor_index)] &&
            empty_loss.burned_by_compact_index[static_cast<std::size_t>(pair.descendant_index)]) {
            loss += 1.0;
        }
    }
    return loss;
}

firebreak::benders::DpvLiftedLowerBoundInequality
build_reference_optimistic_lifted_lower_bound_for_test(
    const firebreak::opt::OptimizationInstance& opt,
    int scenario_position) {
    const int node_count = opt.node_mapper.size();
    std::vector<char> selected(static_cast<std::size_t>(node_count), 0);
    const auto empty_loss =
        firebreak::benders::evaluate_fixed_y_dpv_loss(opt, scenario_position, selected);

    firebreak::benders::DpvLiftedLowerBoundInequality inequality;
    inequality.scenario_id = empty_loss.scenario_id;
    inequality.f_empty = empty_loss.loss;
    inequality.rhs_constant = empty_loss.loss;

    for (const int compact_index : opt.eligible_indices) {
        const double optimistic_singleton_loss =
            slow_reference_optimistic_singleton_loss_for_test(
                opt,
                scenario_position,
                compact_index);
        const double coefficient = optimistic_singleton_loss - empty_loss.loss;
        if (std::fabs(coefficient) > 1.0e-12) {
            inequality.coefficients_by_compact_index.push_back({compact_index, coefficient});
            ++inequality.nonzero_coefficients;
        }
    }
    return inequality;
}

firebreak::benders::DpvLiftedLowerBoundInequality
build_true_singleton_lifted_lower_bound_for_test(
    const firebreak::opt::OptimizationInstance& opt,
    int scenario_position) {
    const int node_count = opt.node_mapper.size();
    std::vector<char> selected(static_cast<std::size_t>(node_count), 0);
    const auto empty_loss =
        firebreak::benders::evaluate_fixed_y_dpv_loss(opt, scenario_position, selected);

    firebreak::benders::DpvLiftedLowerBoundInequality inequality;
    inequality.scenario_id = empty_loss.scenario_id;
    inequality.f_empty = empty_loss.loss;
    inequality.rhs_constant = empty_loss.loss;

    for (const int compact_index : opt.eligible_indices) {
        selected[static_cast<std::size_t>(compact_index)] = 1;
        const double singleton_loss =
            firebreak::benders::evaluate_fixed_y_dpv_loss(opt, scenario_position, selected).loss;
        selected[static_cast<std::size_t>(compact_index)] = 0;

        const double coefficient = singleton_loss - empty_loss.loss;
        if (std::fabs(coefficient) > 1.0e-12) {
            inequality.coefficients_by_compact_index.push_back({compact_index, coefficient});
            ++inequality.nonzero_coefficients;
        }
    }
    return inequality;
}

bool has_exhaustive_lifted_bound_violation_for_test(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::benders::DpvLiftedLowerBoundInequality& inequality,
    int scenario_position,
    int budget) {
    const int eligible_count = static_cast<int>(opt.eligible_indices.size());
    assert(eligible_count < 30);

    const int masks = 1 << eligible_count;
    for (int mask = 0; mask < masks; ++mask) {
        int selected_count = 0;
        for (int pos = 0; pos < eligible_count; ++pos) {
            if ((mask & (1 << pos)) != 0) {
                ++selected_count;
            }
        }
        if (selected_count > budget) {
            continue;
        }
        std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
        for (int pos = 0; pos < eligible_count; ++pos) {
            if ((mask & (1 << pos)) != 0) {
                const int compact_index = opt.eligible_indices[static_cast<std::size_t>(pos)];
                selected[static_cast<std::size_t>(compact_index)] = 1;
            }
        }

        const double actual =
            firebreak::benders::evaluate_fixed_y_dpv_loss(opt, scenario_position, selected).loss;
        const double lifted = inequality.evaluateAtCompact(selected);
        if (actual + 1.0e-9 < lifted) {
            return true;
        }
    }
    return false;
}

double coefficient_for_compact_node(
    const firebreak::benders::DpvLiftedLowerBoundInequality& inequality,
    int compact_index) {
    for (const auto& [node, coefficient] : inequality.coefficients_by_compact_index) {
        if (node == compact_index) {
            return coefficient;
        }
    }
    return 0.0;
}

void assert_same_lifted_inequality(
    const firebreak::benders::DpvLiftedLowerBoundInequality& actual,
    const firebreak::benders::DpvLiftedLowerBoundInequality& expected) {
    assert(actual.scenario_id == expected.scenario_id);
    assert_close(actual.f_empty, expected.f_empty);
    assert_close(actual.rhs_constant, expected.rhs_constant);
    assert(actual.nonzero_coefficients == expected.nonzero_coefficients);
    assert(actual.coefficients_by_compact_index.size() ==
           expected.coefficients_by_compact_index.size());

    for (std::size_t i = 0; i < actual.coefficients_by_compact_index.size(); ++i) {
        assert(actual.coefficients_by_compact_index[i].first ==
               expected.coefficients_by_compact_index[i].first);
        assert_close(
            actual.coefficients_by_compact_index[i].second,
            expected.coefficients_by_compact_index[i].second);
    }
}

double true_singleton_loss(
    const firebreak::opt::OptimizationInstance& opt,
    int scenario_position,
    int compact_index) {
    std::vector<char> selected(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    selected[static_cast<std::size_t>(compact_index)] = 1;
    return firebreak::benders::evaluate_fixed_y_dpv_loss(opt, scenario_position, selected).loss;
}

void assert_tree_singletons_match_true_singletons(
    const firebreak::opt::OptimizationInstance& opt) {
    for (const int compact_index : opt.eligible_indices) {
        const double optimistic =
            firebreak::benders::evaluate_optimistic_singleton_dpv_loss(opt, 0, compact_index).loss;
        const double actual = true_singleton_loss(opt, 0, compact_index);
        assert_close(optimistic, actual);
    }
}

void assert_dag_optimistic_singletons_are_no_greater_than_true_singletons(
    const firebreak::opt::OptimizationInstance& opt) {
    bool found_strict_difference = false;
    for (const int compact_index : opt.eligible_indices) {
        const double optimistic =
            firebreak::benders::evaluate_optimistic_singleton_dpv_loss(opt, 0, compact_index).loss;
        const double actual = true_singleton_loss(opt, 0, compact_index);
        assert(optimistic <= actual + 1.0e-9);
        if (optimistic + 1.0e-9 < actual) {
            found_strict_difference = true;
        }
    }
    assert(found_strict_difference);
}

void test_fixed_y_dpv_loss_traversal() {
    const auto opt = make_path_instance();
    std::vector<char> selected(3, 0);

    assert_close(firebreak::benders::evaluate_fixed_y_dpv_loss(opt, 0, selected).loss, 5.0);

    selected[1] = 1;
    assert_close(firebreak::benders::evaluate_fixed_y_dpv_loss(opt, 0, selected).loss, 0.0);

    selected[1] = 0;
    selected[2] = 1;
    assert_close(firebreak::benders::evaluate_fixed_y_dpv_loss(opt, 0, selected).loss, 2.0);

    selected[1] = 1;
    assert_close(firebreak::benders::evaluate_fixed_y_dpv_loss(opt, 0, selected).loss, 0.0);
}

void test_lifted_lower_bound_algebra() {
    const auto opt = make_path_instance();
    const auto inequality = firebreak::benders::build_dpv_lifted_lower_bound_for_scenario(opt, 0);

    assert(inequality.scenario_id == 1);
    assert_close(inequality.f_empty, 5.0);
    assert_close(inequality.rhs_constant, 5.0);
    assert(inequality.nonzero_coefficients == 2);

    std::vector<char> empty(3, 0);
    assert_close(inequality.evaluateAtCompact(empty), 5.0);

    std::vector<int> singleton_middle = {0, 1, 0};
    assert_close(inequality.evaluateAt(singleton_middle, opt), 0.0);

    std::vector<int> singleton_tail = {0, 0, 1};
    assert_close(inequality.evaluateAt(singleton_tail, opt), 2.0);

    std::vector<int> combined = {0, 1, 1};
    assert_close(inequality.evaluateAt(combined, opt), -3.0);
}

void test_lifted_lower_bound_does_not_double_count_product_records() {
    const auto opt = make_path_instance();
    const auto inequality = firebreak::benders::build_dpv_lifted_lower_bound_for_scenario(opt, 0);

    assert_close(coefficient_for_compact_node(inequality, 1), -5.0);
    assert_close(coefficient_for_compact_node(inequality, 2), -3.0);
}

void test_lifted_lower_bound_preserves_product_multiplicity() {
    const auto opt = make_duplicate_product_pair_instance();
    const auto inequality = firebreak::benders::build_dpv_lifted_lower_bound_for_scenario(opt, 0);

    assert_close(inequality.f_empty, 6.0);
    assert_close(coefficient_for_compact_node(inequality, 1), -6.0);
    assert_close(coefficient_for_compact_node(inequality, 2), -4.0);
}

void test_optimized_lifted_lower_bound_matches_reference_logic() {
    const auto opt = make_duplicate_product_pair_instance();
    const auto optimized = firebreak::benders::build_dpv_lifted_lower_bound_for_scenario(opt, 0);
    const auto reference = build_reference_optimistic_lifted_lower_bound_for_test(opt, 0);
    assert_same_lifted_inequality(optimized, reference);

    std::vector<char> y_empty(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    assert_close(optimized.evaluateAtCompact(y_empty), reference.evaluateAtCompact(y_empty));

    std::vector<char> y_middle(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    y_middle[1] = 1;
    assert_close(optimized.evaluateAtCompact(y_middle), reference.evaluateAtCompact(y_middle));
}

void test_lifted_lower_bound_exhaustive_validity_on_simple_graphs() {
    const auto path = make_path_instance();
    auto path_validation =
        firebreak::benders::validate_dpv_lifted_lower_bound_exhaustive(path, 0, path.budget);
    assert(path_validation.valid);

    const auto branch = make_branch_instance();
    auto branch_validation =
        firebreak::benders::validate_dpv_lifted_lower_bound_exhaustive(branch, 0, branch.budget);
    assert(branch_validation.valid);

    const auto parallel = make_parallel_choice_instance();
    auto parallel_validation =
        firebreak::benders::validate_dpv_lifted_lower_bound_exhaustive(parallel, 0, parallel.budget);
    assert(parallel_validation.valid);

    const auto alternate_path = make_alternate_path_counterexample_instance();
    auto alternate_validation = firebreak::benders::validate_dpv_lifted_lower_bound_exhaustive(
        alternate_path,
        0,
        alternate_path.budget);
    assert(alternate_validation.valid);

    const auto multiple_parent = make_multiple_parent_dag_instance();
    auto multiple_parent_validation =
        firebreak::benders::validate_dpv_lifted_lower_bound_exhaustive(
            multiple_parent,
            0,
            multiple_parent.budget);
    assert(multiple_parent_validation.valid);
}

void test_true_singleton_version_invalid_but_optimistic_version_valid_on_alternate_path_dag() {
    const auto opt = make_alternate_path_counterexample_instance();
    const auto true_singleton_inequality =
        build_true_singleton_lifted_lower_bound_for_test(opt, 0);
    assert(has_exhaustive_lifted_bound_violation_for_test(
        opt,
        true_singleton_inequality,
        0,
        opt.budget));

    auto optimistic_validation =
        firebreak::benders::validate_dpv_lifted_lower_bound_exhaustive(opt, 0, opt.budget);
    assert(optimistic_validation.valid);
}

void test_optimistic_singletons_match_true_singletons_on_trees() {
    assert_tree_singletons_match_true_singletons(make_path_instance());
    assert_tree_singletons_match_true_singletons(make_branch_instance());
}

void test_optimistic_singletons_are_conservative_on_dags() {
    assert_dag_optimistic_singletons_are_no_greater_than_true_singletons(
        make_alternate_path_counterexample_instance());
    assert_dag_optimistic_singletons_are_no_greater_than_true_singletons(
        make_multiple_parent_dag_instance());
}

#ifdef FIREBREAK_WITH_CPLEX
firebreak::solver::ModelResult solve_benders(
    const firebreak::opt::OptimizationInstance& opt,
    double tolerance = 1.0e-7,
    bool use_lifted_lower_bounds = false) {
    firebreak::benders::DpvBendersSolver benders;
    firebreak::benders::DpvBendersOptions options;
    options.max_iterations = 20;
    options.tolerance = tolerance;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.use_lifted_lower_bounds = use_lifted_lower_bounds;
    return benders.solve(opt, options);
}

void assert_clean_benders_convergence(
    const firebreak::solver::ModelResult& result,
    double tolerance = 1.0e-7) {
    assert(result.status == "Optimal");
    assert(result.benders_status == "Optimal");
    assert(result.benders_termination_reason == "CONVERGED_NO_VIOLATED_CUTS");
    assert(!result.benders_termination_reason.empty());
    assert(result.iterations == result.benders_iterations);
    assert(result.cuts_added == result.benders_cuts_added);
    assert_close(result.max_cut_violation, result.benders_final_max_cut_violation);
    assert(result.benders_final_max_cut_violation <= tolerance);
    assert(result.benders_largest_intermediate_cut_violation >= result.benders_final_max_cut_violation);
    assert(result.benders_master_solve_time_sec >= 0.0);
    assert(result.benders_subproblem_time_sec >= 0.0);
    assert(result.benders_subproblems_solved > 0);
    assert(result.benders_average_subproblem_time_sec >= 0.0);
    assert(result.benders_max_subproblem_time_sec >= 0.0);
}

void assert_iteration_diagnostics_are_consistent(const firebreak::solver::ModelResult& result) {
    assert(!result.benders_iteration_log.empty());
    int cumulative_cuts = 0;
    for (const auto& log : result.benders_iteration_log) {
        assert(log.iteration > 0);
        assert(log.cuts_added >= 0);
        assert(log.cumulative_cuts >= 0);
        assert(log.subproblems_attempted == log.subproblems_solved);
        assert(log.subproblems_solved > 0);
        assert(log.master_time_sec >= 0.0);
        assert(log.subproblem_time_sec >= 0.0);
        assert(log.average_subproblem_time_sec >= 0.0);
        assert(log.max_subproblem_time_sec >= 0.0);
        assert(log.iteration_time_sec >= 0.0);
        assert(log.selected_firebreak_count == static_cast<int>(log.selected_firebreaks.size()));
        cumulative_cuts += log.cuts_added;
        assert(log.cumulative_cuts == cumulative_cuts);
    }
    assert(cumulative_cuts == result.benders_cuts_added);
}

void test_benders_matches_monolithic_on_tiny_path() {
    const auto opt = make_path_instance();

    firebreak::solver::DpvSaaCplexModel monolithic;
    const auto monolithic_result = monolithic.solve(opt, 30.0, 0.0, 1, false);

    const auto benders_result = solve_benders(opt);

    assert(monolithic_result.status.size() > 0);
    assert_clean_benders_convergence(benders_result);
    assert_iteration_diagnostics_are_consistent(benders_result);
    assert_close(benders_result.objective_value, monolithic_result.objective_value);
    assert_close(benders_result.best_bound, monolithic_result.objective_value);
    assert(benders_result.iterations > 0);
    assert(benders_result.cuts_added > 0);
    assert(!benders_result.benders_use_lifted_lower_bounds);
    assert(benders_result.benders_lifted_lower_bound_count == 0);
}

void test_benders_with_lifted_lower_bounds_matches_monolithic_on_tiny_path() {
    const auto opt = make_path_instance();

    firebreak::solver::DpvSaaCplexModel monolithic;
    const auto monolithic_result = monolithic.solve(opt, 30.0, 0.0, 1, false);

    const auto benders_result = solve_benders(opt, 1.0e-7, true);

    assert_clean_benders_convergence(benders_result);
    assert_iteration_diagnostics_are_consistent(benders_result);
    assert_close(benders_result.objective_value, monolithic_result.objective_value);
    assert_close(benders_result.best_bound, monolithic_result.objective_value);
    assert(benders_result.benders_use_lifted_lower_bounds);
    assert(benders_result.benders_lifted_lower_bound_count == 1);
    assert(benders_result.benders_lifted_lower_bound_nonzero_coefficients > 0);
    assert(benders_result.benders_lifted_lower_bound_precompute_time_sec >= 0.0);
    assert(!benders_result.benders_lifted_lower_bounds.empty());
}

void test_benders_handles_alternative_optima() {
    const auto opt = make_parallel_choice_instance();
    const auto benders_result = solve_benders(opt);

    assert_clean_benders_convergence(benders_result);
    assert_iteration_diagnostics_are_consistent(benders_result);
    assert_close(benders_result.objective_value, 2.0);
    assert_close(benders_result.best_bound, 2.0);
    assert(benders_result.selected_firebreak_original_nodes.size() == 1);
    const int selected = benders_result.selected_firebreak_original_nodes.front();
    assert(selected == 2 || selected == 3);
}

void test_benders_handles_zero_budget() {
    auto opt = make_path_instance();
    opt.alpha = 0.0;
    opt.budget = 0;

    const auto benders_result = solve_benders(opt);

    assert_clean_benders_convergence(benders_result);
    assert_iteration_diagnostics_are_consistent(benders_result);
    assert(benders_result.selected_firebreak_original_nodes.empty());
    assert_close(benders_result.objective_value, 5.0);
    assert_close(benders_result.best_bound, 5.0);
}
#endif

}  // namespace

int main() {
    test_fixed_y_dpv_loss_traversal();
    test_lifted_lower_bound_algebra();
    test_lifted_lower_bound_does_not_double_count_product_records();
    test_lifted_lower_bound_preserves_product_multiplicity();
    test_optimized_lifted_lower_bound_matches_reference_logic();
    test_lifted_lower_bound_exhaustive_validity_on_simple_graphs();
    test_true_singleton_version_invalid_but_optimistic_version_valid_on_alternate_path_dag();
    test_optimistic_singletons_match_true_singletons_on_trees();
    test_optimistic_singletons_are_conservative_on_dags();
#ifdef FIREBREAK_WITH_CPLEX
    test_benders_matches_monolithic_on_tiny_path();
    test_benders_with_lifted_lower_bounds_matches_monolithic_on_tiny_path();
    test_benders_handles_alternative_optima();
    test_benders_handles_zero_budget();
    std::cout << "All tiny DPV Benders tests passed.\n";
#else
    (void)make_path_instance();
    (void)make_parallel_choice_instance();
    std::cout << "Skipping tiny DPV Benders solve test because CPLEX is not enabled.\n";
#endif
    return 0;
}
