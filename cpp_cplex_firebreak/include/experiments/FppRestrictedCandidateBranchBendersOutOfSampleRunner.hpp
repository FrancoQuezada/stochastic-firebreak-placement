#pragma once

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"
#include "benders/FppStrengthening.hpp"
#include "risk/RiskMeasure.hpp"

namespace firebreak::experiments {

struct FppRestrictedCandidateBranchBendersOutOfSampleOptions {
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<int> train_ids;
    std::vector<int> test_ids;
    bool use_generated_split = false;
    unsigned int seed = 0;
    std::size_t train_count = 0;
    std::size_t test_count = 0;
    double alpha = -1.0;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    double tolerance = 1.0e-6;
    bool verbose = false;
    risk::RiskMeasureConfig risk_config;
    bool use_lifted_lower_bounds = false;
    bool use_root_user_cuts = false;
    int root_user_cut_max_rounds = 1;
    double root_user_cut_tolerance = std::numeric_limits<double>::quiet_NaN();
    benders::FppCombinatorialBendersOptions combinatorial_options;
    benders::FppStrengtheningOptions strengthening_options;
    std::vector<int> initial_active_candidates;
    std::string initial_candidate_policy;
    int initial_candidate_size = -1;
    std::string activation_policy = "none";
    int activation_batch_size = 0;
    int max_candidate_rounds = 0;
    std::string candidate_maintenance_policy = "none";
    int candidate_deactivation_batch_size = -1;
    int candidate_min_active_size = -1;
    int candidate_max_active_size = -1;
    int candidate_deactivation_min_age = 1;
    int candidate_reactivation_cooldown_rounds = 1;
    bool protect_selected_candidates = true;
    bool export_tail_score_diagnostics = false;
    std::string candidate_score_mode = "generic";
    double candidate_tail_score_gamma = 0.5;
    int candidate_tail_protection_size = -1;
    bool eventually_activate_all = true;
    bool restricted_exact_mode = true;
    bool restricted_heuristic_mode = false;
    int stop_after_candidate_rounds = -1;
    std::string run_id;
    std::filesystem::path output_json_path;
    std::filesystem::path output_csv_path;
    std::filesystem::path solution_json_path;
    std::filesystem::path solution_csv_path;
    std::filesystem::path weight_map_file;
};

class FppRestrictedCandidateBranchBendersOutOfSampleRunner {
public:
    int run(const FppRestrictedCandidateBranchBendersOutOfSampleOptions& options) const;
};

}  // namespace firebreak::experiments
