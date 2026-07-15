#include <iostream>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/ExperimentAggregator.hpp"
#include "analysis/BatchSummaryReporter.hpp"
#include "analysis/RuntimeProfiler.hpp"
#include "benders/FppStrengthening.hpp"
#include "experiments/BatchExperimentConfig.hpp"
#include "experiments/BatchExperimentRunner.hpp"
#include "experiments/DpvBendersOutOfSampleRunner.hpp"
#include "experiments/DpvBranchBendersOutOfSampleRunner.hpp"
#include "experiments/DpvSaaOutOfSampleRunner.hpp"
#include "experiments/EvaluationRunner.hpp"
#include "experiments/FppBendersOutOfSampleRunner.hpp"
#include "experiments/FppBranchBendersOutOfSampleRunner.hpp"
#include "experiments/FppDominancePreprocessingDiagnosticRunner.hpp"
#include "experiments/FppMasterLpDiagnosticRunner.hpp"
#include "experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.hpp"
#include "experiments/FppSaaOutOfSampleRunner.hpp"
#include "experiments/FppSaaRunner.hpp"
#include "experiments/FppLpViDiagnosticRunner.hpp"
#include "experiments/GraphDiagnosticsRunner.hpp"
#include "experiments/GreedyOutOfSampleRunner.hpp"
#include "experiments/ManifestRunner.hpp"
#include "experiments/NewInstancesSmokeRunner.hpp"
#include "experiments/OptInstanceRunner.hpp"
#include "experiments/SmokeRunner.hpp"
#include "experiments/StaticDpvOutOfSampleRunner.hpp"
#include "experiments/WeightMapGenerationRunner.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "risk/RiskMeasure.hpp"

namespace {

void print_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  firebreak_cpp smoke --landscape Sub20 --scenario-ids 1,2 "
        << "[--forest-path PATH] [--results-path PATH] [--output results/out.json]\n"
        << "  firebreak_cpp smoke-new-instances --instances-root new_instances "
        << "[--strict|--strict-metadata] [--output results/new_instances_smoke_summary.csv]\n"
        << "  firebreak_cpp generate-weight-map --landscape Sub20 "
        << "--weight-profile homogeneous|heterogeneous|clustered "
        << "--output-csv results/weights/map.csv --output-json results/weights/map.json "
        << "[--forest-path PATH] [--results-path PATH] [--weight-seed 123] "
        << "[--weight-normalize true|false]\n"
        << "  firebreak_cpp evaluate --landscape Sub20 --scenario-ids 1-5 "
        << "--firebreaks 10,20,30 [--forest-path PATH] [--results-path PATH] "
        << "[--weight-map-file weights.csv] [--cvar-beta 0.9] [--output results/out.json]\n"
        << "  firebreak_cpp build-opt-instance --landscape Sub20 --scenario-ids 1,2 "
        << "--alpha 0.01 [--forest-path PATH] [--results-path PATH] "
        << "[--output results/out.json]\n"
        << "  firebreak_cpp solve-fpp-saa --landscape Sub20 --scenario-ids 1,2 "
        << "--alpha 0.01 [--time-limit 60] [--mip-gap 0.001] [--threads 1] "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--weight-map-file weights.csv] [--output results/out.json]\n"
        << "  firebreak_cpp analyze-graphs --landscape Sub20 "
        << "[--scenario-ids 1,2,3 | --scenario-range 1:1000] "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--output results/out.json]\n"
        << "  firebreak_cpp run-fpp-saa-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID [--time-limit 60] [--mip-gap 0.001] "
        << "[--threads 1] [--forest-path PATH] [--results-path PATH] "
        << "[--warm-start-solution solution.csv] "
        << "[--weight-map-file weights.csv] "
        << "[--fpp-formulation base|cut] "
        << "[--risk-measure expected|cvar|mean-cvar] [--cvar-beta 0.9] [--cvar-lambda 1.0] "
        << "[--use-coverage-llbi] [--use-path-llbi] [--path-llbi-max-paths-per-node 8] "
        << "[--use-global-dominance-preprocessing] [--export-coverage-llbi PATH] "
        << "[--export-path-llbi PATH] [--export-dominance-preprocessing PATH] "
        << "[--output-json results/out.json] [--output-csv results/out.csv]\n"
        << "  firebreak_cpp run-fpp-benders-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID [--time-limit 60] [--mip-gap 0.001] "
        << "[--threads 1] [--max-iterations 20] [--tolerance 1e-6] "
        << "[--risk-measure expected|cvar|mean-cvar] [--cvar-beta 0.9] [--cvar-lambda 1.0] "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--weight-map-file weights.csv] "
        << "[--output-json results/out.json] [--output-csv results/out.csv] "
        << "[--export-benders-cuts results/cuts.csv] [--use-lifted-lower-bounds] "
        << "[--export-lifted-lower-bounds results/llbi.csv]\n"
        << "  firebreak_cpp run-fpp-branch-benders-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID [--time-limit 60] [--mip-gap 0.001] "
        << "[--threads 1] [--risk-measure expected|cvar|mean-cvar] "
        << "[--cvar-beta 0.9] [--cvar-lambda 1.0] "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--weight-map-file weights.csv] "
        << "[--output-json results/out.json] [--output-csv results/out.csv] "
        << "[--use-lifted-lower-bounds] [--use-root-user-cuts] "
        << "[--root-user-cut-max-rounds 1] [--root-user-cut-tolerance 1e-6] "
        << "[--use-combinatorial-benders] [--combinatorial-benders-lift none|posterior|heuristic] "
        << "[--combinatorial-benders-cut-sampling-ratio 0.10] "
        << "[--combinatorial-benders-scenario-order eta-asc|eta-desc] "
        << "[--combinatorial-benders-separate-fractional true|false] "
        << "[--combinatorial-benders-initial-cuts true|false] "
        << "[--use-coverage-llbi] [--use-path-llbi] [--path-llbi-max-paths-per-node 8] "
        << "[--use-projected-coverage-llbi-exp] [--use-projected-path-llbi-exp] "
        << "[--use-projected-coverage-llbi-poly] [--use-projected-path-llbi-poly] "
        << "[--projected-llbi-root-rounds 3] [--projected-llbi-max-cuts-per-round 100] "
        << "[--projected-llbi-violation-tolerance 1e-6] "
        << "[--projected-llbi-cut-density-limit 0] [--projected-poly-max-cuts 100000] "
        << "[--use-global-dominance-preprocessing] [--use-conditional-zero-benefit-fixing] "
        << "[--export-coverage-llbi PATH] [--export-path-llbi PATH] "
        << "[--export-dominance-preprocessing PATH] [--export-conditional-fixing-log PATH] "
        << "[--projected-llbi-export-cuts PATH]\n"
        << "  firebreak_cpp run-fpp-restricted-branch-benders-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID [--time-limit 60] [--mip-gap 0.001] "
        << "[--threads 1] [--risk-measure expected|cvar|mean-cvar] "
        << "[--cvar-beta 0.9] [--cvar-lambda 1.0] "
        << "[--initial-candidate-policy explicit-list|burn-frequency] "
        << "[--initial-candidate-size 50] [--initial-candidate-list ids] "
        << "[--candidate-activation-policy none|burn-frequency|benders-coefficients] "
        << "[--candidate-activation-batch-size 20] [--max-candidate-rounds 1] "
        << "[--candidate-maintenance-policy none|benders-coefficients] "
        << "[--candidate-deactivation-batch-size 20] "
        << "[--candidate-min-active-size 50] [--candidate-max-active-size 70] "
        << "[--candidate-deactivation-min-age 1] [--candidate-reactivation-cooldown-rounds 1] "
        << "[--candidate-score-mode generic|cvar-tail-blend] "
        << "[--candidate-tail-score-gamma 0.5] [--candidate-tail-protection-size 20] "
        << "[--allow-deactivate-selected] [--export-tail-score-diagnostics] "
        << "[--eventually-activate-all] [--restricted-exact-mode|--restricted-heuristic-mode] "
        << "[--stop-after-candidate-rounds R] "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--output-json results/out.json] [--output-csv results/out.csv] "
        << "[--use-lifted-lower-bounds] [--use-root-user-cuts] "
        << "[--root-user-cut-max-rounds 1] [--root-user-cut-tolerance 1e-6] "
        << "[--use-combinatorial-benders] [--combinatorial-benders-lift none|posterior|heuristic] "
        << "[--combinatorial-benders-cut-sampling-ratio 0.10] "
        << "[--combinatorial-benders-scenario-order eta-asc|eta-desc] "
        << "[--combinatorial-benders-separate-fractional true|false] "
        << "[--combinatorial-benders-initial-cuts true|false] "
        << "[--use-coverage-llbi] [--use-path-llbi] [--path-llbi-max-paths-per-node 8] "
        << "[--use-global-dominance-preprocessing] [--use-conditional-zero-benefit-fixing] "
        << "[--export-coverage-llbi PATH] [--export-path-llbi PATH] "
        << "[--export-dominance-preprocessing PATH] [--export-conditional-fixing-log PATH]\n"
        << "  firebreak_cpp run-static-dpv-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--output-json results/out.json] [--output-csv results/out.csv]\n"
        << "  firebreak_cpp run-static-dpv-mip-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--output-json results/out.json] [--output-csv results/out.csv]\n"
        << "  firebreak_cpp run-greedy-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --metric DPV3 --run-id RUN_ID "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--output-json results/out.json] [--output-csv results/out.csv]\n"
        << "  firebreak_cpp run-dpv-saa-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID [--time-limit 60] [--mip-gap 0.001] "
        << "[--threads 1] [--forest-path PATH] [--results-path PATH] "
        << "[--warm-start-solution solution.csv] "
        << "[--output-json results/out.json] [--output-csv results/out.csv]\n"
        << "  firebreak_cpp run-dpv-benders-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID [--time-limit 60] [--mip-gap 0.001] "
        << "[--threads 1] [--max-iterations 20] [--tolerance 1e-6] "
        << "[--forest-path PATH] [--results-path PATH] [--warm-start-solution solution.csv] "
        << "[--output-json results/out.json] [--output-csv results/out.csv] "
        << "[--export-benders-cuts results/cuts.csv] [--use-lifted-lower-bounds] "
        << "[--export-lifted-lower-bounds results/llbi.csv]\n"
        << "  firebreak_cpp run-dpv-branch-benders-oos --landscape Sub20 "
        << "[--train-ids 1-5 --test-ids 6:30 | --seed 123 --train-count 2 --test-count 3] "
        << "--alpha 0.01 --run-id RUN_ID [--time-limit 60] [--mip-gap 0.001] "
        << "[--threads 1] [--tolerance 1e-6] "
        << "[--forest-path PATH] [--results-path PATH] [--warm-start-solution solution.csv] "
        << "[--output-json results/out.json] [--output-csv results/out.csv] "
        << "[--use-lifted-lower-bounds] [--use-root-user-cuts] "
        << "[--root-user-cut-max-rounds 1] [--root-user-cut-tolerance 1e-6]\n"
        << "  firebreak_cpp run-batch-oos --landscape Sub20 --alphas 0.01,0.02 "
        << "--train-counts 2,5 --test-count 10 --num-cases 2 --seed-base 123 "
        << "--methods FPP-SAA,DPV-SAA,Static-DPV,Greedy-DPV3 "
        << "[--time-limit 60] [--mip-gap 0.001] [--threads 1] "
        << "[--warm-start-policy none] [--forest-path PATH] [--results-path PATH] "
        << "[--fpp-modes fpp_base,fpp_cut] "
        << "[--fpp-formulation base|cut] [--enable-dominator-cuts] [--enable-separator-cuts] "
        << "[--enable-greedy-warm-start] [--enable-local-search] "
        << "[--root-user-cut-max-rounds 1] [--root-user-cut-tolerance 1e-6] "
        << "[--use-combinatorial-benders] [--combinatorial-benders-lift none|posterior|heuristic] "
        << "[--combinatorial-benders-cut-sampling-ratio 0.10] "
        << "[--combinatorial-benders-scenario-order eta-asc|eta-desc] "
        << "[--use-coverage-llbi] [--use-path-llbi] [--path-llbi-max-paths-per-node 8] "
        << "[--use-projected-coverage-llbi-exp] [--use-projected-path-llbi-exp] "
        << "[--use-projected-coverage-llbi-poly] [--use-projected-path-llbi-poly] "
        << "[--projected-llbi-root-rounds 3] [--projected-llbi-max-cuts-per-round 100] "
        << "[--projected-llbi-violation-tolerance 1e-6] "
        << "[--projected-llbi-cut-density-limit 0] [--projected-poly-max-cuts 100000] "
        << "[--use-global-dominance-preprocessing] [--use-conditional-zero-benefit-fixing] "
        << "[--risk-measure expected|cvar|mean-cvar] [--cvar-beta 0.9] [--cvar-lambda 0.5] "
        << "[--shared-splits --split-dir results/batch/shared/splits] "
        << "[--rerun-existing] --output-dir results/batch/exp --output-csv results/batch/exp/batch_results.csv\n"
        << "  firebreak_cpp aggregate-batch --input-csv results/batch/exp/batch_results.csv "
        << "--output-dir results/batch/exp/summary\n"
        << "  firebreak_cpp run-fpp-lp-diagnostic --landscape Sub20 --alphas 0.01,0.02,0.03 "
        << "--train-count 100 --test-count 100 --num-cases 5 --seed-base 20260520 "
        << "[--time-limit 300] [--threads 1] --output-dir results/batch/fpp_lp_vi_diagnostic\n"
        << "  firebreak_cpp diagnose-fpp-master-lp --landscape Sub20 "
        << "--train-ids 1-100 --alpha 0.01 --variant master_lp_llbi "
        << "[--experiment-id ID] [--case-id case00] [--seed-base 20260601] [--seed 20260601] "
        << "[--forest-path PATH] [--results-path PATH] [--threads 1] "
        << "[--projected-llbi-root-rounds 3] [--projected-llbi-max-cuts-per-round 100] "
        << "[--projected-llbi-violation-tolerance 1e-6] "
        << "[--projected-llbi-cut-density-limit 0] [--projected-poly-max-cuts 100000] "
        << "[--output-json results/out.json] [--output-csv results/out.csv]\n"
        << "  firebreak_cpp diagnose-fpp-dominance-preprocessing --landscape Sub20 "
        << "--train-ids 1-100 --alpha 0.01 "
        << "[--experiment-id ID] [--case-id case00] [--seed-base 20260601] [--seed 20260601] "
        << "[--forest-path PATH] [--results-path PATH] "
        << "[--output-json results/out.json] [--output-csv results/out.csv]\n"
        << "  firebreak_cpp run-manifest --manifest config/phase11_sub20_debug.txt [--rerun-existing]\n";
}

std::string require_value(int& i, int argc, char** argv, const std::string& flag) {
    if (i + 1 >= argc) {
        throw std::runtime_error("Missing value for " + flag);
    }
    ++i;
    return argv[i];
}

double parse_double_strict(const std::string& value, const std::string& flag) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error("Invalid numeric value for " + flag + ": " + value);
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid numeric value for " + flag + ": " + value);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Numeric value out of range for " + flag + ": " + value);
    }
}

int parse_int_strict(const std::string& value, const std::string& flag) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error("Invalid integer value for " + flag + ": " + value);
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid integer value for " + flag + ": " + value);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Integer value out of range for " + flag + ": " + value);
    }
}

std::uint64_t parse_uint64_strict(const std::string& value, const std::string& flag) {
    if (!value.empty() && value.front() == '-') {
        throw std::runtime_error("Invalid unsigned integer value for " + flag + ": " + value);
    }
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error("Invalid unsigned integer value for " + flag + ": " + value);
        }
        return static_cast<std::uint64_t>(parsed);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid unsigned integer value for " + flag + ": " + value);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Unsigned integer value out of range for " + flag + ": " + value);
    }
}

int parse_nonnegative_int_arg(const std::string& value, const std::string& flag) {
    const int parsed = parse_int_strict(value, flag);
    if (parsed < 0) {
        throw std::runtime_error(flag + " must be nonnegative.");
    }
    return parsed;
}

double parse_nonnegative_double_arg(const std::string& value, const std::string& flag) {
    const double parsed = parse_double_strict(value, flag);
    if (parsed < 0.0) {
        throw std::runtime_error(flag + " must be nonnegative.");
    }
    return parsed;
}

bool parse_bool_arg(const std::string& value, const std::string& flag) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    throw std::runtime_error(flag + " must be true or false.");
}

bool parse_fpp_strengthening_arg(
    const std::string& arg,
    int& i,
    int argc,
    char** argv,
    firebreak::benders::FppStrengtheningOptions& options) {
    if (arg == "--use-coverage-llbi") {
        options.use_coverage_llbi = true;
        return true;
    }
    if (arg == "--use-path-llbi") {
        options.use_path_llbi = true;
        return true;
    }
    if (arg == "--use-projected-coverage-llbi-exp") {
        options.use_projected_coverage_llbi_exp = true;
        return true;
    }
    if (arg == "--use-projected-path-llbi-exp") {
        options.use_projected_path_llbi_exp = true;
        return true;
    }
    if (arg == "--use-projected-coverage-llbi-poly") {
        options.use_projected_coverage_llbi_poly = true;
        return true;
    }
    if (arg == "--use-projected-path-llbi-poly") {
        options.use_projected_path_llbi_poly = true;
        return true;
    }
    if (arg == "--projected-llbi-root-rounds") {
        const int rounds = parse_int_strict(require_value(i, argc, argv, arg), arg);
        if (rounds <= 0) {
            throw std::runtime_error("--projected-llbi-root-rounds must be positive.");
        }
        options.projected_llbi_root_rounds = rounds;
        return true;
    }
    if (arg == "--projected-llbi-max-cuts-per-round") {
        const int max_cuts = parse_int_strict(require_value(i, argc, argv, arg), arg);
        if (max_cuts <= 0) {
            throw std::runtime_error("--projected-llbi-max-cuts-per-round must be positive.");
        }
        options.projected_llbi_max_cuts_per_round = max_cuts;
        return true;
    }
    if (arg == "--projected-llbi-violation-tolerance") {
        options.projected_llbi_violation_tolerance =
            parse_nonnegative_double_arg(require_value(i, argc, argv, arg), arg);
        return true;
    }
    if (arg == "--projected-llbi-cut-density-limit") {
        options.projected_llbi_cut_density_limit =
            parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
        return true;
    }
    if (arg == "--projected-poly-max-cuts") {
        const int max_cuts = parse_int_strict(require_value(i, argc, argv, arg), arg);
        if (max_cuts <= 0) {
            throw std::runtime_error("--projected-poly-max-cuts must be positive.");
        }
        options.projected_poly_max_cuts = max_cuts;
        return true;
    }
    if (arg == "--projected-exp-max-cuts") {
        throw std::runtime_error(
            "--projected-exp-max-cuts was renamed to --projected-poly-max-cuts because "
            "the cap applies to the polynomial static projected subset.");
    }
    if (arg == "--path-llbi-max-paths-per-node") {
        const int max_paths = parse_int_strict(require_value(i, argc, argv, arg), arg);
        if (max_paths <= 0) {
            throw std::runtime_error("--path-llbi-max-paths-per-node must be positive.");
        }
        options.path_llbi_max_paths_per_node = max_paths;
        return true;
    }
    if (arg == "--use-global-dominance-preprocessing") {
        options.use_global_dominance_preprocessing = true;
        return true;
    }
    if (arg == "--use-conditional-zero-benefit-fixing") {
        options.use_conditional_zero_benefit_fixing = true;
        return true;
    }
    if (arg == "--export-coverage-llbi") {
        options.coverage_llbi_export_path = require_value(i, argc, argv, arg);
        return true;
    }
    if (arg == "--export-path-llbi") {
        options.path_llbi_export_path = require_value(i, argc, argv, arg);
        return true;
    }
    if (arg == "--export-dominance-preprocessing") {
        options.dominance_preprocessing_export_path = require_value(i, argc, argv, arg);
        return true;
    }
    if (arg == "--export-conditional-fixing-log") {
        options.conditional_fixing_log_export_path = require_value(i, argc, argv, arg);
        return true;
    }
    if (arg == "--projected-llbi-export-cuts") {
        options.projected_llbi_export_cuts_path = require_value(i, argc, argv, arg);
        return true;
    }
    return false;
}

bool parse_fpp_strengthening_batch_arg(
    const std::string& arg,
    int& i,
    int argc,
    char** argv,
    firebreak::experiments::BatchExperimentConfig& config) {
    if (arg == "--use-coverage-llbi") {
        config.use_coverage_llbi = true;
        return true;
    }
    if (arg == "--use-path-llbi") {
        config.use_path_llbi = true;
        return true;
    }
    if (arg == "--use-projected-coverage-llbi-exp") {
        config.projected_llbi_options.use_projected_coverage_llbi_exp = true;
        return true;
    }
    if (arg == "--use-projected-path-llbi-exp") {
        config.projected_llbi_options.use_projected_path_llbi_exp = true;
        return true;
    }
    if (arg == "--use-projected-coverage-llbi-poly") {
        config.projected_llbi_options.use_projected_coverage_llbi_poly = true;
        return true;
    }
    if (arg == "--use-projected-path-llbi-poly") {
        config.projected_llbi_options.use_projected_path_llbi_poly = true;
        return true;
    }
    if (arg == "--projected-llbi-root-rounds") {
        const int rounds = parse_int_strict(require_value(i, argc, argv, arg), arg);
        if (rounds <= 0) {
            throw std::runtime_error("--projected-llbi-root-rounds must be positive.");
        }
        config.projected_llbi_options.root_rounds = rounds;
        return true;
    }
    if (arg == "--projected-llbi-max-cuts-per-round") {
        const int max_cuts = parse_int_strict(require_value(i, argc, argv, arg), arg);
        if (max_cuts <= 0) {
            throw std::runtime_error("--projected-llbi-max-cuts-per-round must be positive.");
        }
        config.projected_llbi_options.max_cuts_per_round = max_cuts;
        return true;
    }
    if (arg == "--projected-llbi-violation-tolerance") {
        config.projected_llbi_options.violation_tolerance =
            parse_nonnegative_double_arg(require_value(i, argc, argv, arg), arg);
        return true;
    }
    if (arg == "--projected-llbi-cut-density-limit") {
        config.projected_llbi_options.cut_density_limit =
            parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
        return true;
    }
    if (arg == "--projected-poly-max-cuts") {
        const int max_cuts = parse_int_strict(require_value(i, argc, argv, arg), arg);
        if (max_cuts <= 0) {
            throw std::runtime_error("--projected-poly-max-cuts must be positive.");
        }
        config.projected_llbi_options.poly_max_cuts = max_cuts;
        return true;
    }
    if (arg == "--projected-exp-max-cuts") {
        throw std::runtime_error(
            "--projected-exp-max-cuts was renamed to --projected-poly-max-cuts because "
            "the cap applies to the polynomial static projected subset.");
    }
    if (arg == "--projected-llbi-export-cuts") {
        config.projected_llbi_options.export_cuts_path = require_value(i, argc, argv, arg);
        return true;
    }
    if (arg == "--path-llbi-max-paths-per-node") {
        const int max_paths = parse_int_strict(require_value(i, argc, argv, arg), arg);
        if (max_paths <= 0) {
            throw std::runtime_error("--path-llbi-max-paths-per-node must be positive.");
        }
        config.path_llbi_max_paths_per_node = max_paths;
        return true;
    }
    if (arg == "--use-global-dominance-preprocessing") {
        config.use_global_dominance_preprocessing = true;
        return true;
    }
    if (arg == "--use-conditional-zero-benefit-fixing") {
        config.use_conditional_zero_benefit_fixing = true;
        return true;
    }
    return false;
}

bool parse_fpp_combinatorial_benders_arg(
    const std::string& arg,
    int& i,
    int argc,
    char** argv,
    firebreak::benders::FppCombinatorialBendersOptions& options) {
    if (arg == "--use-combinatorial-benders") {
        options.enabled = true;
        return true;
    }
    if (arg == "--combinatorial-benders-lift") {
        options.lift_mode =
            firebreak::benders::parse_fpp_combinatorial_benders_lift_mode(
                require_value(i, argc, argv, arg));
        return true;
    }
    if (arg == "--combinatorial-benders-cut-sampling-ratio") {
        options.cut_sampling_ratio = parse_double_strict(require_value(i, argc, argv, arg), arg);
        return true;
    }
    if (arg == "--combinatorial-benders-scenario-order") {
        options.scenario_order =
            firebreak::benders::parse_fpp_combinatorial_benders_scenario_order(
                require_value(i, argc, argv, arg));
        return true;
    }
    if (arg == "--combinatorial-benders-separate-fractional") {
        options.separate_fractional = parse_bool_arg(require_value(i, argc, argv, arg), arg);
        return true;
    }
    if (arg == "--combinatorial-benders-initial-cuts") {
        options.initial_cuts = parse_bool_arg(require_value(i, argc, argv, arg), arg);
        return true;
    }
    return false;
}

std::vector<int> parse_scenario_range(const std::string& value) {
    const auto colon = value.find(':');
    if (colon == std::string::npos || value.find(':', colon + 1) != std::string::npos) {
        throw std::runtime_error("Scenario range must use START:END format.");
    }
    const int start = parse_int_strict(value.substr(0, colon), "--scenario-range");
    const int end = parse_int_strict(value.substr(colon + 1), "--scenario-range");
    if (start <= 0 || end <= 0 || start > end) {
        throw std::runtime_error("Scenario range must be positive and satisfy START <= END.");
    }
    std::vector<int> ids;
    ids.reserve(static_cast<std::size_t>(end - start + 1));
    for (int id = start; id <= end; ++id) {
        ids.push_back(id);
    }
    return ids;
}

std::string command_line_string(int argc, char** argv) {
    std::string out;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            out += " ";
        }
        out += argv[i];
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage(std::cerr);
            return 1;
        }

        const std::string command = argv[1];
        if (command != "smoke" &&
            command != "smoke-new-instances" &&
            command != "generate-weight-map" &&
            command != "evaluate" &&
            command != "build-opt-instance" &&
            command != "solve-fpp-saa" &&
            command != "analyze-graphs" &&
            command != "run-fpp-saa-oos" &&
            command != "run-fpp-benders-oos" &&
            command != "run-fpp-branch-benders-oos" &&
            command != "run-fpp-restricted-branch-benders-oos" &&
            command != "run-static-dpv-oos" &&
            command != "run-static-dpv-mip-oos" &&
            command != "run-greedy-oos" &&
            command != "run-dpv-saa-oos" &&
            command != "run-dpv-benders-oos" &&
            command != "run-dpv-branch-benders-oos" &&
            command != "run-batch-oos" &&
            command != "run-fpp-lp-diagnostic" &&
            command != "diagnose-fpp-master-lp" &&
            command != "diagnose-fpp-dominance-preprocessing" &&
            command != "aggregate-batch" &&
            command != "run-manifest") {
            print_usage(std::cerr);
            return 1;
        }

        if (command == "smoke") {
            firebreak::experiments::SmokeOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--scenario-ids") {
                    options.scenario_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--output") {
                    options.output_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::SmokeRunner runner;
            return runner.run(options);
        }

        if (command == "smoke-new-instances") {
            firebreak::experiments::NewInstancesSmokeOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--instances-root") {
                    options.instances_root = require_value(i, argc, argv, arg);
                } else if (arg == "--output") {
                    options.output_path = require_value(i, argc, argv, arg);
                } else if (arg == "--strict-metadata" || arg == "--strict") {
                    options.strict_metadata = true;
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::NewInstancesSmokeRunner runner;
            return runner.run(options);
        }

        if (command == "generate-weight-map") {
            firebreak::experiments::WeightMapGenerationOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--weight-profile") {
                    options.config.profile = require_value(i, argc, argv, arg);
                } else if (arg == "--weight-seed") {
                    options.config.seed = parse_uint64_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-normalize") {
                    options.config.normalize = parse_bool_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-min") {
                    options.config.heterogeneous_min =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-max") {
                    options.config.heterogeneous_max =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-cluster-count") {
                    options.config.cluster_count =
                        parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-cluster-fraction") {
                    options.config.cluster_fraction =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-background-min") {
                    options.config.background_min =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-background-max") {
                    options.config.background_max =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-cluster-min") {
                    options.config.cluster_min =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-cluster-max") {
                    options.config.cluster_max =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--weight-cluster-min-separation") {
                    options.config.cluster_min_separation =
                        parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::WeightMapGenerationRunner runner;
            return runner.run(options);
        }

        if (command == "evaluate") {
            firebreak::experiments::EvaluationOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--scenario-ids") {
                    options.scenario_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--firebreaks") {
                    options.firebreaks_csv = require_value(i, argc, argv, arg);
                } else if (arg == "--weight-map-file") {
                    options.weight_map_file = require_value(i, argc, argv, arg);
                } else if (arg == "--cvar-beta") {
                    options.cvar_beta = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--output") {
                    options.output_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::EvaluationRunner runner;
            return runner.run(options);
        }

        if (command == "analyze-graphs") {
            firebreak::experiments::GraphDiagnosticsOptions options;
            bool has_scenario_ids = false;
            bool has_scenario_range = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--scenario-ids") {
                    if (has_scenario_range) {
                        throw std::runtime_error("Use either --scenario-ids or --scenario-range, not both.");
                    }
                    has_scenario_ids = true;
                    options.scenario_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--scenario-range") {
                    if (has_scenario_ids) {
                        throw std::runtime_error("Use either --scenario-ids or --scenario-range, not both.");
                    }
                    has_scenario_range = true;
                    options.scenario_ids = parse_scenario_range(require_value(i, argc, argv, arg));
                } else if (arg == "--output") {
                    options.output_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::GraphDiagnosticsRunner runner;
            return runner.run(options);
        }

        if (command == "run-fpp-saa-oos") {
            firebreak::experiments::FppSaaOutOfSampleOptions options;
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    options.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--warm-start-solution") {
                    options.warm_start_solution_path = require_value(i, argc, argv, arg);
                } else if (arg == "--weight-map-file") {
                    options.weight_map_file = require_value(i, argc, argv, arg);
                } else if (arg == "--fpp-formulation") {
                    options.fpp_formulation =
                        firebreak::experiments::normalize_fpp_formulation(require_value(i, argc, argv, arg));
                } else if (arg == "--risk-measure") {
                    options.risk_config.type =
                        firebreak::risk::parse_risk_measure_type(require_value(i, argc, argv, arg));
                } else if (arg == "--cvar-beta") {
                    options.risk_config.cvarBeta = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--cvar-lambda") {
                    options.risk_config.cvarLambda = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (parse_fpp_strengthening_arg(
                               arg,
                               i,
                               argc,
                               argv,
                               options.strengthening_options)) {
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::FppSaaOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-fpp-benders-oos") {
            firebreak::experiments::FppBendersOutOfSampleOptions options;
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    options.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--max-iterations") {
                    options.max_iterations = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--tolerance") {
                    options.tolerance = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--risk-measure") {
                    options.risk_config.type =
                        firebreak::risk::parse_risk_measure_type(require_value(i, argc, argv, arg));
                } else if (arg == "--cvar-beta") {
                    options.risk_config.cvarBeta = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--cvar-lambda") {
                    options.risk_config.cvarLambda = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--weight-map-file") {
                    options.weight_map_file = require_value(i, argc, argv, arg);
                } else if (arg == "--export-benders-cuts") {
                    options.benders_cut_export_path = require_value(i, argc, argv, arg);
                } else if (arg == "--use-lifted-lower-bounds") {
                    options.use_lifted_lower_bounds = true;
                } else if (arg == "--export-lifted-lower-bounds") {
                    options.lifted_lower_bound_export_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::FppBendersOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-fpp-branch-benders-oos") {
            firebreak::experiments::FppBranchBendersOutOfSampleOptions options;
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    options.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--tolerance") {
                    options.tolerance = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--risk-measure") {
                    options.risk_config.type =
                        firebreak::risk::parse_risk_measure_type(require_value(i, argc, argv, arg));
                } else if (arg == "--cvar-beta") {
                    options.risk_config.cvarBeta = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--cvar-lambda") {
                    options.risk_config.cvarLambda = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--weight-map-file") {
                    options.weight_map_file = require_value(i, argc, argv, arg);
                } else if (arg == "--use-lifted-lower-bounds") {
                    options.use_lifted_lower_bounds = true;
                } else if (arg == "--use-root-user-cuts") {
                    options.use_root_user_cuts = true;
                } else if (arg == "--root-user-cut-max-rounds") {
                    const int rounds = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (rounds <= 0) {
                        throw std::runtime_error("--root-user-cut-max-rounds must be positive.");
                    }
                    options.root_user_cut_max_rounds = rounds;
                } else if (arg == "--root-user-cut-tolerance") {
                    options.root_user_cut_tolerance =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (parse_fpp_strengthening_arg(
                               arg,
                               i,
                               argc,
                               argv,
                               options.strengthening_options)) {
                } else if (parse_fpp_combinatorial_benders_arg(
                               arg,
                               i,
                               argc,
                               argv,
                               options.combinatorial_options)) {
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::FppBranchBendersOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-fpp-restricted-branch-benders-oos") {
            firebreak::experiments::FppRestrictedCandidateBranchBendersOutOfSampleOptions options;
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            bool saw_exact_mode = false;
            bool saw_heuristic_mode = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    options.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--tolerance") {
                    options.tolerance = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--risk-measure") {
                    options.risk_config.type =
                        firebreak::risk::parse_risk_measure_type(require_value(i, argc, argv, arg));
                } else if (arg == "--cvar-beta") {
                    options.risk_config.cvarBeta = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--cvar-lambda") {
                    options.risk_config.cvarLambda = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--initial-candidate-policy") {
                    options.initial_candidate_policy = require_value(i, argc, argv, arg);
                } else if (arg == "--initial-candidate-size") {
                    options.initial_candidate_size = parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--initial-candidate-list") {
                    options.initial_active_candidates =
                        firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--candidate-activation-policy") {
                    options.activation_policy = require_value(i, argc, argv, arg);
                } else if (arg == "--candidate-activation-batch-size") {
                    options.activation_batch_size = parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--max-candidate-rounds") {
                    options.max_candidate_rounds = parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-maintenance-policy") {
                    options.candidate_maintenance_policy = require_value(i, argc, argv, arg);
                } else if (arg == "--candidate-deactivation-batch-size") {
                    options.candidate_deactivation_batch_size =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-min-active-size") {
                    options.candidate_min_active_size =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-max-active-size") {
                    options.candidate_max_active_size =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-deactivation-min-age") {
                    options.candidate_deactivation_min_age =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-reactivation-cooldown-rounds") {
                    options.candidate_reactivation_cooldown_rounds =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-score-mode") {
                    options.candidate_score_mode = require_value(i, argc, argv, arg);
                } else if (arg == "--candidate-tail-score-gamma") {
                    options.candidate_tail_score_gamma =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-tail-protection-size") {
                    options.candidate_tail_protection_size =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--allow-deactivate-selected") {
                    options.protect_selected_candidates = false;
                } else if (arg == "--export-tail-score-diagnostics") {
                    options.export_tail_score_diagnostics = true;
                } else if (arg == "--use-lifted-lower-bounds") {
                    options.use_lifted_lower_bounds = true;
                } else if (arg == "--use-root-user-cuts") {
                    options.use_root_user_cuts = true;
                } else if (arg == "--root-user-cut-max-rounds") {
                    const int rounds = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (rounds <= 0) {
                        throw std::runtime_error("--root-user-cut-max-rounds must be positive.");
                    }
                    options.root_user_cut_max_rounds = rounds;
                } else if (arg == "--root-user-cut-tolerance") {
                    options.root_user_cut_tolerance =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--stop-after-candidate-rounds") {
                    options.stop_after_candidate_rounds =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--eventually-activate-all") {
                    options.eventually_activate_all = true;
                } else if (arg == "--restricted-exact-mode") {
                    saw_exact_mode = true;
                    options.restricted_exact_mode = true;
                    options.restricted_heuristic_mode = false;
                    options.eventually_activate_all = true;
                } else if (arg == "--restricted-heuristic-mode") {
                    saw_heuristic_mode = true;
                    options.restricted_heuristic_mode = true;
                    options.restricted_exact_mode = false;
                    options.eventually_activate_all = false;
                } else if (parse_fpp_strengthening_arg(
                               arg,
                               i,
                               argc,
                               argv,
                               options.strengthening_options)) {
                } else if (parse_fpp_combinatorial_benders_arg(
                               arg,
                               i,
                               argc,
                               argv,
                               options.combinatorial_options)) {
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }
            if (saw_exact_mode && saw_heuristic_mode) {
                throw std::runtime_error("--restricted-exact-mode and --restricted-heuristic-mode are mutually exclusive.");
            }

            firebreak::experiments::FppRestrictedCandidateBranchBendersOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-static-dpv-oos" || command == "run-static-dpv-mip-oos") {
            firebreak::experiments::StaticDpvOutOfSampleOptions options;
            options.use_static_dpv_mip = command == "run-static-dpv-mip-oos";
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::StaticDpvOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-greedy-oos") {
            firebreak::experiments::GreedyOutOfSampleOptions options;
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--metric") {
                    options.metric = require_value(i, argc, argv, arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::GreedyOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-dpv-saa-oos") {
            firebreak::experiments::DpvSaaOutOfSampleOptions options;
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    options.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--warm-start-solution") {
                    options.warm_start_solution_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::DpvSaaOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-dpv-benders-oos") {
            firebreak::experiments::DpvBendersOutOfSampleOptions options;
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    options.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--max-iterations") {
                    options.max_iterations = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--tolerance") {
                    options.tolerance = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--warm-start-solution") {
                    options.warm_start_solution_path = require_value(i, argc, argv, arg);
                } else if (arg == "--export-benders-cuts") {
                    options.benders_cut_export_path = require_value(i, argc, argv, arg);
                } else if (arg == "--use-lifted-lower-bounds") {
                    options.use_lifted_lower_bounds = true;
                } else if (arg == "--export-lifted-lower-bounds") {
                    options.lifted_lower_bound_export_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::DpvBendersOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-dpv-branch-benders-oos") {
            firebreak::experiments::DpvBranchBendersOutOfSampleOptions options;
            bool has_explicit_train = false;
            bool has_explicit_test = false;
            bool has_generated_arg = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_train = true;
                    options.train_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--test-ids") {
                    if (has_generated_arg) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_explicit_test = true;
                    options.test_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--seed") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--train-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    if (has_explicit_train || has_explicit_test) {
                        throw std::runtime_error("Use either explicit train/test IDs or generated split parameters, not both.");
                    }
                    has_generated_arg = true;
                    options.use_generated_split = true;
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    options.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--tolerance") {
                    options.tolerance = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--run-id") {
                    options.run_id = require_value(i, argc, argv, arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-json") {
                    options.solution_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--solution-csv") {
                    options.solution_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--warm-start-solution") {
                    options.warm_start_solution_path = require_value(i, argc, argv, arg);
                } else if (arg == "--use-lifted-lower-bounds") {
                    options.use_lifted_lower_bounds = true;
                } else if (arg == "--use-root-user-cuts") {
                    options.use_root_user_cuts = true;
                } else if (arg == "--root-user-cut-max-rounds") {
                    const int rounds = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (rounds <= 0) {
                        throw std::runtime_error("--root-user-cut-max-rounds must be positive.");
                    }
                    options.root_user_cut_max_rounds = rounds;
                } else if (arg == "--root-user-cut-tolerance") {
                    options.root_user_cut_tolerance =
                        parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::DpvBranchBendersOutOfSampleRunner runner;
            return runner.run(options);
        }

        if (command == "run-batch-oos") {
            firebreak::experiments::BatchExperimentConfig config;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    config.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    config.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    config.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--alphas") {
                    config.alpha_values = firebreak::experiments::parse_alpha_list(require_value(i, argc, argv, arg));
                } else if (arg == "--train-counts") {
                    config.train_counts = firebreak::experiments::parse_count_list(require_value(i, argc, argv, arg), "--train-counts");
                } else if (arg == "--test-count") {
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    config.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--num-cases") {
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--num-cases must be positive.");
                    }
                    config.num_cases = static_cast<std::size_t>(count);
                } else if (arg == "--seed-base") {
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed-base must be nonnegative.");
                    }
                    config.seed_base = static_cast<unsigned int>(seed);
                } else if (arg == "--methods") {
                    config.methods = firebreak::experiments::parse_batch_method_list(require_value(i, argc, argv, arg));
                } else if (arg == "--time-limit") {
                    config.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    config.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    config.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (config.threads < 0) {
                        throw std::runtime_error("--threads must be nonnegative.");
                    }
                } else if (arg == "--warm-start-policy") {
                    config.warm_start_policy = firebreak::experiments::normalize_warm_start_policy(require_value(i, argc, argv, arg));
                } else if (arg == "--fpp-modes" || arg == "--fpp-mode") {
                    config.fpp_modes = firebreak::experiments::parse_fpp_mode_list(require_value(i, argc, argv, arg));
                } else if (arg == "--fpp-formulation" || arg == "--formulation") {
                    config.fpp_formulation =
                        firebreak::experiments::normalize_fpp_formulation(require_value(i, argc, argv, arg));
                } else if (arg == "--enable-dominator-cuts") {
                    config.enable_dominator_cuts = true;
                } else if (arg == "--enable-separator-cuts") {
                    config.enable_separator_cuts = true;
                } else if (arg == "--enable-greedy-warm-start") {
                    config.enable_greedy_warm_start = true;
                } else if (arg == "--enable-local-search") {
                    config.enable_local_search = true;
                } else if (arg == "--sep-at-root") {
                    config.sep_at_root = true;
                } else if (arg == "--no-sep-at-root") {
                    config.sep_at_root = false;
                } else if (arg == "--sep-frequency-nodes") {
                    config.sep_frequency_nodes = parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--sep-max-scenarios-per-call") {
                    config.sep_max_scenarios_per_call =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--sep-max-nodes-per-scenario") {
                    config.sep_max_nodes_per_scenario =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--sep-max-cuts-per-call") {
                    config.sep_max_cuts_per_call = parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--sep-min-violation") {
                    config.sep_min_violation = parse_nonnegative_double_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--sep-max-cut-cardinality") {
                    config.sep_max_cut_cardinality =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-pool-size-multiplier") {
                    config.candidate_pool_size_multiplier =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--candidate-pool-min-size") {
                    config.candidate_pool_min_size = parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--enable-greedy-exact-marginal") {
                    config.enable_greedy_exact_marginal = true;
                } else if (arg == "--disable-greedy-exact-marginal") {
                    config.enable_greedy_exact_marginal = false;
                } else if (arg == "--local-search-max-iterations") {
                    config.local_search_max_iterations =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--local-search-time-limit-sec") {
                    config.local_search_time_limit_sec =
                        parse_nonnegative_double_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--max-aggregate-dominator-cuts-per-scenario") {
                    config.max_aggregate_dominator_cuts_per_scenario =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--max-individual-dominator-cuts-per-scenario") {
                    config.max_individual_dominator_cuts_per_scenario =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--root-user-cut-max-rounds") {
                    config.root_user_cut_max_rounds =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                    if (config.root_user_cut_max_rounds <= 0) {
                        throw std::runtime_error("--root-user-cut-max-rounds must be positive.");
                    }
                } else if (arg == "--root-user-cut-tolerance") {
                    config.root_user_cut_tolerance =
                        parse_nonnegative_double_arg(require_value(i, argc, argv, arg), arg);
                } else if (parse_fpp_strengthening_batch_arg(arg, i, argc, argv, config)) {
                } else if (parse_fpp_combinatorial_benders_arg(
                               arg,
                               i,
                               argc,
                               argv,
                               config.combinatorial_options)) {
                } else if (arg == "--risk-measure") {
                    config.risk_config.type =
                        firebreak::risk::parse_risk_measure_type(require_value(i, argc, argv, arg));
                    config.risk_measure_specified = true;
                } else if (arg == "--cvar-beta") {
                    config.risk_config.cvarBeta = parse_double_strict(require_value(i, argc, argv, arg), arg);
                    config.cvar_beta_specified = true;
                } else if (arg == "--cvar-lambda") {
                    config.risk_config.cvarLambda = parse_double_strict(require_value(i, argc, argv, arg), arg);
                    config.cvar_lambda_specified = true;
                } else if (arg == "--shared-splits") {
                    config.shared_splits = true;
                } else if (arg == "--split-dir") {
                    config.split_dir = require_value(i, argc, argv, arg);
                } else if (arg == "--rerun-existing") {
                    config.resume_existing = false;
                } else if (arg == "--output-dir") {
                    config.output_dir = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    config.output_csv = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::BatchExperimentRunner runner;
            return runner.run(config);
        }

        if (command == "run-fpp-lp-diagnostic") {
            firebreak::experiments::FppLpViDiagnosticOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--alphas") {
                    options.alphas = firebreak::experiments::parse_alpha_list(require_value(i, argc, argv, arg));
                } else if (arg == "--train-count") {
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--train-count must be positive.");
                    }
                    options.train_count = static_cast<std::size_t>(count);
                } else if (arg == "--test-count") {
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--test-count must be positive.");
                    }
                    options.test_count = static_cast<std::size_t>(count);
                } else if (arg == "--num-cases") {
                    const int count = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (count <= 0) {
                        throw std::runtime_error("--num-cases must be positive.");
                    }
                    options.num_cases = static_cast<std::size_t>(count);
                } else if (arg == "--seed-base") {
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed-base must be nonnegative.");
                    }
                    options.seed_base = static_cast<unsigned int>(seed);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--max-aggregate-dominator-cuts-per-scenario") {
                    options.max_aggregate_dominator_cuts_per_scenario =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--max-individual-dominator-cuts-per-scenario") {
                    options.max_individual_dominator_cuts_per_scenario =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--offline-sep-max-rounds") {
                    options.offline_sep_max_rounds =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--offline-sep-max-cuts-per-round") {
                    options.offline_sep_max_cuts_per_round =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--offline-sep-min-violation") {
                    options.offline_sep_min_violation =
                        parse_nonnegative_double_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--offline-sep-max-scenarios-per-round") {
                    options.offline_sep_max_scenarios_per_round =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--offline-sep-max-nodes-per-scenario") {
                    options.offline_sep_max_nodes_per_scenario =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--offline-sep-max-cut-cardinality") {
                    options.offline_sep_max_cut_cardinality =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--output-dir") {
                    options.output_dir = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::FppLpViDiagnosticRunner runner;
            return runner.run(options);
        }

        if (command == "aggregate-batch") {
            std::filesystem::path input_csv;
            std::filesystem::path output_dir;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--input-csv") {
                    input_csv = require_value(i, argc, argv, arg);
                } else if (arg == "--output-dir") {
                    output_dir = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }
            if (input_csv.empty()) {
                throw std::runtime_error("--input-csv is required.");
            }
            if (output_dir.empty()) {
                throw std::runtime_error("--output-dir is required.");
            }

            firebreak::analysis::ExperimentAggregator aggregator;
            const auto resolved_input_csv = firebreak::io::resolve_input_path(input_csv.string());
            const auto resolved_output_dir = firebreak::io::resolve_output_path(output_dir.string());
            (void)aggregator.aggregate(resolved_input_csv, resolved_output_dir);

            const auto runtime_csv = resolved_output_dir / "runtime_summary.csv";
            firebreak::analysis::RuntimeProfiler profiler;
            profiler.write_runtime_summary(resolved_input_csv, runtime_csv);

            firebreak::analysis::BatchSummaryReporter reporter;
            reporter.write_report(
                resolved_input_csv,
                resolved_output_dir / "summary_by_method.csv",
                resolved_output_dir / "pairwise_comparison_fpp_vs_dpv.csv",
                runtime_csv,
                resolved_output_dir / "summary_report.txt",
                resolved_output_dir.parent_path().filename().string());
            return 0;
        }

        if (command == "run-manifest") {
            firebreak::experiments::ManifestRunOptions options;
            options.executable_command = command_line_string(argc, argv);
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--manifest") {
                    options.manifest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--rerun-existing") {
                    options.rerun_existing = true;
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::ManifestRunner runner;
            return runner.run(options);
        }

        if (command == "solve-fpp-saa") {
            firebreak::experiments::FppSaaOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--scenario-ids") {
                    options.scenario_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--time-limit") {
                    options.time_limit_seconds = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--mip-gap") {
                    options.mip_gap = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--verbose") {
                    options.verbose = true;
                } else if (arg == "--weight-map-file") {
                    options.weight_map_file = require_value(i, argc, argv, arg);
                } else if (arg == "--output") {
                    options.output_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }

            firebreak::experiments::FppSaaRunner runner;
            return runner.run(options);
        }

        if (command == "diagnose-fpp-master-lp") {
            firebreak::experiments::FppMasterLpDiagnosticOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--experiment-id") {
                    options.experiment_id = require_value(i, argc, argv, arg);
                } else if (arg == "--case-id") {
                    options.case_id = require_value(i, argc, argv, arg);
                } else if (arg == "--seed-base") {
                    const int seed_base = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed_base < 0) {
                        throw std::runtime_error("--seed-base must be nonnegative.");
                    }
                    options.seed_base = static_cast<unsigned int>(seed_base);
                } else if (arg == "--seed") {
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    options.train_ids =
                        firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--variant") {
                    options.variant = require_value(i, argc, argv, arg);
                } else if (arg == "--threads") {
                    options.threads = parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--path-llbi-max-paths-per-node") {
                    options.path_llbi_max_paths_per_node =
                        parse_int_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--projected-llbi-root-rounds") {
                    const int value = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (value <= 0) {
                        throw std::runtime_error("--projected-llbi-root-rounds must be positive.");
                    }
                    options.projected_llbi_root_rounds = value;
                } else if (arg == "--projected-llbi-max-cuts-per-round") {
                    const int value = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (value <= 0) {
                        throw std::runtime_error("--projected-llbi-max-cuts-per-round must be positive.");
                    }
                    options.projected_llbi_max_cuts_per_round = value;
                } else if (arg == "--projected-llbi-violation-tolerance") {
                    options.projected_llbi_violation_tolerance =
                        parse_nonnegative_double_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--projected-llbi-cut-density-limit") {
                    options.projected_llbi_cut_density_limit =
                        parse_nonnegative_int_arg(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--projected-poly-max-cuts") {
                    const int value = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (value <= 0) {
                        throw std::runtime_error("--projected-poly-max-cuts must be positive.");
                    }
                    options.projected_poly_max_cuts = value;
                } else if (arg == "--projected-exp-max-cuts") {
                    throw std::runtime_error(
                        "--projected-exp-max-cuts was renamed to --projected-poly-max-cuts because "
                        "the cap applies to the polynomial static projected subset.");
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }
            firebreak::experiments::FppMasterLpDiagnosticRunner runner;
            return runner.run(options);
        }

        if (command == "diagnose-fpp-dominance-preprocessing") {
            firebreak::experiments::FppDominancePreprocessingDiagnosticOptions options;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--experiment-id") {
                    options.experiment_id = require_value(i, argc, argv, arg);
                } else if (arg == "--case-id") {
                    options.case_id = require_value(i, argc, argv, arg);
                } else if (arg == "--seed-base") {
                    const int seed_base = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed_base < 0) {
                        throw std::runtime_error("--seed-base must be nonnegative.");
                    }
                    options.seed_base = static_cast<unsigned int>(seed_base);
                } else if (arg == "--seed") {
                    const int seed = parse_int_strict(require_value(i, argc, argv, arg), arg);
                    if (seed < 0) {
                        throw std::runtime_error("--seed must be nonnegative.");
                    }
                    options.seed = static_cast<unsigned int>(seed);
                } else if (arg == "--landscape") {
                    options.landscape = require_value(i, argc, argv, arg);
                } else if (arg == "--forest-path") {
                    options.forest_path = require_value(i, argc, argv, arg);
                } else if (arg == "--results-path") {
                    options.results_path = require_value(i, argc, argv, arg);
                } else if (arg == "--train-ids") {
                    options.train_ids =
                        firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
                } else if (arg == "--alpha") {
                    options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
                } else if (arg == "--output-json") {
                    options.output_json_path = require_value(i, argc, argv, arg);
                } else if (arg == "--output-csv") {
                    options.output_csv_path = require_value(i, argc, argv, arg);
                } else if (arg == "--help" || arg == "-h") {
                    print_usage(std::cout);
                    return 0;
                } else {
                    throw std::runtime_error("Unknown argument: " + arg);
                }
            }
            firebreak::experiments::FppDominancePreprocessingDiagnosticRunner runner;
            return runner.run(options);
        }

        firebreak::experiments::OptInstanceOptions options;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--landscape") {
                options.landscape = require_value(i, argc, argv, arg);
            } else if (arg == "--forest-path") {
                options.forest_path = require_value(i, argc, argv, arg);
            } else if (arg == "--results-path") {
                options.results_path = require_value(i, argc, argv, arg);
            } else if (arg == "--scenario-ids") {
                options.scenario_ids = firebreak::io::parse_scenario_id_list(require_value(i, argc, argv, arg));
            } else if (arg == "--alpha") {
                options.alpha = parse_double_strict(require_value(i, argc, argv, arg), arg);
            } else if (arg == "--output") {
                options.output_path = require_value(i, argc, argv, arg);
            } else if (arg == "--help" || arg == "-h") {
                print_usage(std::cout);
                return 0;
            } else {
                throw std::runtime_error("Unknown argument: " + arg);
            }
        }

        firebreak::experiments::OptInstanceRunner runner;
        return runner.run(options);
    } catch (const std::exception& exc) {
        std::cerr << "Error: " << exc.what() << "\n";
        return 2;
    }
}
