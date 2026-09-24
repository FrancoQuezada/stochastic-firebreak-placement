#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "risk/RiskMeasure.hpp"
#include "benders/FppCombinatorialBenders.hpp"
#include "benders/FppProjectedLlbi.hpp"

namespace firebreak::experiments {

struct BatchExperimentConfig {
    std::string experiment_name;
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<double> alpha_values;
    std::vector<std::size_t> train_counts;
    std::size_t test_count = 0;
    std::size_t num_cases = 0;
    unsigned int seed_base = 0;
    std::vector<std::string> methods;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    std::filesystem::path output_csv;
    std::filesystem::path output_dir;
    std::string warm_start_policy = "none";
    bool resume_existing = true;
    bool shared_splits = false;
    std::filesystem::path split_dir;

    std::vector<std::string> fpp_modes;
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
    bool use_combinatorial_benders = false;
    benders::FppCombinatorialBendersOptions combinatorial_options;
    risk::RiskMeasureConfig risk_config;
    bool risk_measure_specified = false;
    bool cvar_beta_specified = false;
    bool cvar_lambda_specified = false;

    // Phase 8B: canonical weight-map registry metadata. `weight_map_file` is the only
    // field that changes solver behavior; the rest are reproducibility metadata that
    // propagate additively into MethodDispatchRequest/StandardExperimentResult and into
    // the resume-key so a weighted row can never collide with a legacy homogeneous one.
    // Missing/empty fields (the default) resolve to legacy homogeneous behavior.
    std::filesystem::path weight_map_file;
    std::string weight_profile;
    int weight_replicate = 0;
    std::uint64_t weight_generation_seed = 0;
    int weight_generator_version = 0;
    std::string canonical_landscape_id;
    std::string paired_landscape_id;
    std::string weight_map_hash;
    std::string weight_source_universe_hash;
    std::string paired_reburn_instance_id;
    bool paired_evaluation_enabled = false;
};

struct FppModeSettings {
    std::string mode;
    std::string formulation = "base";
    bool enable_greedy_warm_start = false;
    bool enable_dominator_cuts = false;
    bool enable_separator_cuts = false;
    bool enable_local_search = false;
};

struct DpvBranchBendersVariantSettings {
    bool is_branch_benders = false;
    bool use_lifted_lower_bounds = false;
    bool use_root_user_cuts = false;
    int root_user_cut_max_rounds = 1;
};

struct FppMethodVariantSettings {
    bool is_fpp_solver = false;
    bool is_fpp_saa = false;
    bool is_fpp_benders = false;
    bool is_fpp_branch_benders = false;
    bool is_fpp_restricted_branch_benders = false;
    bool use_lifted_lower_bounds = false;
    bool use_root_user_cuts = false;
    bool use_coverage_llbi = false;
    bool use_path_llbi = false;
    benders::FppProjectedLlbiOptions projected_llbi_options;
    bool use_global_dominance_preprocessing = false;
    bool use_conditional_zero_benefit_fixing = false;
    bool use_combinatorial_benders = false;
    benders::FppCombinatorialBendersOptions combinatorial_options;
    risk::RiskMeasureConfig risk_config;
};

const std::vector<std::string>& supported_batch_methods();
const std::vector<std::string>& supported_warm_start_policies();
const std::vector<std::string>& supported_fpp_formulations();
const std::vector<std::string>& supported_fpp_modes();

std::vector<double> parse_alpha_list(const std::string& value);
std::vector<std::size_t> parse_count_list(const std::string& value, const std::string& label);
std::vector<std::string> parse_batch_method_list(const std::string& value);
std::vector<std::string> parse_fpp_mode_list(const std::string& value);
std::string normalize_batch_method_name(const std::string& value);
std::string normalize_warm_start_policy(const std::string& value);
std::string normalize_fpp_formulation(const std::string& value);
std::string normalize_fpp_mode(const std::string& value);
FppModeSettings fpp_mode_settings(const std::string& mode);
DpvBranchBendersVariantSettings dpv_branch_benders_variant_settings(const std::string& method);
bool is_dpv_branch_benders_method(const std::string& method);
FppMethodVariantSettings fpp_method_variant_settings(const std::string& method);
bool is_fpp_solver_method(const std::string& method);
std::string fpp_mode_name_from_settings(
    const std::string& formulation,
    bool enable_greedy_warm_start,
    bool enable_dominator_cuts,
    bool enable_separator_cuts,
    bool enable_local_search);
std::string fpp_enhancement_config_summary(const BatchExperimentConfig& config);

void validate_batch_experiment_config(const BatchExperimentConfig& config);

}  // namespace firebreak::experiments
