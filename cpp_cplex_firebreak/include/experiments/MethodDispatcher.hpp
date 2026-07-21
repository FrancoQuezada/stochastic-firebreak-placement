#pragma once

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "io/ExperimentResultWriter.hpp"
#include "benders/FppCombinatorialBenders.hpp"
#include "benders/FppProjectedLlbi.hpp"
#include "risk/RiskMeasure.hpp"

namespace firebreak::experiments {

struct MethodDispatchRequest {
    std::string experiment_id;
    int case_id = 0;
    std::string run_id;
    std::string method;
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::filesystem::path weight_map_file;
    std::filesystem::path output_dir;
    std::filesystem::path output_csv;
    double alpha = 0.0;
    std::vector<int> train_ids;
    std::vector<int> test_ids;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    std::string warm_start_policy = "none";
    bool verbose = false;
    std::string dpv_ignition_policy = "fpp-safe";

    std::string fpp_mode;
    std::string fpp_formulation = "base";
    bool enable_dominator_cuts = false;
    bool enable_separator_cuts = false;
    bool enable_greedy_warm_start = false;
    bool enable_local_search = false;

    bool sep_at_root = true;
    int sep_frequency_nodes = 50;
    int sep_max_scenarios_per_call = 10;
    int sep_max_nodes_per_scenario = 20;
    int sep_max_cuts_per_call = 100;
    double sep_min_violation = 1.0e-5;
    int sep_max_cut_cardinality = 50;

    int candidate_pool_size_multiplier = 5;
    int candidate_pool_min_size = 50;
    bool enable_greedy_exact_marginal = true;
    int local_search_max_iterations = 1000;
    double local_search_time_limit_sec = 60.0;

    int max_aggregate_dominator_cuts_per_scenario = 50;
    int max_individual_dominator_cuts_per_scenario = 100;

    int root_user_cut_max_rounds = 1;
    double root_user_cut_tolerance = std::numeric_limits<double>::quiet_NaN();
    bool use_coverage_llbi = false;
    bool use_path_llbi = false;
    int path_llbi_max_paths_per_node = 8;
    benders::FppProjectedLlbiOptions projected_llbi_options;
    bool use_global_dominance_preprocessing = false;
    bool use_conditional_zero_benefit_fixing = false;
    benders::FppCombinatorialBendersOptions combinatorial_options;
    risk::RiskMeasureConfig risk_config;
    bool risk_measure_specified = false;
    bool cvar_beta_specified = false;
    bool cvar_lambda_specified = false;
};

class MethodDispatcher {
public:
    io::StandardExperimentResult run_method(const MethodDispatchRequest& request) const;
};

}  // namespace firebreak::experiments
