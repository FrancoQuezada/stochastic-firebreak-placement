#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "io/ExperimentResultWriter.hpp"

namespace {

namespace fs = std::filesystem;

fs::path temp_file(const std::string& name) {
    return fs::temp_directory_path() / name;
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    assert(in);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

int main() {
    firebreak::io::StandardExperimentResult result;
    result.run_id = "weighted_dpv_reporting";
    result.timestamp = "2026-07-20T00:00:00Z";
    result.landscape = "synthetic";
    result.method = "Greedy-DPV3";
    result.objective_metric = "greedy_DPV3_inverse_weighted_reachability";
    result.train_ids = {1};
    result.test_ids = {2};
    result.solver_status = "NotApplicable";
    result.dpv_weighted = true;
    result.dpv_variant = "greedy_dpv3_cumulative_inverse_frequency_distance";
    result.dpv_structural_definition = "active cumulative propagation graph";
    result.dpv_ignition_policy = "fpp_ignition_no_protection";
    result.dpv_weight_profile = "heterogeneous";
    result.dpv_weight_map_hash = "fnv1a64:test";
    result.dpv_scenario_aggregation = "cumulative_arc_frequency_sum_over_training_scenarios";
    result.dpv_normalization = "none";
    result.dpv_candidates_scored = 5;
    result.dpv_candidates_selected = 2;
    result.dpv_score_min = 1.0;
    result.dpv_score_max = 9.0;
    result.dpv_score_mean = 4.0;
    result.dpv_selected_score_sum = 12.0;
    result.dpv_score_precompute_time_sec = 0.25;
    result.dpv_selection_time_sec = 0.5;
    result.dpv_surrogate_objective = 12.0;
    result.dpv_greedy_iterations = 2;
    result.dpv_score_recomputations = 11;
    result.dpv_marginal_scores_evaluated = 11;
    result.dpv_overlap_value_removed = 0.0;

    const auto json_path = temp_file("firebreak_weighted_dpv_reporting.json");
    const auto csv_path = temp_file("firebreak_weighted_dpv_reporting.csv");
    fs::remove(json_path);
    fs::remove(csv_path);
    firebreak::io::write_experiment_result_json(json_path, result);
    firebreak::io::append_experiment_result_csv(csv_path, result);

    const auto json = read_text(json_path);
    const auto csv = read_text(csv_path);
    assert(json.find("\"dpv_weighted\": true") != std::string::npos);
    assert(json.find("\"dpv_ignition_policy\": \"fpp_ignition_no_protection\"") != std::string::npos);
    assert(csv.find("dpv_weighted,dpv_model_weighted,dpv_model_type") != std::string::npos);
    assert(csv.find("dpv_variant,dpv_structural_definition,dpv_ignition_policy") != std::string::npos);
    assert(csv.find("greedy_dpv3_cumulative_inverse_frequency_distance") != std::string::npos);
    assert(csv.find("fpp_ignition_no_protection") != std::string::npos);

    fs::remove(json_path);
    fs::remove(csv_path);

    std::cout << "Weighted DPV heuristic reporting test passed.\n";
    return 0;
}
