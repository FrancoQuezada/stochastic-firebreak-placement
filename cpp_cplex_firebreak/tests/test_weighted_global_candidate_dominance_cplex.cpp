#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppBranchBendersSolver.hpp"
#include "benders/FppStrengthening.hpp"
#include "core/LandscapeWeightMap.hpp"
#include "risk/RiskMeasure.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_dominated_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_global_dominance_cplex";
    opt.alpha = 1.0;
    opt.n_cells = 4;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {1, 2, 3};
    opt.eligible_original_nodes = {2, 3, 4};

    firebreak::opt::OptimizationScenario first;
    first.scenario_id = 1;
    first.probability = 0.5;
    first.ignition_index = 0;
    first.ignition_original_node = 1;
    first.observed_node_indices = {0, 1, 2, 3};
    first.arcs = {
        {0, 1, 1, 2},
        {1, 2, 2, 3},
        {2, 3, 3, 4},
    };

    firebreak::opt::OptimizationScenario second = first;
    second.scenario_id = 2;

    opt.scenarios = {first, second};
    opt.scenario_probabilities = {0.5, 0.5};
    opt.total_arcs = first.arcs.size() + second.arcs.size();

    const std::vector<firebreak::core::LandscapeWeightRecord> records = {
        {1, 1.0, 1.0, 0},
        {2, 0.01, 0.01, 0},
        {3, 100.0, 100.0, 1},
        {4, 50.0, 50.0, 1},
    };
    opt.cell_weight_map =
        firebreak::core::make_landscape_weight_map("heterogeneous", 603, false, records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

#ifdef FIREBREAK_WITH_CPLEX
firebreak::solver::ModelResult solve_callback(
    const firebreak::opt::OptimizationInstance& opt,
    const firebreak::risk::RiskMeasureConfig& risk_config) {
    firebreak::benders::FppBranchBendersSolver solver;
    firebreak::benders::FppBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.risk_config = risk_config;
    return solver.solve(opt, options);
}

void compare_full_and_dominated_reduced(const firebreak::risk::RiskMeasureConfig& risk_config) {
    const auto full = make_dominated_instance();
    const auto dominance =
        firebreak::benders::apply_fpp_global_dominance_preprocessing(full, true);
    assert(dominance.enabled);
    assert(dominance.structural_weight_safe);
    assert(dominance.original_candidate_count == 3);
    assert(dominance.candidates_removed == 2);
    assert(dominance.post_candidate_count == 1);
    assert((dominance.kept_candidate_compact_nodes == std::vector<int>{1}));

    const auto full_result = solve_callback(full, risk_config);
    const auto reduced_result = solve_callback(dominance.reduced_instance, risk_config);

    assert(full_result.status == "Optimal");
    assert(reduced_result.status == "Optimal");
    assert_close(reduced_result.objective_value, full_result.objective_value);
    assert((reduced_result.selected_firebreak_indices == std::vector<int>{1}));
}

void test_expected_cvar_mean_cvar_equivalence() {
    firebreak::risk::RiskMeasureConfig expected;
    compare_full_and_dominated_reduced(expected);

    firebreak::risk::RiskMeasureConfig cvar;
    cvar.type = firebreak::risk::RiskMeasureType::CVaR;
    cvar.cvarBeta = 0.5;
    compare_full_and_dominated_reduced(cvar);

    firebreak::risk::RiskMeasureConfig mean_cvar;
    mean_cvar.type = firebreak::risk::RiskMeasureType::MeanCVaR;
    mean_cvar.cvarBeta = 0.5;
    mean_cvar.cvarLambda = 0.5;
    compare_full_and_dominated_reduced(mean_cvar);
}
#endif

}  // namespace

int main() {
#ifdef FIREBREAK_WITH_CPLEX
    test_expected_cvar_mean_cvar_equivalence();
    std::cout << "All weighted global candidate dominance CPLEX tests passed.\n";
#else
    std::cout << "Skipping weighted global candidate dominance CPLEX tests because CPLEX is not enabled.\n";
#endif
    return 0;
}
