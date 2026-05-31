#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace firebreak::experiments {

struct FppMasterLpDiagnosticOptions {
    std::string experiment_id = "sub20_alpha001_002_fpp_master_lp_relaxation";
    std::string case_id;
    unsigned int seed_base = 20260601;
    unsigned int seed = 0;
    std::string landscape;
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    std::vector<int> train_ids;
    double alpha = -1.0;
    std::string variant;
    int threads = 1;
    int path_llbi_max_paths_per_node = 8;
    int projected_llbi_root_rounds = 3;
    int projected_llbi_max_cuts_per_round = 100;
    double projected_llbi_violation_tolerance = 1.0e-6;
    int projected_llbi_cut_density_limit = 0;
    int projected_poly_max_cuts = 100000;
    std::filesystem::path output_json_path;
    std::filesystem::path output_csv_path;
};

class FppMasterLpDiagnosticRunner {
public:
    int run(const FppMasterLpDiagnosticOptions& options) const;
};

}  // namespace firebreak::experiments
