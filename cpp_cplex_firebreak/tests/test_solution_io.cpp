#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "io/ExperimentResultWriter.hpp"
#include "io/SolutionIO.hpp"

int main() {
    const auto base = std::filesystem::temp_directory_path() / "firebreak_solution_io_test";
    const auto csv_path = base / "solution.csv";
    const auto json_path = base / "solution.json";
    const auto result_csv_path = base / "result.csv";
    const auto result_json_path = base / "result.json";

    const std::vector<int> nodes{154, 174, 290, 328};
    firebreak::io::save_firebreak_solution_csv(csv_path, nodes);
    const auto loaded = firebreak::io::load_firebreak_solution_csv(csv_path);
    assert(loaded == nodes);

    firebreak::io::FirebreakSolutionRecord record;
    record.method = "FPP-SAA";
    record.landscape = "Sub20";
    record.alpha = 0.01;
    record.budget = 4;
    record.selected_firebreak_original_nodes = nodes;
    record.selected_firebreak_indices = {118, 138, 250, 287};
    firebreak::io::save_firebreak_solution_json(json_path, record);
    assert(std::filesystem::exists(json_path));

    firebreak::io::StandardExperimentResult result;
    result.run_id = "result_writer_test";
    result.timestamp = "2026-05-20T00:00:00Z";
    result.landscape = "Sub20";
    result.method = "FPP-SAA";
    result.objective_metric = "expected_burned_area";
    result.fpp_mode = "fpp_cut_dominator_separator_greedy";
    result.formulation = "cut";
    result.dominator_cuts_enabled = true;
    result.separator_cuts_enabled = true;
    result.greedy_warm_start_enabled = true;
    result.local_search_enabled = false;
    result.compact_node_count = 100;
    result.eligible_node_count = 80;
    result.total_observed_scenario_nodes = 200;
    result.total_scenario_arcs = 150;
    result.separator_callback_invocations = 3;
    result.explored_nodes = 12;
    result.dominator_aggregate_cuts_added = 4;
    result.heuristic_exact_evaluations = 5;
    result.heuristic_selected_count = 2;
    result.evaluator_objective = 10.0;
    result.evaluator_abs_diff = 0.0;
    result.evaluator_rel_diff = 0.0;
    result.train_empirical_cvar_burned_area = 15.0;
    result.test_empirical_cvar_burned_area = 25.0;
    result.validation_status = "pass";
    result.warm_start_used = true;
    result.mip_start_accepted = true;
    result.benders_status = "Optimal";
    result.benders_master_solve_time_sec = 0.1;
    result.benders_subproblem_time_sec = 0.2;
    result.benders_subproblems_solved = 2;
    result.benders_average_subproblem_time_sec = 0.1;
    result.benders_max_subproblem_time_sec = 0.12;
    result.branch_benders_enabled = true;
    result.branch_benders_candidate_callback_calls = 3;
    result.branch_benders_subproblems_attempted = 4;
    result.branch_benders_subproblems_solved = 4;
    result.branch_benders_subproblem_time_sec = 0.4;
    result.branch_benders_average_subproblem_time_sec = 0.1;
    result.branch_benders_max_subproblem_time_sec = 0.11;
    result.branch_benders_cut_construction_time_sec = 0.01;
    result.branch_benders_lazy_cut_insertion_time_sec = 0.02;
    result.branch_benders_violated_cuts = 2;
    result.branch_benders_nonviolated_cuts = 2;
    result.combinatorial_benders_enabled = true;
    result.combinatorial_benders_lift_mode = "heuristic";
    result.combinatorial_benders_scenario_order = "eta-desc";
    result.combinatorial_benders_cut_sampling_ratio = 0.1;
    result.combinatorial_benders_integer_cuts_added = 3;
    result.combinatorial_benders_fractional_cuts_added = 4;
    result.combinatorial_benders_initial_cuts_added = 5;
    result.combinatorial_benders_scenarios_checked = 6;
    result.combinatorial_benders_separation_time_sec = 0.07;
    result.combinatorial_benders_avg_paths_per_cut = 2.5;
    result.combinatorial_benders_avg_cut_nonzeros = 3.5;
    result.combinatorial_benders_num_violated_cuts = 7;
    result.coverage_llbi_enabled = true;
    result.coverage_llbi_num_zeta_vars = 7;
    result.coverage_llbi_num_constraints = 8;
    result.coverage_llbi_precompute_time_sec = 0.03;
    result.path_llbi_enabled = true;
    result.path_llbi_num_b_vars = 9;
    result.path_llbi_num_path_constraints = 10;
    result.path_llbi_num_paths_used = 11;
    result.path_llbi_precompute_time_sec = 0.04;
    result.global_dominance_enabled = true;
    result.global_dominance_candidates_removed = 1;
    result.global_dominance_equivalence_classes = 1;
    result.global_dominance_precompute_time_sec = 0.05;
    result.conditional_zero_benefit_enabled = true;
    result.conditional_zero_benefit_fixings_attempted = 12;
    result.conditional_zero_benefit_fixings_applied = 0;
    result.conditional_zero_benefit_time_sec = 0.06;
    firebreak::io::write_experiment_result_json(result_json_path, result);
    firebreak::io::append_experiment_result_csv(result_csv_path, result);
    assert(std::filesystem::exists(result_json_path));
    assert(std::filesystem::exists(result_csv_path));
    {
        std::ifstream in(result_csv_path);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(text.find("fpp_mode") != std::string::npos);
        assert(text.find("validation_status") != std::string::npos);
        assert(text.find("risk_measure") != std::string::npos);
        assert(text.find("train_cvar_burned_area") != std::string::npos);
        assert(text.find("mip_start_accepted") != std::string::npos);
        assert(text.find("explored_nodes") != std::string::npos);
        assert(text.find("benders_master_solve_time_sec") != std::string::npos);
        assert(text.find("branch_benders_subproblems_attempted") != std::string::npos);
        assert(text.find("branch_benders_average_subproblem_time_sec") != std::string::npos);
        assert(text.find("combinatorial_benders_enabled") != std::string::npos);
        assert(text.find("combinatorial_benders_scenario_order") != std::string::npos);
        assert(text.find("eta-desc") != std::string::npos);
        assert(text.find("combinatorial_benders_integer_cuts_added") != std::string::npos);
        assert(text.find("coverage_llbi_enabled") != std::string::npos);
        assert(text.find("path_llbi_num_paths_used") != std::string::npos);
        assert(text.find("global_dominance_candidates_removed") != std::string::npos);
        assert(text.find("conditional_zero_benefit_fixings_attempted") != std::string::npos);
        assert(text.find("fpp_cut_dominator_separator_greedy") != std::string::npos);
    }
    {
        std::ifstream in(result_json_path);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(text.find("\"master_solve_time_sec\"") != std::string::npos);
        assert(text.find("\"subproblems_attempted\"") != std::string::npos);
        assert(text.find("\"cut_construction_time_sec\"") != std::string::npos);
        assert(text.find("\"coverage_llbi_enabled\": true") != std::string::npos);
        assert(text.find("\"combinatorial_benders_enabled\": true") != std::string::npos);
        assert(text.find("\"combinatorial_benders_scenario_order\": \"eta-desc\"") != std::string::npos);
        assert(text.find("\"combinatorial_benders_integer_cuts_added\": 3") != std::string::npos);
        assert(text.find("\"path_llbi_num_paths_used\": 11") != std::string::npos);
        assert(text.find("\"global_dominance_candidates_removed\": 1") != std::string::npos);
        assert(text.find("\"conditional_zero_benefit_fixings_attempted\": 12") != std::string::npos);
    }

    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);
    std::filesystem::remove(result_csv_path);
    std::filesystem::remove(result_json_path);
    std::filesystem::remove(base);

    std::cout << "All solution IO tests passed.\n";
    return 0;
}
