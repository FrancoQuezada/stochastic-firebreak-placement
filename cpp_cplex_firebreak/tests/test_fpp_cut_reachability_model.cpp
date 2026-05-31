#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "eval/FppRecourseEvaluator.hpp"
#include "experiments/BatchExperimentConfig.hpp"
#include "heuristics/ReachabilityGreedyWarmStart.hpp"
#include "solver/FppCutReachabilityCplexModel.hpp"
#include "solver/FppSaaCplexModel.hpp"
#include "solver/WarmStart.hpp"

namespace {

firebreak::opt::OptimizationInstance make_opt_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    int budget,
    const std::vector<int>& eligible_compact_nodes = {}) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = budget;
    opt.node_mapper.build_from_nodes(original_nodes);

    if (eligible_compact_nodes.empty()) {
        for (const int original_node : original_nodes) {
            opt.eligible_original_nodes.push_back(original_node);
            opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
        }
    } else {
        opt.eligible_indices = eligible_compact_nodes;
        for (const int compact_node : eligible_compact_nodes) {
            opt.eligible_original_nodes.push_back(opt.node_mapper.to_node(compact_node));
        }
    }

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = original_nodes.front();
    scenario.message_filename = "synthetic.csv";
    scenario.arcs = arcs;
    for (int i = 0; i < static_cast<int>(original_nodes.size()); ++i) {
        scenario.observed_node_indices.push_back(i);
    }

    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();
    return opt;
}

void test_structure_counts_and_descriptors() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1, {1});

    const auto structure = firebreak::solver::analyze_fpp_cut_reachability_model_structure(opt);
    assert(structure.x_variable_count == 3);
    assert(structure.q_variable_count == 3);
    assert(structure.y_variable_count == 1);
    assert(structure.total_variable_count == 7);
    assert(structure.budget_constraint_count == 1);
    assert(structure.root_constraint_count == 2);
    assert(structure.propagation_entrance_constraint_count == 2);
    assert(structure.pass_through_constraint_count == 2);
    assert(structure.firebreak_upper_bound_constraint_count == 1);
    assert(structure.total_constraint_count == 8);
    assert(structure.has_y_for_node_index(1));
    assert(!structure.has_y_for_node_index(0));
    assert(!structure.has_y_for_node_index(2));
    assert(structure.observed_node_count_by_scenario == std::vector<std::size_t>{3});

    assert(structure.pass_through_constraints.size() == 2);
    assert(structure.pass_through_constraints[0].node_index == 1);
    assert(structure.pass_through_constraints[0].node_has_firebreak_variable);
    assert(structure.pass_through_constraints[1].node_index == 2);
    assert(!structure.pass_through_constraints[1].node_has_firebreak_variable);
}

void test_duplicate_arcs_are_deduplicated_in_structure() {
    const auto opt = make_opt_instance({1, 2}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 1, 1, 2},
    }, 1);

    const auto structure = firebreak::solver::analyze_fpp_cut_reachability_model_structure(opt);
    assert(structure.propagation_entrance_constraint_count == 1);
}

int local_position(const std::vector<int>& nodes, int compact_node) {
    const auto it = std::find(nodes.begin(), nodes.end(), compact_node);
    assert(it != nodes.end());
    return static_cast<int>(std::distance(nodes.begin(), it));
}

void test_cut_mip_start_values_for_reached_firebreak() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);

    const auto values =
        firebreak::solver::build_fpp_cut_reachability_mip_start_values(opt, {1});
    assert(values.feasible);
    assert(values.y_selected_by_compact_node[1]);

    const int root_pos = local_position(values.nodes_by_scenario[0], 0);
    const int a_pos = local_position(values.nodes_by_scenario[0], 1);
    const int b_pos = local_position(values.nodes_by_scenario[0], 2);
    assert(values.q_reached_by_scenario[0][root_pos]);
    assert(values.x_burned_by_scenario[0][root_pos]);
    assert(values.q_reached_by_scenario[0][a_pos]);
    assert(!values.x_burned_by_scenario[0][a_pos]);
    assert(!values.q_reached_by_scenario[0][b_pos]);
    assert(!values.x_burned_by_scenario[0][b_pos]);
}

void test_cut_mip_start_values_root_convention() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1, {0});

    const auto values =
        firebreak::solver::build_fpp_cut_reachability_mip_start_values(opt, {0});
    assert(values.feasible);
    assert(values.y_selected_by_compact_node[0]);

    const int root_pos = local_position(values.nodes_by_scenario[0], 0);
    const int a_pos = local_position(values.nodes_by_scenario[0], 1);
    const int b_pos = local_position(values.nodes_by_scenario[0], 2);
    assert(values.q_reached_by_scenario[0][root_pos]);
    assert(values.x_burned_by_scenario[0][root_pos]);
    assert(values.q_reached_by_scenario[0][a_pos]);
    assert(values.x_burned_by_scenario[0][a_pos]);
    assert(values.q_reached_by_scenario[0][b_pos]);
    assert(values.x_burned_by_scenario[0][b_pos]);
}

#ifdef FIREBREAK_WITH_CPLEX
void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-6);
}

firebreak::solver::WarmStart build_greedy_warm_start(
    const firebreak::opt::OptimizationInstance& opt) {
    firebreak::heuristics::ReachabilityGreedyWarmStartOptions options;
    firebreak::heuristics::ReachabilityGreedyWarmStart greedy(opt, options);
    const auto greedy_result = greedy.run();

    std::vector<int> original_nodes;
    for (const int compact_node : greedy_result.selected_firebreak_compact_nodes) {
        original_nodes.push_back(opt.node_mapper.to_node(compact_node));
    }
    return firebreak::solver::prepare_warm_start_from_original_nodes(
        original_nodes,
        opt,
        opt.budget,
        "reachability-greedy-cut-test");
}

void assert_base_cut_match(
    const firebreak::opt::OptimizationInstance& opt,
    double expected_objective) {
    firebreak::solver::FppSaaCplexModel base_model;
    firebreak::solver::FppCutReachabilityCplexModel cut_model;

    const auto base = base_model.solve(opt, 30.0, 0.0, 1, false);
    const auto cut = cut_model.solve(opt, 30.0, 0.0, 1, false);

    assert_close(base.objective_value, expected_objective);
    assert_close(cut.objective_value, expected_objective);
    assert_close(base.objective_value, cut.objective_value);
    assert(cut.formulation == "cut");

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate(cut.selected_firebreak_indices, true);
    assert_close(recourse.expected_burned_area, cut.objective_value);
}

void test_chain_base_cut_equivalence() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    assert_base_cut_match(opt, 1.0);
}

void test_branching_base_cut_equivalence() {
    const auto opt = make_opt_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 4, 3, 5},
    }, 1);
    assert_base_cut_match(opt, 3.0);
}

void test_parallel_paths_base_cut_equivalence() {
    auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    assert_base_cut_match(opt, 3.0);

    opt.budget = 2;
    assert_base_cut_match(opt, 1.0);
}

void test_root_firebreak_convention() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1, {0});
    assert_base_cut_match(opt, 3.0);
}

void test_noneligible_intermediate_node() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1, {2});

    const auto structure = firebreak::solver::analyze_fpp_cut_reachability_model_structure(opt);
    assert(structure.pass_through_constraints[0].node_index == 1);
    assert(!structure.pass_through_constraints[0].node_has_firebreak_variable);
    assert_base_cut_match(opt, 2.0);
}

void test_cut_accepts_full_greedy_warm_start() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);

    auto warm_start = build_greedy_warm_start(opt);

    firebreak::solver::FppCutReachabilityCplexModel cut_model;
    const auto result = cut_model.solve(opt, 30.0, 0.0, 1, false, &warm_start);
    assert(result.warm_start_used);
    assert_close(result.objective_value, 1.0);

    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    const auto recourse = evaluator.evaluate(result.selected_firebreak_indices, true);
    assert_close(recourse.expected_burned_area, result.objective_value);
}

void test_cut_full_warm_start_with_dominator_cuts() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    auto warm_start = build_greedy_warm_start(opt);

    firebreak::cuts::DominatorCutOptions dominator;
    dominator.enabled = true;
    dominator.max_aggregate_dominator_cuts_per_scenario = 10;
    dominator.max_individual_dominator_cuts_per_scenario = 20;

    firebreak::solver::FppCutReachabilityCplexModel cut_model;
    const auto no_start = cut_model.solve(opt, 30.0, 0.0, 1, false, nullptr, &dominator);
    const auto with_start = cut_model.solve(opt, 30.0, 0.0, 1, false, &warm_start, &dominator);
    assert(with_start.warm_start_used);
    assert(with_start.dominator_cuts_added > 0);
    assert_close(no_start.objective_value, with_start.objective_value);
}

void test_cut_full_warm_start_with_separator_cuts() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    auto warm_start = build_greedy_warm_start(opt);

    firebreak::cuts::SeparatorCutOptions separator;
    separator.enabled = true;
    separator.sep_at_root = true;
    separator.sep_frequency_nodes = 1;
    separator.sep_max_scenarios_per_call = 10;
    separator.sep_max_nodes_per_scenario = 10;
    separator.sep_max_cuts_per_call = 100;
    separator.sep_min_violation = 1.0e-6;
    separator.sep_max_cut_cardinality = 50;

    firebreak::solver::FppCutReachabilityCplexModel cut_model;
    const auto no_start = cut_model.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, &separator);
    const auto with_start = cut_model.solve(opt, 30.0, 0.0, 1, false, &warm_start, nullptr, &separator);
    assert(with_start.warm_start_used);
    assert(with_start.separator_cuts_enabled);
    assert_close(no_start.objective_value, with_start.objective_value);
}

void test_cut_reachability_risk_objectives_match_base() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.9;
    cvar.cvarLambda = 1.0;

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.9;
    mean_cvar.cvarLambda = 0.5;

    firebreak::solver::FppSaaCplexModel base_model;
    firebreak::solver::FppCutReachabilityCplexModel cut_model;
    for (const auto& risk_config : {cvar, mean_cvar}) {
        const auto base = base_model.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, risk_config);
        const auto cut = cut_model.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, risk_config);
        assert(base.risk_measure == cut.risk_measure);
        assert_close(base.objective_value, cut.objective_value);
        assert_close(base.expected_loss_component, cut.expected_loss_component);
        assert(std::isfinite(cut.cvar_loss_component));
        assert(std::isfinite(cut.risk_threshold_value));
    }
}

firebreak::solver::ModelResult solve_fpp_mode(
    const firebreak::opt::OptimizationInstance& opt,
    const std::string& mode) {
    const auto settings = firebreak::experiments::fpp_mode_settings(mode);

    firebreak::solver::WarmStart warm_start;
    const firebreak::solver::WarmStart* warm_start_ptr = nullptr;
    if (settings.enable_greedy_warm_start) {
        warm_start = build_greedy_warm_start(opt);
        warm_start_ptr = &warm_start;
    }

    firebreak::cuts::DominatorCutOptions dominator;
    dominator.enabled = settings.enable_dominator_cuts;
    dominator.max_aggregate_dominator_cuts_per_scenario = 10;
    dominator.max_individual_dominator_cuts_per_scenario = 20;
    const firebreak::cuts::DominatorCutOptions* dominator_ptr =
        dominator.enabled ? &dominator : nullptr;

    firebreak::cuts::SeparatorCutOptions separator;
    separator.enabled = settings.enable_separator_cuts;
    separator.sep_at_root = true;
    separator.sep_frequency_nodes = 1;
    separator.sep_max_scenarios_per_call = 10;
    separator.sep_max_nodes_per_scenario = 10;
    separator.sep_max_cuts_per_call = 100;
    separator.sep_min_violation = 1.0e-6;
    separator.sep_max_cut_cardinality = 50;
    const firebreak::cuts::SeparatorCutOptions* separator_ptr =
        separator.enabled ? &separator : nullptr;

    firebreak::solver::ModelResult result;
    if (settings.formulation == "base") {
        firebreak::solver::FppSaaCplexModel model;
        result = model.solve(
            opt,
            30.0,
            0.0,
            1,
            false,
            warm_start_ptr,
            dominator_ptr,
            separator_ptr);
    } else {
        firebreak::solver::FppCutReachabilityCplexModel model;
        result = model.solve(
            opt,
            30.0,
            0.0,
            1,
            false,
            warm_start_ptr,
            dominator_ptr,
            separator_ptr);
    }

    result.fpp_mode = settings.mode;
    result.greedy_warm_start_enabled = settings.enable_greedy_warm_start;
    result.dominator_cuts_enabled = settings.enable_dominator_cuts;
    result.separator_cuts_enabled = settings.enable_separator_cuts;
    result.local_search_enabled = settings.enable_local_search;
    return result;
}

void test_all_declared_fpp_modes_match_on_tiny_chain() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);

    const auto& modes = firebreak::experiments::supported_fpp_modes();
    double reference_objective = -1.0;
    for (const auto& mode : modes) {
        const auto result = solve_fpp_mode(opt, mode);
        assert(result.local_search_enabled == false);
        assert(result.fpp_mode == mode);
        assert(result.formulation == (mode.find("fpp_cut") == 0 ? "cut" : "base"));
        assert(result.greedy_warm_start_enabled == (mode.find("greedy") != std::string::npos));
        assert(result.dominator_cuts_enabled == (mode.find("dominator") != std::string::npos));
        assert(result.separator_cuts_enabled == (mode.find("separator") != std::string::npos));
        if (result.greedy_warm_start_enabled) {
            assert(result.warm_start_used);
        }

        firebreak::eval::FppRecourseEvaluator evaluator(opt);
        const auto recourse = evaluator.evaluate(result.selected_firebreak_indices, false);
        assert_close(recourse.expected_burned_area, result.objective_value);

        if (reference_objective < 0.0) {
            reference_objective = result.objective_value;
        }
        assert_close(result.objective_value, reference_objective);
    }
    assert_close(reference_objective, 1.0);
}
#endif

}  // namespace

int main() {
    test_structure_counts_and_descriptors();
    test_duplicate_arcs_are_deduplicated_in_structure();
    test_cut_mip_start_values_for_reached_firebreak();
    test_cut_mip_start_values_root_convention();
#ifdef FIREBREAK_WITH_CPLEX
    test_chain_base_cut_equivalence();
    test_branching_base_cut_equivalence();
    test_parallel_paths_base_cut_equivalence();
    test_root_firebreak_convention();
    test_noneligible_intermediate_node();
    test_cut_accepts_full_greedy_warm_start();
    test_cut_full_warm_start_with_dominator_cuts();
    test_cut_full_warm_start_with_separator_cuts();
    test_cut_reachability_risk_objectives_match_base();
    test_all_declared_fpp_modes_match_on_tiny_chain();
#endif
    std::cout << "All FPP cut/reachability formulation tests passed.\n";
    return 0;
}
