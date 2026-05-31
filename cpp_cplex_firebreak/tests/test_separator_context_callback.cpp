#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "cuts/DominatorCuts.hpp"
#include "cuts/SeparatorContextCallback.hpp"
#include "cuts/SeparatorCutSeparator.hpp"
#include "solver/FppCutReachabilityCplexModel.hpp"
#include "solver/FppSaaCplexModel.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace {

firebreak::opt::OptimizationInstance make_opt_instance(
    const std::vector<int>& original_nodes,
    const std::vector<firebreak::opt::CompactArc>& arcs,
    int budget,
    const std::vector<int>& eligible_compact_nodes = {},
    bool use_explicit_eligible = false) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.alpha = 1.0;
    opt.n_cells = static_cast<int>(original_nodes.size());
    opt.budget = budget;
    opt.node_mapper.build_from_nodes(original_nodes);

    if (use_explicit_eligible) {
        opt.eligible_indices = eligible_compact_nodes;
        for (const int compact_node : eligible_compact_nodes) {
            opt.eligible_original_nodes.push_back(opt.node_mapper.to_node(compact_node));
        }
    } else {
        for (const int original_node : original_nodes) {
            opt.eligible_original_nodes.push_back(original_node);
            opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
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

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-6);
}

firebreak::cuts::SeparatorCutOptions separator_options() {
    firebreak::cuts::SeparatorCutOptions options;
    options.enabled = true;
    options.sep_at_root = true;
    options.sep_frequency_nodes = 1;
    options.sep_max_scenarios_per_call = 10;
    options.sep_max_nodes_per_scenario = 10;
    options.sep_max_cuts_per_call = 100;
    options.sep_min_violation = 1.0e-6;
    options.sep_max_cut_cardinality = 50;
    return options;
}

bool contains_node(const std::vector<int>& nodes, int node) {
    return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

void assert_single_cut_with_target(
    const std::vector<firebreak::cuts::CandidateSeparatorCut>& cuts,
    int target) {
    assert(!cuts.empty());
    assert(cuts.front().target_compact_node == target);
    assert(cuts.front().violation > 1.0e-6);
}

std::vector<std::vector<double>> one_scenario_xbar(int node_count, int target, double value) {
    std::vector<std::vector<double>> xbar(1, std::vector<double>(static_cast<std::size_t>(node_count), 0.0));
    xbar[0][static_cast<std::size_t>(target)] = value;
    return xbar;
}

void test_direct_chain_violation() {
    // Direct core tests use artificial LP values so violated separator cuts are
    // exercised even when CPLEX solves tiny MIPs without entering a callback.
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);
    auto options = separator_options();
    firebreak::cuts::SeparatorCutSeparator separator(opt, options);

    std::vector<double> ybar(3, 0.0);
    ybar[1] = 0.8;
    ybar[2] = 0.0;
    const auto cuts = separator.separate(ybar, one_scenario_xbar(3, 2, 0.5));

    assert_single_cut_with_target(cuts, 2);
    assert((cuts.front().separator_compact_nodes == std::vector<int>{1}));
    assert_close(cuts.front().separator_capacity, 0.2);
    assert_close(cuts.front().lhs_value, 1.3);
    assert_close(cuts.front().rhs_value, 1.0);
    assert(cuts.front().violation > options.sep_min_violation);
}

void test_direct_parallel_path_violation() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 2);
    auto options = separator_options();
    firebreak::cuts::SeparatorCutSeparator separator(opt, options);

    std::vector<double> ybar(4, 0.0);
    ybar[1] = 0.6;
    ybar[2] = 0.6;
    ybar[3] = 0.0;
    const auto cuts = separator.separate(ybar, one_scenario_xbar(4, 3, 0.9));

    assert_single_cut_with_target(cuts, 3);
    assert((cuts.front().separator_compact_nodes == std::vector<int>{1, 2}));
    assert_close(cuts.front().separator_capacity, 0.8);
    assert_close(cuts.front().lhs_value, 2.1);
    assert_close(cuts.front().rhs_value, 2.0);
    assert(cuts.front().violation > options.sep_min_violation);
}

void test_direct_noneligible_separator() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1, {2}, true);
    auto options = separator_options();
    firebreak::cuts::SeparatorCutSeparator separator(opt, options);

    std::vector<double> ybar(3, 0.0);
    ybar[2] = 0.7;
    const auto cuts = separator.separate(ybar, one_scenario_xbar(3, 2, 0.5));

    assert_single_cut_with_target(cuts, 2);
    assert(!contains_node(cuts.front().separator_compact_nodes, 1));
    assert((cuts.front().separator_compact_nodes == std::vector<int>{2}));
    assert(cuts.front().violation > options.sep_min_violation);
}

void test_direct_root_target_skipped() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);
    auto options = separator_options();
    firebreak::cuts::SeparatorCutSeparator separator(opt, options);

    const auto cuts = separator.separate(std::vector<double>(3, 0.0), one_scenario_xbar(3, 0, 1.0));
    assert(cuts.empty());
}

#ifdef FIREBREAK_WITH_CPLEX
firebreak::cuts::DominatorCutOptions dominator_options() {
    firebreak::cuts::DominatorCutOptions options;
    options.enabled = true;
    options.max_aggregate_dominator_cuts_per_scenario = 10;
    options.max_individual_dominator_cuts_per_scenario = 20;
    return options;
}

firebreak::solver::ModelResult solve_base(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::cuts::SeparatorCutOptions* separator = nullptr,
    const firebreak::cuts::DominatorCutOptions* dominator = nullptr) {
    firebreak::solver::FppSaaCplexModel model;
    return model.solve(opt, 30.0, 0.0, 1, false, nullptr, dominator, separator);
}

firebreak::solver::ModelResult solve_cut(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::cuts::SeparatorCutOptions* separator = nullptr,
    const firebreak::cuts::DominatorCutOptions* dominator = nullptr) {
    firebreak::solver::FppCutReachabilityCplexModel model;
    return model.solve(opt, 30.0, 0.0, 1, false, nullptr, dominator, separator);
}

void assert_separator_enabled_result(const firebreak::solver::ModelResult& result) {
    assert(result.separator_cuts_enabled);
    assert(result.separator_min_cut_calls >= 0);
    assert(result.separator_cuts_added >= 0);
    assert(result.separator_time_sec >= 0.0);
}

void assert_base_and_cut_preserve_objective(
    const firebreak::opt::OptimizationInstance& opt,
    double expected_objective) {
    const auto sep = separator_options();

    const auto base = solve_base(opt);
    const auto base_sep = solve_base(opt, &sep);
    const auto cut = solve_cut(opt);
    const auto cut_sep = solve_cut(opt, &sep);

    assert_close(base.objective_value, expected_objective);
    assert_close(base_sep.objective_value, expected_objective);
    assert_close(cut.objective_value, expected_objective);
    assert_close(cut_sep.objective_value, expected_objective);
    assert_close(base.objective_value, base_sep.objective_value);
    assert_close(base.objective_value, cut.objective_value);
    assert_close(base.objective_value, cut_sep.objective_value);
    assert_separator_enabled_result(base_sep);
    assert_separator_enabled_result(cut_sep);
}

void test_callback_can_be_enabled() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1);
    const auto sep = separator_options();
    const auto result = solve_base(opt, &sep);
    assert_separator_enabled_result(result);
    assert_close(result.objective_value, 1.0);
}

void test_objective_preservation_chain_branching_parallel() {
    const auto chain = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    assert_base_and_cut_preserve_objective(chain, 1.0);

    const auto branching = make_opt_instance({1, 2, 3, 4, 5}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{2, 4, 3, 5},
    }, 1);
    assert_base_and_cut_preserve_objective(branching, 3.0);

    const auto parallel = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    assert_base_and_cut_preserve_objective(parallel, 3.0);
}

void test_dominator_compatibility() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);

    const auto sep = separator_options();
    const auto dom = dominator_options();
    const auto base_dom = solve_base(opt, nullptr, &dom);
    const auto base_sep = solve_base(opt, &sep);
    const auto base_both = solve_base(opt, &sep, &dom);
    const auto cut_both = solve_cut(opt, &sep, &dom);

    assert_close(base_dom.objective_value, 1.0);
    assert_close(base_sep.objective_value, 1.0);
    assert_close(base_both.objective_value, 1.0);
    assert_close(cut_both.objective_value, 1.0);
    assert(base_both.dominator_cuts_added > 0);
    assert_separator_enabled_result(base_both);
    assert_separator_enabled_result(cut_both);
}

void test_root_convention() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 1);
    assert_base_and_cut_preserve_objective(opt, 1.0);
}

void test_noneligible_nodes() {
    const auto opt = make_opt_instance({1, 2, 3}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 2, 2, 3},
    }, 1, {2}, true);
    assert_base_and_cut_preserve_objective(opt, 2.0);
}

void test_callback_invocation_on_controlled_relaxation() {
    const auto opt = make_opt_instance({1, 2, 3, 4}, {
        firebreak::opt::CompactArc{0, 1, 1, 2},
        firebreak::opt::CompactArc{1, 3, 2, 4},
        firebreak::opt::CompactArc{0, 2, 1, 3},
        firebreak::opt::CompactArc{2, 3, 3, 4},
    }, 2);

    IloEnv env;
    try {
        IloModel model(env);
        IloBoolVarArray y(env, 3);
        y[0].setName("y_1");
        y[1].setName("y_2");
        y[2].setName("y_3");

        IloNumVarArray x(env, 4, 0.0, 1.0, ILOFLOAT);
        x[0].setName("x_0");
        x[1].setName("x_1");
        x[2].setName("x_2");
        x[3].setName("x_3");
        for (IloInt i = 0; i < x.getSize(); ++i) {
            model.add(x[i] >= 0.0);
        }

        IloExpr budget(env);
        budget += y[0] + y[1] + y[2];
        model.add(budget <= 1.2);
        budget.end();

        IloExpr objective(env);
        objective += x[3] + y[0] + y[1];
        model.add(IloMaximize(env, objective));
        objective.end();

        IloCplex cplex(model);
        cplex.setOut(env.getNullStream());
        cplex.setWarning(env.getNullStream());
        cplex.setParam(IloCplex::Param::Threads, 1);
        // Test-only settings keep this toy MIP in branch-and-cut long enough
        // to invoke the generic relaxation callback.
        cplex.setParam(IloCplex::Param::Preprocessing::Presolve, IloFalse);
        cplex.setParam(IloCplex::Param::MIP::Strategy::Search, IloCplex::Traditional);
        cplex.setParam(IloCplex::Param::MIP::Strategy::HeuristicFreq, -1);

        firebreak::cuts::SeparatorVariableAccess access;
        access.y_vars = y.toNumVarArray();
        access.y_position_by_node = {-1, 0, 1, 2};
        access.x_vars_by_scenario = {x};
        access.x_position_by_scenario = {{0, 1, 2, 3}};

        auto options = separator_options();
        firebreak::cuts::SeparatorContextCallback callback(opt, options, access);
        cplex.use(&callback, IloCplex::Callback::Context::Id::Relaxation);

        const bool solved = cplex.solve();
        assert(solved);
        assert(callback.stats().callback_invocations > 0);
        assert(callback.stats().min_cut_calls > 0);
        assert(callback.stats().cuts_added > 0);
        env.end();
    } catch (const IloException& e) {
        std::cerr << "Controlled callback diagnostic failed with IloException: "
                  << e << "\n";
        env.end();
        throw;
    } catch (...) {
        env.end();
        throw;
    }
}
#endif

}  // namespace

int main() {
    test_direct_chain_violation();
    test_direct_parallel_path_violation();
    test_direct_noneligible_separator();
    test_direct_root_target_skipped();
#ifdef FIREBREAK_WITH_CPLEX
    test_callback_can_be_enabled();
    test_objective_preservation_chain_branching_parallel();
    test_dominator_compatibility();
    test_root_convention();
    test_noneligible_nodes();
    test_callback_invocation_on_controlled_relaxation();
    std::cout << "Separator context callback tests passed.\n";
#else
    std::cout << "Separator core tests passed; CPLEX callback tests skipped because CPLEX support is not enabled.\n";
#endif
    return 0;
}
