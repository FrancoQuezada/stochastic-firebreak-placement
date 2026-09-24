#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "benders/FppBranchBendersSolver.hpp"
#include "benders/FppRestrictedCandidateBranchBendersSolver.hpp"
#include "core/LandscapeWeightMap.hpp"

namespace {

void assert_close(double actual, double expected, double tolerance = 1.0e-6) {
    assert(std::fabs(actual - expected) <= tolerance);
}

firebreak::opt::OptimizationInstance make_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "weighted_conditional_fixing_cplex";
    opt.alpha = 1.0;
    opt.n_cells = 5;
    opt.budget = 2;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4, 5});
    opt.eligible_indices = {1, 2, 3, 4};
    opt.eligible_original_nodes = {2, 3, 4, 5};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 1;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3, 4};
    scenario.arcs = {
        {0, 1, 1, 2},
        {1, 2, 2, 3},
        {0, 3, 1, 4},
        {3, 4, 4, 5},
    };
    opt.scenarios = {scenario};
    opt.scenario_probabilities = {1.0};
    opt.total_arcs = scenario.arcs.size();

    const std::vector<firebreak::core::LandscapeWeightRecord> records = {
        {1, 1.0, 1.0, 0},
        {2, 0.01, 0.01, 0},
        {3, 100.0, 100.0, 1},
        {4, 2.0, 2.0, 1},
        {5, 50.0, 50.0, 1},
    };
    opt.cell_weight_map =
        firebreak::core::make_landscape_weight_map("heterogeneous", 604, false, records);
    opt.compact_cell_weights =
        firebreak::core::build_compact_weight_vector(opt.cell_weight_map, opt.node_mapper);
    return opt;
}

#ifdef FIREBREAK_WITH_CPLEX
void test_callback_conditional_diagnostics_preserve_objective() {
    const auto opt = make_instance();
    firebreak::benders::FppBranchBendersSolver solver;

    firebreak::benders::FppBranchBendersOptions base;
    base.tolerance = 1.0e-7;
    base.time_limit_seconds = 30.0;
    base.mip_gap = 0.0;
    base.threads = 1;

    auto fixing = base;
    fixing.strengthening_options.use_conditional_zero_benefit_fixing = true;

    const auto base_result = solver.solve(opt, base);
    const auto fixing_result = solver.solve(opt, fixing);

    assert(base_result.status == "Optimal");
    assert(fixing_result.status == "Optimal");
    assert_close(fixing_result.objective_value, base_result.objective_value);
    assert(fixing_result.conditional_zero_benefit_enabled);
    assert(fixing_result.conditional_zero_benefit_structural_weight_safe);
    assert(fixing_result.conditional_zero_benefit_fixings_applied == 0);
    assert(fixing_result.conditional_zero_benefit_variables_fixed_zero == 0);
}

void test_restricted_conditional_diagnostics_preserve_objective() {
    const auto opt = make_instance();
    firebreak::benders::FppBranchBendersSolver callback_solver;
    firebreak::benders::FppBranchBendersOptions callback_options;
    callback_options.tolerance = 1.0e-7;
    callback_options.time_limit_seconds = 30.0;
    callback_options.mip_gap = 0.0;
    callback_options.threads = 1;
    const auto reference = callback_solver.solve(opt, callback_options);

    firebreak::benders::FppRestrictedCandidateBranchBendersSolver restricted_solver;
    firebreak::benders::FppRestrictedCandidateBranchBendersOptions options;
    options.tolerance = 1.0e-7;
    options.time_limit_seconds = 30.0;
    options.mip_gap = 0.0;
    options.threads = 1;
    options.initial_active_candidates = {1, 3};
    options.initial_candidate_policy = "explicit-list";
    options.activation_policy = "benders-coefficients";
    options.activation_batch_size = 1;
    options.max_candidate_rounds = 1;
    options.eventually_activate_all = true;
    options.strengthening_options.use_conditional_zero_benefit_fixing = true;

    const auto result = restricted_solver.solve(opt, options);
    assert(reference.status == "Optimal");
    assert(result.status == "Optimal");
    assert(result.final_lower_bound_is_global);
    assert_close(result.final_full_objective, reference.objective_value);
    assert(result.final_stage_result.conditional_zero_benefit_enabled);
    assert(result.final_stage_result.conditional_zero_benefit_structural_weight_safe);
    assert(result.final_stage_result.conditional_zero_benefit_fixings_applied == 0);
}
#endif

}  // namespace

int main() {
#ifdef FIREBREAK_WITH_CPLEX
    test_callback_conditional_diagnostics_preserve_objective();
    test_restricted_conditional_diagnostics_preserve_objective();
    std::cout << "All weighted conditional zero-benefit CPLEX tests passed.\n";
#else
    std::cout << "Skipping weighted conditional zero-benefit CPLEX tests because CPLEX is not enabled.\n";
#endif
    return 0;
}
