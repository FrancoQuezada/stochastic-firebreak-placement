#include "benders/CvarTailScoreDiagnostics.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "benders/BendersCut.hpp"
#include "benders/RestrictedCandidateManager.hpp"
#include "io/ExperimentResultWriter.hpp"

namespace {

using firebreak::benders::BendersCut;
using firebreak::benders::CvarTailScoreDiagnosticsInput;
using firebreak::benders::computeCvarTailScoreRoundDiagnostics;

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-9);
}

BendersCut make_cut(
    int scenario_id,
    std::vector<std::pair<int, double>> coefficients) {
    BendersCut cut;
    cut.scenario_id = scenario_id;
    cut.rhs_constant = 0.0;
    cut.coefficients_by_compact_index = std::move(coefficients);
    return cut;
}

CvarTailScoreDiagnosticsInput base_input() {
    CvarTailScoreDiagnosticsInput input;
    input.round_index = 1;
    input.risk_measure = "cvar";
    input.cvar_beta = 0.5;
    input.risk_threshold = 50.0;
    input.candidate_count = 4;
    input.eligible_compact_indices = {10, 11, 12, 13};
    input.scenario_probability_by_id = {{1, 0.5}, {2, 0.5}};
    input.accumulated_cuts = {
        make_cut(1, {{10, -10.0}, {11, -8.0}, {12, -1.0}}),
        make_cut(2, {{10, -1.0}, {11, -1.0}, {12, -6.0}, {13, -5.0}}),
    };
    input.recent_cuts = {input.accumulated_cuts.front()};
    input.scenario_losses_by_id = {{1, 100.0}, {2, 10.0}};
    input.cvar_excess_by_id = {{1, 25.0}, {2, 0.0}};
    input.active_candidates_before_round = {1, 2, 3};
    input.active_candidates_after_round = {0, 1, 2};
    input.activated_candidates = {0, 2};
    input.deactivated_candidates = {3};
    input.selected_candidates = {1};
    input.protected_selected_candidates = {1};
    input.top_k = 2;
    return input;
}

void test_top_k_overlap_and_tail_scores() {
    const auto diagnostics = computeCvarTailScoreRoundDiagnostics(base_input());
    assert(diagnostics.tail_scenario_count == 1);
    assert((diagnostics.tail_scenario_ids == std::vector<int>{1}));
    assert(diagnostics.top_generic_candidates.size() == 2);
    assert(diagnostics.top_tail_empirical_candidates.size() == 2);
    assert(diagnostics.top_generic_candidates[0].candidate_id == 0);
    assert(diagnostics.top_tail_empirical_candidates[0].candidate_id == 0);
    assert(diagnostics.top_k_overlap_generic_tail == 2);
    assert(diagnostics.activated_tail_top_k_overlap == 1);
    assert(diagnostics.deactivated_tail_bottom_k_overlap == 1);
    assert(diagnostics.deactivated_tail_top_k_warning_count == 0);
    assert(diagnostics.selected_tail_top_k_overlap == 1);
    assert_close(diagnostics.top_tail_empirical_candidates[0].score, 5.0);
}

void test_missing_tail_information_is_graceful() {
    auto input = base_input();
    input.scenario_losses_by_id.clear();
    input.cvar_excess_by_id.clear();
    const auto diagnostics = computeCvarTailScoreRoundDiagnostics(input);
    assert(diagnostics.tail_scenario_count == 0);
    assert(!diagnostics.notes.empty());
    assert(diagnostics.top_tail_empirical_candidates.size() == 2);
}

void test_diagnostics_do_not_change_manager_activation() {
    firebreak::benders::RestrictedCandidateManager manager(4, 2, {0, 1});
    auto input = base_input();
    input.active_candidates_before_round = manager.activeCandidates();
    input.active_candidates_after_round = manager.activeCandidates();
    input.activated_candidates.clear();
    input.deactivated_candidates.clear();
    (void)computeCvarTailScoreRoundDiagnostics(input);
    assert((manager.activeCandidates() == std::vector<int>{0, 1}));
    const auto activated = manager.activateTopK({{2, 10.0}, {3, 5.0}}, 1);
    assert((activated == std::vector<int>{2}));
}

void test_candidate_event_warnings() {
    const auto diagnostics = computeCvarTailScoreRoundDiagnostics(base_input());
    bool found_protected_selected = false;
    bool found_poor_activation = false;
    for (const auto& event : diagnostics.candidate_events) {
        if (event.candidate_id == 1) {
            found_protected_selected = true;
            assert(event.was_selected);
            assert(event.was_protected);
            assert(event.warning.find("protected_selected_tail_top_k") != std::string::npos);
        }
        if (event.candidate_id == 2) {
            found_poor_activation = true;
            assert(event.was_activated);
            assert(event.warning.find("activated_poor_tail_rank") != std::string::npos);
        }
    }
    assert(found_protected_selected);
    assert(found_poor_activation);
}

void test_json_writer_emits_tail_score_section() {
    firebreak::io::StandardExperimentResult result;
    result.run_id = "tail_score_json_fixture";
    result.timestamp = "2026-05-24T00:00:00Z";
    result.landscape = "Unit";
    result.method = "FPP-Restricted-Branch-Benders-CVaR-Heuristic";
    result.restricted_candidate_enabled = true;
    result.restricted_candidate_tail_score_diagnostics_enabled = true;
    result.restricted_candidate_tail_score_diagnostics.push_back(
        computeCvarTailScoreRoundDiagnostics(base_input()));

    const auto path =
        std::filesystem::temp_directory_path() / "tail_score_json_fixture.json";
    firebreak::io::write_experiment_result_json(path, result);

    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    assert(json.find("\"tail_score_diagnostics_enabled\": true") != std::string::npos);
    assert(json.find("\"tail_score_diagnostics\"") != std::string::npos);
    assert(json.find("\"candidate_events\"") != std::string::npos);
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    test_top_k_overlap_and_tail_scores();
    test_missing_tail_information_is_graceful();
    test_diagnostics_do_not_change_manager_activation();
    test_candidate_event_warnings();
    test_json_writer_emits_tail_score_section();
    std::cout << "All CVaR tail-score diagnostic tests passed.\n";
    return 0;
}
