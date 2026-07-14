#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "experiments/ExperimentManifest.hpp"

std::filesystem::path write_manifest(const std::string& name, const std::string& content) {
    const auto dir = std::filesystem::temp_directory_path() / "firebreak_manifest_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / name;
    std::ofstream out(path);
    out << content;
    return path;
}

void test_valid_manifest() {
    const auto path = write_manifest("valid.txt", R"(
# comment

experiment_name = test_manifest
landscape=Sub20
forest_path=../sample_test/data/CanadianFBP/Sub20
results_path=../sample_test/Sub20
alphas=0.01,0.02
train_counts=2,5
test_count=3
num_cases=2
seed_base=123
methods=FPP-SAA,FPP-Branch-Benders,DPV-SAA,Static-DPV,Greedy-DPV3
time_limit=60
mip_gap=0.001
threads=1
output_dir=results/batch/test_manifest
warm_start_policy=none
fpp_modes=fpp_base,fpp_cut_dominator_separator_greedy
shared_splits=true
split_dir=results/batch/shared/splits
fpp_formulation=base
enable_dominator_cuts=false
enable_separator_cuts=false
enable_greedy_warm_start=true
enable_local_search=false
sep_at_root=false
sep_frequency_nodes=25
sep_max_scenarios_per_call=4
sep_max_nodes_per_scenario=8
sep_max_cuts_per_call=12
sep_min_violation=0.0001
sep_max_cut_cardinality=9
candidate_pool_size_multiplier=7
candidate_pool_min_size=33
enable_greedy_exact_marginal=false
local_search_max_iterations=77
local_search_time_limit_sec=12.5
max_aggregate_dominator_cuts_per_scenario=11
max_individual_dominator_cuts_per_scenario=22
root_user_cut_max_rounds=2
root_user_cut_tolerance=0.000001
combinatorial_benders_scenario_order=eta-desc
cvar_beta=0.8
cvar_lambda=0.4
)");
    const auto manifest = firebreak::experiments::load_experiment_manifest(path);
    assert(manifest.config.experiment_name == "test_manifest");
    assert(manifest.config.landscape == "Sub20");
    assert(manifest.config.alpha_values.size() == 2);
    assert(manifest.config.train_counts.size() == 2);
    assert(manifest.config.methods.size() == 5);
    assert(manifest.config.output_csv == std::filesystem::path("results/batch/test_manifest") / "batch_results.csv");
    assert(manifest.config.shared_splits);
    assert(manifest.config.split_dir == std::filesystem::path("results/batch/shared/splits"));
    assert((manifest.config.fpp_modes == std::vector<std::string>{
        "fpp_base",
        "fpp_cut_dominator_separator_greedy",
    }));
    assert(manifest.config.fpp_formulation == "base");
    assert(!manifest.config.enable_dominator_cuts);
    assert(!manifest.config.enable_separator_cuts);
    assert(manifest.config.enable_greedy_warm_start);
    assert(!manifest.config.enable_local_search);
    assert(!manifest.config.sep_at_root);
    assert(manifest.config.sep_frequency_nodes == 25);
    assert(manifest.config.sep_max_scenarios_per_call == 4);
    assert(manifest.config.sep_max_nodes_per_scenario == 8);
    assert(manifest.config.sep_max_cuts_per_call == 12);
    assert(manifest.config.sep_min_violation == 0.0001);
    assert(manifest.config.sep_max_cut_cardinality == 9);
    assert(manifest.config.candidate_pool_size_multiplier == 7);
    assert(manifest.config.candidate_pool_min_size == 33);
    assert(!manifest.config.enable_greedy_exact_marginal);
    assert(manifest.config.local_search_max_iterations == 77);
    assert(manifest.config.local_search_time_limit_sec == 12.5);
    assert(manifest.config.max_aggregate_dominator_cuts_per_scenario == 11);
    assert(manifest.config.max_individual_dominator_cuts_per_scenario == 22);
    assert(manifest.config.root_user_cut_max_rounds == 2);
    assert(manifest.config.root_user_cut_tolerance == 0.000001);
    assert(manifest.config.combinatorial_options.scenario_order ==
           firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending);
    assert(manifest.config.cvar_beta_specified);
    assert(manifest.config.cvar_lambda_specified);
    assert(manifest.config.risk_config.cvarBeta == 0.8);
    assert(manifest.config.risk_config.cvarLambda == 0.4);
}

void test_branch_benders_variant_manifest() {
    const auto path = write_manifest("branch_benders_variants.txt", R"(
experiment_name=test_branch_benders_variants
landscape=Sub20
alphas=0.01
train_counts=2
test_count=3
num_cases=1
seed_base=123
methods=DPV-Branch-Benders,DPV-Branch-Benders-LLBI,DPV-Branch-Benders-RootCuts,DPV-Branch-Benders-LLBI-RootCuts
output_dir=results/batch/test_branch_benders_variants
warm_start_policy=none
root_user_cut_max_rounds=1
root_user_cut_tolerance=1e-6
)");
    const auto manifest = firebreak::experiments::load_experiment_manifest(path);
    assert((manifest.config.methods == std::vector<std::string>{
        "DPV-Branch-Benders",
        "DPV-Branch-Benders-LLBI",
        "DPV-Branch-Benders-RootCuts",
        "DPV-Branch-Benders-LLBI-RootCuts",
    }));
    assert(manifest.config.root_user_cut_max_rounds == 1);
    assert(manifest.config.root_user_cut_tolerance == 1.0e-6);
}

void test_official_method_taxonomy_manifest() {
    const auto path = write_manifest("official_method_taxonomy.txt", R"(
experiment_name=test_official_method_taxonomy
landscape=Sub20
alphas=0.01
train_counts=2
test_count=3
num_cases=1
seed_base=26000
methods=FPP-SAA,FPP-SAA-CVaR,FPP-SAA-MeanCVaR,FPP-Benders,FPP-Benders-CVaR,FPP-Benders-MeanCVaR,FPP-Branch-Benders,FPP-Branch-Benders-CVaR,FPP-Branch-Benders-MeanCVaR,FPP-Branch-Benders-LLBI,FPP-Branch-Benders-RootCuts,FPP-Branch-Benders-LLBI-RootCuts,FPP-Branch-Benders-CVaR-LLBI,FPP-Branch-Benders-CVaR-RootCuts,FPP-Branch-Benders-CVaR-LLBI-RootCuts,FPP-Restricted-Branch-Benders,FPP-Restricted-Branch-Benders-LLBI,FPP-Restricted-Branch-Benders-RootCuts,FPP-Restricted-Branch-Benders-LLBI-RootCuts,FPP-Restricted-Branch-Benders-CVaR,FPP-Restricted-Branch-Benders-CVaR-LLBI,FPP-Restricted-Branch-Benders-CVaR-RootCuts,FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts,FPP-Restricted-Branch-Benders-MeanCVaR,FPP-Restricted-Branch-Benders-MeanCVaR-LLBI,FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts,FPP-Restricted-Branch-Benders-MeanCVaR-LLBI-RootCuts,DPV-SAA,DPV-Benders,DPV-Branch-Benders,DPV-Branch-Benders-LLBI,DPV-Branch-Benders-RootCuts,DPV-Branch-Benders-LLBI-RootCuts,Static-DPV,Static-DPV-MIP,Greedy-DPV3,Greedy-DPV2,Greedy-Betweenness,Greedy-Closeness
output_dir=results/batch/test_official_method_taxonomy
warm_start_policy=none
)");
    const auto manifest = firebreak::experiments::load_experiment_manifest(path);
    assert((manifest.config.methods == std::vector<std::string>{
        "FPP-SAA",
        "FPP-SAA-CVaR",
        "FPP-SAA-MeanCVaR",
        "FPP-Benders",
        "FPP-Benders-CVaR",
        "FPP-Benders-MeanCVaR",
        "FPP-Branch-Benders",
        "FPP-Branch-Benders-CVaR",
        "FPP-Branch-Benders-MeanCVaR",
        "FPP-Branch-Benders-LLBI",
        "FPP-Branch-Benders-RootCuts",
        "FPP-Branch-Benders-LLBI-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI",
        "FPP-Branch-Benders-CVaR-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders",
        "FPP-Restricted-Branch-Benders-LLBI",
        "FPP-Restricted-Branch-Benders-RootCuts",
        "FPP-Restricted-Branch-Benders-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders-CVaR",
        "FPP-Restricted-Branch-Benders-CVaR-LLBI",
        "FPP-Restricted-Branch-Benders-CVaR-RootCuts",
        "FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders-MeanCVaR",
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI",
        "FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts",
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI-RootCuts",
        "DPV-SAA",
        "DPV-Benders",
        "DPV-Branch-Benders",
        "DPV-Branch-Benders-LLBI",
        "DPV-Branch-Benders-RootCuts",
        "DPV-Branch-Benders-LLBI-RootCuts",
        "Static-DPV",
        "Static-DPV-MIP",
        "Greedy-DPV3",
        "Greedy-DPV2",
        "Greedy-Betweenness",
        "Greedy-Closeness",
    }));
}

void test_manifest_fpp_cvar_risk_parameters() {
    const auto path = write_manifest("fpp_cvar_risk_parameters.txt", R"(
experiment_name=test_fpp_cvar_risk_parameters
landscape=Sub20
alphas=0.01
train_counts=2
test_count=3
num_cases=1
seed_base=123
methods=FPP-SAA-CVaR,FPP-Benders-MeanCVaR,FPP-Branch-Benders-CVaR-LLBI,FPP-Restricted-Branch-Benders-CVaR
output_dir=results/batch/test_fpp_cvar_risk_parameters
cvar_beta=0.85
cvar_lambda=0.25
)");
    const auto manifest = firebreak::experiments::load_experiment_manifest(path);
    assert(manifest.config.cvar_beta_specified);
    assert(manifest.config.cvar_lambda_specified);
    assert(manifest.config.risk_config.cvarBeta == 0.85);
    assert(manifest.config.risk_config.cvarLambda == 0.25);
}

void test_manifest_risk_measure_conflict_rejected() {
    const auto path = write_manifest("risk_conflict.txt", R"(
experiment_name=test_risk_conflict
landscape=Sub20
alphas=0.01
train_counts=2
test_count=3
num_cases=1
seed_base=123
methods=FPP-SAA-CVaR
output_dir=results/batch/test_risk_conflict
risk_measure=expected
)");
    bool threw = false;
    try {
        (void)firebreak::experiments::load_experiment_manifest(path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_manifest_dpv_cvar_rejected() {
    const auto path = write_manifest("dpv_cvar_rejected.txt", R"(
experiment_name=test_dpv_cvar_rejected
landscape=Sub20
alphas=0.01
train_counts=2
test_count=3
num_cases=1
seed_base=123
methods=DPV-SAA
output_dir=results/batch/test_dpv_cvar_rejected
risk_measure=cvar
)");
    bool threw = false;
    try {
        (void)firebreak::experiments::load_experiment_manifest(path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_final_candidate_manifests_parse() {
#ifdef FIREBREAK_PROJECT_ROOT
    const std::filesystem::path project_root = FIREBREAK_PROJECT_ROOT;
#else
    const std::filesystem::path project_root = ".";
#endif
    const auto fpp_manifest = firebreak::experiments::load_experiment_manifest(
        project_root / "config" / "final_candidate_methods_fpp_smoke.txt");
    assert((fpp_manifest.config.methods == std::vector<std::string>{
        "FPP-SAA",
        "FPP-SAA-CVaR",
        "FPP-Branch-Benders",
        "FPP-Branch-Benders-CVaR",
        "FPP-Branch-Benders-CVaR-LLBI",
        "FPP-Branch-Benders-CVaR-LLBI-RootCuts",
    }));
    assert(fpp_manifest.config.cvar_beta_specified);
    assert(fpp_manifest.config.cvar_lambda_specified);
    assert(fpp_manifest.config.risk_config.cvarBeta == 0.9);
    assert(fpp_manifest.config.risk_config.cvarLambda == 0.5);

    const auto dpv_manifest = firebreak::experiments::load_experiment_manifest(
        project_root / "config" / "final_candidate_methods_dpv_smoke.txt");
    assert((dpv_manifest.config.methods == std::vector<std::string>{
        "DPV-SAA",
        "DPV-Branch-Benders",
        "DPV-Branch-Benders-LLBI",
        "DPV-Branch-Benders-LLBI-RootCuts",
        "Static-DPV",
        "Greedy-DPV3",
    }));
    assert(!dpv_manifest.config.risk_measure_specified);
}

void test_missing_required_key() {
    const auto path = write_manifest("missing.txt", R"(
experiment_name=test_missing
landscape=Sub20
alphas=0.01
train_counts=2
test_count=3
num_cases=1
seed_base=123
methods=FPP-SAA
warm_start_policy=none
)");
    bool threw = false;
    try {
        (void)firebreak::experiments::load_experiment_manifest(path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_invalid_method() {
    const auto path = write_manifest("invalid_method.txt", R"(
experiment_name=test_invalid_method
landscape=Sub20
alphas=0.01
train_counts=2
test_count=3
num_cases=1
seed_base=123
methods=FPP-SAA,Unknown
output_dir=results/batch/test_invalid_method
warm_start_policy=none
)");
    bool threw = false;
    try {
        (void)firebreak::experiments::load_experiment_manifest(path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

int main() {
    test_valid_manifest();
    test_branch_benders_variant_manifest();
    test_official_method_taxonomy_manifest();
    test_manifest_fpp_cvar_risk_parameters();
    test_manifest_risk_measure_conflict_rejected();
    test_manifest_dpv_cvar_rejected();
    test_final_candidate_manifests_parse();
    test_missing_required_key();
    test_invalid_method();
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "firebreak_manifest_tests");
    std::cout << "All experiment manifest tests passed.\n";
    return 0;
}
