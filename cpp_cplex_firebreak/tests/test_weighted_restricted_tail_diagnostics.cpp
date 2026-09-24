#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <vector>

#include "benders/CvarTailScoreDiagnostics.hpp"

namespace {

firebreak::benders::BendersCut make_cut(
    int scenario_id,
    std::vector<std::pair<int, double>> coefficients) {
    firebreak::benders::BendersCut cut;
    cut.scenario_id = scenario_id;
    cut.rhs_constant = 0.0;
    cut.coefficients_by_compact_index = std::move(coefficients);
    return cut;
}

void test_weighted_tail_uses_weighted_losses_and_hash() {
    firebreak::benders::CvarTailScoreDiagnosticsInput input;
    input.round_index = 2;
    input.risk_measure = "cvar";
    input.cvar_beta = 0.5;
    input.weighted = true;
    input.weight_profile = "heterogeneous";
    input.weight_map_hash = "fnv1a64:weighted-map";
    input.risk_threshold = 20.0;
    input.candidate_count = 2;
    input.eligible_compact_indices = {10, 11};
    input.scenario_probability_by_id = {{1, 0.5}, {2, 0.5}};
    input.accumulated_cuts = {
        make_cut(1, {{10, -1.0}, {11, -1.0}}),
        make_cut(2, {{10, -10.0}, {11, -20.0}}),
    };
    input.recent_cuts = input.accumulated_cuts;
    input.scenario_losses_by_id = {
        {1, 2.0},
        {2, 30.0},
    };
    input.cvar_excess_by_id = {
        {1, 0.0},
        {2, 10.0},
    };
    input.active_candidates_before_round = {0, 1};
    input.active_candidates_after_round = {0};
    input.deactivated_candidates = {1};
    input.top_k = 1;

    const auto diagnostics =
        firebreak::benders::computeCvarTailScoreRoundDiagnostics(input);

    assert(diagnostics.weighted);
    assert(diagnostics.weight_profile == "heterogeneous");
    assert(diagnostics.weight_map_hash == "fnv1a64:weighted-map");
    assert((diagnostics.tail_scenario_ids == std::vector<int>{2}));
    assert(diagnostics.scenario_diagnostics.size() == 2);
    assert(!diagnostics.scenario_diagnostics[0].tail_membership);
    assert(diagnostics.scenario_diagnostics[1].tail_membership);
    assert(std::fabs(diagnostics.scenario_diagnostics[1].weighted_loss - 30.0) <= 1.0e-12);
    assert(std::fabs(diagnostics.scenario_diagnostics[1].weighted_var_threshold - 20.0) <= 1.0e-12);
    assert(std::fabs(diagnostics.scenario_diagnostics[1].tail_excess - 10.0) <= 1.0e-12);
}

void test_cvar_ties_are_deterministic_by_scenario_id() {
    firebreak::benders::CvarTailScoreDiagnosticsInput input;
    input.risk_measure = "cvar";
    input.cvar_beta = 0.5;
    input.candidate_count = 1;
    input.eligible_compact_indices = {10};
    input.scenario_probability_by_id = {{1, 0.5}, {2, 0.5}};
    input.accumulated_cuts = {
        make_cut(1, {{10, -1.0}}),
        make_cut(2, {{10, -2.0}}),
    };
    input.scenario_losses_by_id = {{2, 5.0}, {1, 5.0}};
    input.active_candidates_before_round = {0};
    input.active_candidates_after_round = {0};
    input.top_k = 1;

    const auto diagnostics =
        firebreak::benders::computeCvarTailScoreRoundDiagnostics(input);
    assert((diagnostics.tail_scenario_ids == std::vector<int>{1}));
}

}  // namespace

int main() {
    test_weighted_tail_uses_weighted_losses_and_hash();
    test_cvar_ties_are_deterministic_by_scenario_id();
    std::cout << "All weighted restricted tail diagnostic tests passed.\n";
    return 0;
}
