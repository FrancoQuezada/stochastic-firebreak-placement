#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "benders/FppBendersSolver.hpp"
#include "benders/FppBranchBendersSolver.hpp"
#include "benders/FppPersistentScenarioSubproblemManager.hpp"
#include "benders/FppRestrictedCandidateBranchBendersSolver.hpp"
#include "benders/RestrictedCandidateCutPool.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "eval/FppRecourseEvaluator.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/FppSaaCplexModel.hpp"
#include "solver/FppWeightedLossUtils.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

template <typename Fn>
void assert_throws(Fn fn, const char* label) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        std::cerr << "Expected exception was not thrown: " << label << "\n";
    }
    assert(threw);
}

firebreak::opt::OptimizationInstance make_weighted_branch_instance(bool homogeneous = false) {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_restricted_branch";
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
        73,
        false,
        records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

firebreak::opt::OptimizationInstance make_weighted_risk_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_restricted_risk";
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
        74,
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
        if (eligible_y[pos] != 0) {
            selected.push_back(opt.eligible_indices[pos]);
        }
    }
    firebreak::eval::FppRecourseEvaluator evaluator(opt);
    return evaluator.evaluate(selected, false).expected_weighted_burn_loss;
}

firebreak::benders::FppRestrictedCandidateBranchBendersOptions restricted_options(
    const firebreak::risk::RiskMeasureConfig& risk_config,
    std::vector<int> initial_candidates = {0}) {
    firebreak::benders::FppRestrictedCandidateBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    options.initial_candidate_policy = "explicit-list";
    options.initial_active_candidates = std::move(initial_candidates);
    options.activation_policy = "none";
    options.eventually_activate_all = true;
    return options;
}

firebreak::solver::ModelResult solve_unrestricted_callback(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config,
    bool root_cuts = false) {
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
    return solver.solve(opt, options);
}

double final_restricted_objective(
    const firebreak::benders::FppRestrictedCandidateBranchBendersResult& result) {
    return std::isfinite(result.final_full_objective)
        ? result.final_full_objective
        : result.restricted_objective;
}

void compare_direct_explicit_callback_restricted(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config,
    bool root_cuts = false) {
    firebreak::solver::FppSaaCplexModel direct;
    const auto direct_result =
        direct.solve(opt, 30.0, 0.0, 1, false, nullptr, nullptr, nullptr, risk_config);

    firebreak::benders::FppBendersSolver explicit_solver;
    firebreak::benders::FppBendersOptions explicit_options;
    explicit_options.max_iterations = 20;
    explicit_options.tolerance = 1.0e-7;
    explicit_options.time_limit_seconds = 30.0;
    explicit_options.mip_gap = 0.0;
    explicit_options.threads = 1;
    explicit_options.risk_config = risk_config;
    const auto explicit_result = explicit_solver.solve(opt, explicit_options);

    const auto callback_result = solve_unrestricted_callback(opt, risk_config, root_cuts);

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver restricted_solver;
    const auto restricted_result =
        restricted_solver.solve(opt, restricted_options(risk_config, {0}));

    assert(direct_result.status == "Optimal");
    assert(callback_result.status == "Optimal");
    assert(restricted_result.status == "Optimal");
    assert(restricted_result.full_activation_performed);
    assert(restricted_result.eventually_activated_all);
    assert(restricted_result.active_candidate_count_final ==
           static_cast<int>(opt.eligible_indices.size()));
    assert_close(callback_result.objective_value, direct_result.objective_value);
    assert_close(callback_result.objective_value, explicit_result.objective_value);
    assert_close(final_restricted_objective(restricted_result), direct_result.objective_value);
    assert(restricted_result.final_stage_result.objective_metric.find("weighted_") == 0);
}

void test_expected_cvar_mean_cvar_equivalence() {
    compare_direct_explicit_callback_restricted(
        make_weighted_branch_instance(false),
        firebreak::risk::RiskMeasureConfig());

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    cvar.cvarLambda = 1.0;
    compare_direct_explicit_callback_restricted(make_weighted_risk_instance(), cvar);

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.5;
    compare_direct_explicit_callback_restricted(make_weighted_risk_instance(), mean_cvar);
}

void test_eventual_activation_finds_missing_weighted_optimum() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto result =
        solver.solve(opt, restricted_options(firebreak::risk::RiskMeasureConfig(), {0}));

    assert(result.initial_active_candidate_count == 1);
    assert(result.full_activation_performed);
    assert(result.eventually_activated_all);
    assert(result.active_candidate_count_final == 2);
    assert(!result.activation_history.empty());
    assert_close(final_restricted_objective(result), 4.0);
    assert(result.final_stage_result.selected_firebreak_original_nodes.size() == 1);
    assert(result.final_stage_result.selected_firebreak_original_nodes[0] == 5);
}

void test_cut_pool_keeps_inactive_candidate_coefficients() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppPersistentScenarioSubproblemManager manager(opt, false);
    const auto subproblem = manager.solveScenario(0, {1, 0});
    const int later_activated_compact = opt.eligible_indices[1];

    bool found_later_candidate = false;
    for (const auto& [compact_index, coefficient] :
         subproblem.benders_cut.coefficients_by_compact_index) {
        if (compact_index == later_activated_compact) {
            found_later_candidate = true;
            assert(std::isfinite(coefficient));
        }
    }
    assert(found_later_candidate);

    for (int y0 = 0; y0 <= 1; ++y0) {
        for (int y1 = 0; y1 <= 1; ++y1) {
            if (y0 + y1 > opt.budget) {
                continue;
            }
            const std::vector<int> ybar = {y0, y1};
            const double cut_rhs =
                subproblem.benders_cut.evaluateAt(compact_y_from_eligible(opt, ybar));
            const double exact = exact_weighted_recourse(opt, ybar);
            assert(cut_rhs <= exact + 1.0e-6);
        }
    }

    firebreak::benders::RestrictedCandidateCutPool cut_pool;
    cut_pool.setWeightMapHash(opt.cell_weight_map.deterministic_hash);
    assert(cut_pool.addCut(subproblem.benders_cut, 0, "restricted", 1));
    assert_throws(
        [&] { cut_pool.setWeightMapHash("different-weight-map-hash"); },
        "restricted candidate cut pool hash mismatch");

    firebreak::benders::RestrictedCandidateCutPool homogeneous_pool;
    assert(homogeneous_pool.addCut(subproblem.benders_cut, 0, "restricted", 1));
    assert_throws(
        [&] { homogeneous_pool.setWeightMapHash(opt.cell_weight_map.deterministic_hash); },
        "homogeneous cut pool reused for weighted run");
}

void test_homogeneous_regression() {
    auto implicit = make_weighted_branch_instance(true);
    implicit.cell_weight_map = firebreak::core::LandscapeWeightMap();
    implicit.compact_cell_weights.clear();
    const auto explicit_homogeneous = make_weighted_branch_instance(true);

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    const auto a = solver.solve(
        implicit,
        restricted_options(firebreak::risk::RiskMeasureConfig(), {0}));
    const auto b = solver.solve(
        explicit_homogeneous,
        restricted_options(firebreak::risk::RiskMeasureConfig(), {0}));
    assert_close(final_restricted_objective(a), final_restricted_objective(b));
    assert(a.final_stage_result.selected_firebreak_original_nodes ==
           b.final_stage_result.selected_firebreak_original_nodes);
}

void test_root_cuts_preserve_weighted_optimum() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;
    auto baseline_options = restricted_options(firebreak::risk::RiskMeasureConfig(), {0});
    auto root_options = baseline_options;
    root_options.use_root_user_cuts = true;
    root_options.root_user_cut_max_rounds = 2;
    root_options.root_user_cut_tolerance = 1.0e-7;

    const auto baseline = solver.solve(opt, baseline_options);
    const auto root = solver.solve(opt, root_options);
    assert_close(final_restricted_objective(root), final_restricted_objective(baseline));
    assert(root.final_stage_result.branch_benders_use_root_user_cuts);
    assert(root.full_activation_performed);
}

void test_unsupported_weighted_restricted_options_rejected() {
    const auto opt = make_weighted_branch_instance(false);
    firebreak::benders::FppRestrictedCandidateBranchBendersSolver solver;

    auto burn_frequency_initial =
        restricted_options(firebreak::risk::RiskMeasureConfig(), {0});
    burn_frequency_initial.initial_candidate_policy = "burn-frequency";
    burn_frequency_initial.initial_candidate_size = 1;
    assert_throws(
        [&] { (void)solver.solve(opt, burn_frequency_initial); },
        "burn-frequency initial policy");

    auto activation = restricted_options(firebreak::risk::RiskMeasureConfig(), {0});
    activation.activation_policy = "benders-coefficients";
    activation.activation_batch_size = 1;
    activation.max_candidate_rounds = 1;
    assert_throws(
        [&] { (void)solver.solve(opt, activation); },
        "benders-coefficients activation");

    auto llbi = restricted_options(firebreak::risk::RiskMeasureConfig(), {0});
    llbi.use_lifted_lower_bounds = true;
    assert_throws(
        [&] { (void)solver.solve(opt, llbi); },
        "lifted lower bounds");

    auto maintenance = restricted_options(firebreak::risk::RiskMeasureConfig(), {0});
    maintenance.activation_policy = "benders-coefficients";
    maintenance.activation_batch_size = 1;
    maintenance.max_candidate_rounds = 1;
    maintenance.restricted_heuristic_mode = true;
    maintenance.eventually_activate_all = false;
    maintenance.candidate_maintenance_policy = "benders-coefficients";
    assert_throws(
        [&] { (void)solver.solve(opt, maintenance); },
        "candidate maintenance");
}

}  // namespace

int main() {
#ifndef FIREBREAK_WITH_CPLEX
    std::cout << "Skipping weighted restricted FPP Branch-Benders tests because CPLEX is not enabled.\n";
    return 0;
#else
    test_expected_cvar_mean_cvar_equivalence();
    test_eventual_activation_finds_missing_weighted_optimum();
    test_cut_pool_keeps_inactive_candidate_coefficients();
    test_homogeneous_regression();
    test_root_cuts_preserve_weighted_optimum();
    test_unsupported_weighted_restricted_options_rejected();
    std::cout << "All weighted restricted FPP Branch-Benders tests passed.\n";
    return 0;
#endif
}
