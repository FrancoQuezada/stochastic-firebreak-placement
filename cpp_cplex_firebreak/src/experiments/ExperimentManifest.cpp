#include "experiments/ExperimentManifest.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "io/PathUtils.hpp"

namespace firebreak::experiments {

namespace {

std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

bool parse_bool_key_present(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key) {
    return values.find(key) != values.end();
}

bool parse_bool_value(const std::string& value, const std::string& key) {
    std::string cleaned;
    cleaned.reserve(value.size());
    for (const char ch : value) {
        cleaned.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (cleaned == "true" || cleaned == "yes" || cleaned == "1") {
        return true;
    }
    if (cleaned == "false" || cleaned == "no" || cleaned == "0") {
        return false;
    }
    throw std::runtime_error("Invalid boolean value for " + key + ": " + value);
}

int parse_nonnegative_int(const std::string& value, const std::string& key) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size() || parsed < 0) {
            throw std::runtime_error("Invalid nonnegative integer for " + key + ": " + value);
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid nonnegative integer for " + key + ": " + value);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Integer out of range for " + key + ": " + value);
    }
}

int parse_positive_int(const std::string& value, const std::string& key) {
    const int parsed = parse_nonnegative_int(value, key);
    if (parsed <= 0) {
        throw std::runtime_error("Expected positive integer for " + key + ": " + value);
    }
    return parsed;
}

double parse_double_value(const std::string& value, const std::string& key) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error("Invalid numeric value for " + key + ": " + value);
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid numeric value for " + key + ": " + value);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Numeric value out of range for " + key + ": " + value);
    }
}

std::string require_key(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key) {
    const auto found = values.find(key);
    if (found == values.end() || trim(found->second).empty()) {
        throw std::runtime_error("Manifest is missing required key: " + key);
    }
    return found->second;
}

bool has_key(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key) {
    return values.find(key) != values.end();
}

std::string join_strings(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    return out.str();
}

std::string join_doubles(const std::vector<double>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    return out.str();
}

std::string join_counts(const std::vector<std::size_t>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    return out.str();
}

}  // namespace

ExperimentManifest load_experiment_manifest(const std::filesystem::path& manifest_path) {
    std::ifstream in(manifest_path);
    if (!in) {
        throw std::runtime_error("Could not open manifest file: " + manifest_path.string());
    }

    std::unordered_map<std::string, std::string> values;
    std::unordered_set<std::string> seen_keys;
    std::string line;
    int line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        const std::string cleaned = trim(line);
        if (cleaned.empty() || cleaned[0] == '#') {
            continue;
        }
        const auto eq = cleaned.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("Invalid manifest line " + std::to_string(line_number) + ": expected key=value.");
        }
        const std::string key = trim(cleaned.substr(0, eq));
        const std::string value = trim(cleaned.substr(eq + 1));
        if (key.empty()) {
            throw std::runtime_error("Invalid manifest line " + std::to_string(line_number) + ": empty key.");
        }
        if (!seen_keys.insert(key).second) {
            throw std::runtime_error("Duplicate manifest key: " + key);
        }
        values[key] = value;
    }

    const std::vector<std::string> required = {
        "experiment_name",
        "landscape",
        "alphas",
        "train_counts",
        "test_count",
        "num_cases",
        "seed_base",
        "methods",
        "output_dir",
    };
    for (const auto& key : required) {
        (void)require_key(values, key);
    }

    ExperimentManifest manifest;
    manifest.source_path = manifest_path;
    auto& config = manifest.config;
    config.experiment_name = require_key(values, "experiment_name");
    config.landscape = require_key(values, "landscape");
    if (parse_bool_key_present(values, "forest_path")) {
        config.forest_path = values["forest_path"];
    }
    if (parse_bool_key_present(values, "results_path")) {
        config.results_path = values["results_path"];
    }
    config.alpha_values = parse_alpha_list(require_key(values, "alphas"));
    config.train_counts = parse_count_list(require_key(values, "train_counts"), "train_counts");
    config.test_count = static_cast<std::size_t>(parse_positive_int(require_key(values, "test_count"), "test_count"));
    config.num_cases = static_cast<std::size_t>(parse_positive_int(require_key(values, "num_cases"), "num_cases"));
    config.seed_base = static_cast<unsigned int>(parse_nonnegative_int(require_key(values, "seed_base"), "seed_base"));
    config.methods = parse_batch_method_list(require_key(values, "methods"));
    if (parse_bool_key_present(values, "time_limit")) {
        config.time_limit_seconds = parse_double_value(values["time_limit"], "time_limit");
    }
    if (parse_bool_key_present(values, "mip_gap")) {
        config.mip_gap = parse_double_value(values["mip_gap"], "mip_gap");
    }
    if (parse_bool_key_present(values, "threads")) {
        config.threads = parse_nonnegative_int(values["threads"], "threads");
    }
    config.output_dir = require_key(values, "output_dir");
    config.output_csv = config.output_dir / "batch_results.csv";
    if (parse_bool_key_present(values, "output_csv")) {
        config.output_csv = values["output_csv"];
    }
    if (parse_bool_key_present(values, "warm_start_policy")) {
        config.warm_start_policy = normalize_warm_start_policy(values["warm_start_policy"]);
    }
    if (has_key(values, "fpp_modes") && has_key(values, "fpp_mode")) {
        throw std::runtime_error("Manifest should use either fpp_modes or fpp_mode, not both.");
    }
    if (has_key(values, "fpp_modes")) {
        config.fpp_modes = parse_fpp_mode_list(values["fpp_modes"]);
    } else if (has_key(values, "fpp_mode")) {
        config.fpp_modes = parse_fpp_mode_list(values["fpp_mode"]);
    }
    if (parse_bool_key_present(values, "resume")) {
        config.resume_existing = parse_bool_value(values["resume"], "resume");
    }
    if (parse_bool_key_present(values, "shared_splits")) {
        config.shared_splits = parse_bool_value(values["shared_splits"], "shared_splits");
    }
    if (parse_bool_key_present(values, "split_dir")) {
        config.split_dir = values["split_dir"];
    }
    if (has_key(values, "fpp_formulation") && has_key(values, "formulation")) {
        const auto fpp_formulation = normalize_fpp_formulation(values["fpp_formulation"]);
        const auto formulation = normalize_fpp_formulation(values["formulation"]);
        if (fpp_formulation != formulation) {
            throw std::runtime_error("Manifest keys fpp_formulation and formulation disagree.");
        }
        config.fpp_formulation = fpp_formulation;
    } else if (has_key(values, "fpp_formulation")) {
        config.fpp_formulation = normalize_fpp_formulation(values["fpp_formulation"]);
    } else if (has_key(values, "formulation")) {
        config.fpp_formulation = normalize_fpp_formulation(values["formulation"]);
    }
    if (has_key(values, "enable_dominator_cuts")) {
        config.enable_dominator_cuts = parse_bool_value(values["enable_dominator_cuts"], "enable_dominator_cuts");
    }
    if (has_key(values, "enable_separator_cuts")) {
        config.enable_separator_cuts = parse_bool_value(values["enable_separator_cuts"], "enable_separator_cuts");
    }
    if (has_key(values, "enable_greedy_warm_start")) {
        config.enable_greedy_warm_start = parse_bool_value(values["enable_greedy_warm_start"], "enable_greedy_warm_start");
    }
    if (has_key(values, "enable_local_search")) {
        config.enable_local_search = parse_bool_value(values["enable_local_search"], "enable_local_search");
    }
    if (has_key(values, "sep_at_root")) {
        config.sep_at_root = parse_bool_value(values["sep_at_root"], "sep_at_root");
    }
    if (has_key(values, "sep_frequency_nodes")) {
        config.sep_frequency_nodes = parse_nonnegative_int(values["sep_frequency_nodes"], "sep_frequency_nodes");
    }
    if (has_key(values, "sep_max_scenarios_per_call")) {
        config.sep_max_scenarios_per_call =
            parse_nonnegative_int(values["sep_max_scenarios_per_call"], "sep_max_scenarios_per_call");
    }
    if (has_key(values, "sep_max_nodes_per_scenario")) {
        config.sep_max_nodes_per_scenario =
            parse_nonnegative_int(values["sep_max_nodes_per_scenario"], "sep_max_nodes_per_scenario");
    }
    if (has_key(values, "sep_max_cuts_per_call")) {
        config.sep_max_cuts_per_call =
            parse_nonnegative_int(values["sep_max_cuts_per_call"], "sep_max_cuts_per_call");
    }
    if (has_key(values, "sep_min_violation")) {
        config.sep_min_violation = parse_double_value(values["sep_min_violation"], "sep_min_violation");
    }
    if (has_key(values, "sep_max_cut_cardinality")) {
        config.sep_max_cut_cardinality =
            parse_nonnegative_int(values["sep_max_cut_cardinality"], "sep_max_cut_cardinality");
    }
    if (has_key(values, "candidate_pool_size_multiplier")) {
        config.candidate_pool_size_multiplier =
            parse_nonnegative_int(values["candidate_pool_size_multiplier"], "candidate_pool_size_multiplier");
    }
    if (has_key(values, "candidate_pool_min_size")) {
        config.candidate_pool_min_size =
            parse_nonnegative_int(values["candidate_pool_min_size"], "candidate_pool_min_size");
    }
    if (has_key(values, "enable_greedy_exact_marginal")) {
        config.enable_greedy_exact_marginal =
            parse_bool_value(values["enable_greedy_exact_marginal"], "enable_greedy_exact_marginal");
    }
    if (has_key(values, "local_search_max_iterations")) {
        config.local_search_max_iterations =
            parse_nonnegative_int(values["local_search_max_iterations"], "local_search_max_iterations");
    }
    if (has_key(values, "local_search_time_limit_sec")) {
        config.local_search_time_limit_sec =
            parse_double_value(values["local_search_time_limit_sec"], "local_search_time_limit_sec");
    }
    if (has_key(values, "max_aggregate_dominator_cuts_per_scenario")) {
        config.max_aggregate_dominator_cuts_per_scenario =
            parse_nonnegative_int(
                values["max_aggregate_dominator_cuts_per_scenario"],
                "max_aggregate_dominator_cuts_per_scenario");
    }
    if (has_key(values, "max_individual_dominator_cuts_per_scenario")) {
        config.max_individual_dominator_cuts_per_scenario =
            parse_nonnegative_int(
                values["max_individual_dominator_cuts_per_scenario"],
                "max_individual_dominator_cuts_per_scenario");
    }
    if (has_key(values, "root_user_cut_max_rounds")) {
        config.root_user_cut_max_rounds =
            parse_positive_int(values["root_user_cut_max_rounds"], "root_user_cut_max_rounds");
    }
    if (has_key(values, "root_user_cut_tolerance")) {
        const double tolerance =
            parse_double_value(values["root_user_cut_tolerance"], "root_user_cut_tolerance");
        if (tolerance < 0.0) {
            throw std::runtime_error("root_user_cut_tolerance must be nonnegative.");
        }
        config.root_user_cut_tolerance = tolerance;
    }
    if (has_key(values, "use_coverage_llbi")) {
        config.use_coverage_llbi = parse_bool_value(values["use_coverage_llbi"], "use_coverage_llbi");
    }
    if (has_key(values, "use_path_llbi")) {
        config.use_path_llbi = parse_bool_value(values["use_path_llbi"], "use_path_llbi");
    }
    if (has_key(values, "path_llbi_max_paths_per_node")) {
        config.path_llbi_max_paths_per_node =
            parse_positive_int(values["path_llbi_max_paths_per_node"], "path_llbi_max_paths_per_node");
    }
    if (has_key(values, "use_projected_coverage_llbi_exp")) {
        config.projected_llbi_options.use_projected_coverage_llbi_exp =
            parse_bool_value(
                values["use_projected_coverage_llbi_exp"],
                "use_projected_coverage_llbi_exp");
    }
    if (has_key(values, "use_projected_path_llbi_exp")) {
        config.projected_llbi_options.use_projected_path_llbi_exp =
            parse_bool_value(
                values["use_projected_path_llbi_exp"],
                "use_projected_path_llbi_exp");
    }
    if (has_key(values, "use_projected_coverage_llbi_poly")) {
        config.projected_llbi_options.use_projected_coverage_llbi_poly =
            parse_bool_value(
                values["use_projected_coverage_llbi_poly"],
                "use_projected_coverage_llbi_poly");
    }
    if (has_key(values, "use_projected_path_llbi_poly")) {
        config.projected_llbi_options.use_projected_path_llbi_poly =
            parse_bool_value(
                values["use_projected_path_llbi_poly"],
                "use_projected_path_llbi_poly");
    }
    if (has_key(values, "projected_llbi_root_rounds")) {
        config.projected_llbi_options.root_rounds =
            parse_positive_int(values["projected_llbi_root_rounds"], "projected_llbi_root_rounds");
    }
    if (has_key(values, "projected_llbi_max_cuts_per_round")) {
        config.projected_llbi_options.max_cuts_per_round =
            parse_positive_int(
                values["projected_llbi_max_cuts_per_round"],
                "projected_llbi_max_cuts_per_round");
    }
    if (has_key(values, "projected_llbi_violation_tolerance")) {
        const double tolerance = parse_double_value(
            values["projected_llbi_violation_tolerance"],
            "projected_llbi_violation_tolerance");
        if (tolerance < 0.0) {
            throw std::runtime_error("projected_llbi_violation_tolerance must be nonnegative.");
        }
        config.projected_llbi_options.violation_tolerance = tolerance;
    }
    if (has_key(values, "projected_llbi_cut_density_limit")) {
        config.projected_llbi_options.cut_density_limit =
            parse_nonnegative_int(
                values["projected_llbi_cut_density_limit"],
                "projected_llbi_cut_density_limit");
    }
    if (has_key(values, "projected_poly_max_cuts")) {
        config.projected_llbi_options.poly_max_cuts =
            parse_positive_int(values["projected_poly_max_cuts"], "projected_poly_max_cuts");
    }
    if (has_key(values, "projected_exp_max_cuts")) {
        throw std::runtime_error(
            "projected_exp_max_cuts was renamed to projected_poly_max_cuts because "
            "the cap applies to the polynomial static projected subset.");
    }
    if (has_key(values, "projected_llbi_export_cuts")) {
        config.projected_llbi_options.export_cuts_path =
            values["projected_llbi_export_cuts"];
    }
    if (has_key(values, "use_global_dominance_preprocessing")) {
        config.use_global_dominance_preprocessing = parse_bool_value(
            values["use_global_dominance_preprocessing"],
            "use_global_dominance_preprocessing");
    }
    if (has_key(values, "use_conditional_zero_benefit_fixing")) {
        config.use_conditional_zero_benefit_fixing = parse_bool_value(
            values["use_conditional_zero_benefit_fixing"],
            "use_conditional_zero_benefit_fixing");
    }
    if (has_key(values, "use_combinatorial_benders")) {
        config.combinatorial_options.enabled = parse_bool_value(
            values["use_combinatorial_benders"],
            "use_combinatorial_benders");
    }
    if (has_key(values, "combinatorial_benders_lift")) {
        config.combinatorial_options.lift_mode =
            benders::parse_fpp_combinatorial_benders_lift_mode(
                values["combinatorial_benders_lift"]);
    }
    if (has_key(values, "combinatorial_benders_cut_sampling_ratio")) {
        config.combinatorial_options.cut_sampling_ratio =
            parse_double_value(
                values["combinatorial_benders_cut_sampling_ratio"],
                "combinatorial_benders_cut_sampling_ratio");
    }
    if (has_key(values, "combinatorial_benders_scenario_order")) {
        config.combinatorial_options.scenario_order =
            benders::parse_fpp_combinatorial_benders_scenario_order(
                values["combinatorial_benders_scenario_order"]);
    }
    if (has_key(values, "combinatorial_benders_separate_fractional")) {
        config.combinatorial_options.separate_fractional = parse_bool_value(
            values["combinatorial_benders_separate_fractional"],
            "combinatorial_benders_separate_fractional");
    }
    if (has_key(values, "combinatorial_benders_initial_cuts")) {
        config.combinatorial_options.initial_cuts = parse_bool_value(
            values["combinatorial_benders_initial_cuts"],
            "combinatorial_benders_initial_cuts");
    }
    if (has_key(values, "risk_measure")) {
        config.risk_config.type = risk::parse_risk_measure_type(values["risk_measure"]);
        config.risk_measure_specified = true;
    }
    if (has_key(values, "cvar_beta")) {
        config.risk_config.cvarBeta = parse_double_value(values["cvar_beta"], "cvar_beta");
        config.cvar_beta_specified = true;
    }
    if (has_key(values, "cvar_lambda")) {
        config.risk_config.cvarLambda = parse_double_value(values["cvar_lambda"], "cvar_lambda");
        config.cvar_lambda_specified = true;
    }

    validate_batch_experiment_config(config);
    return manifest;
}

void copy_manifest_file(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& output_dir) {
    std::filesystem::create_directories(output_dir);
    const auto output_path = output_dir / "manifest_used.txt";
    std::filesystem::copy_file(
        manifest_path,
        output_path,
        std::filesystem::copy_options::overwrite_existing);
}

std::string describe_manifest_config(const ExperimentManifest& manifest) {
    const auto& c = manifest.config;
    std::ostringstream out;
    out << "Experiment manifest: " << c.experiment_name << "\n"
        << "Landscape: " << c.landscape << "\n"
        << "Forest path: " << c.forest_path.string() << "\n"
        << "Results path: " << c.results_path.string() << "\n"
        << "Alphas: " << join_doubles(c.alpha_values) << "\n"
        << "Train counts: " << join_counts(c.train_counts) << "\n"
        << "Test count: " << c.test_count << "\n"
        << "Num cases: " << c.num_cases << "\n"
        << "Seed base: " << c.seed_base << "\n"
        << "Methods: " << join_strings(c.methods) << "\n"
        << "FPP modes: " << join_strings(c.fpp_modes) << "\n"
        << "Time limit: " << c.time_limit_seconds << "\n"
        << "MIP gap: " << c.mip_gap << "\n"
        << "Threads: " << c.threads << "\n"
        << "Output dir: " << c.output_dir.string() << "\n"
        << "Output CSV: " << c.output_csv.string() << "\n"
        << "Warm-start policy: " << c.warm_start_policy << "\n"
        << "Root user cut max rounds: " << c.root_user_cut_max_rounds << "\n"
        << "Root user cut tolerance: " << c.root_user_cut_tolerance << "\n"
        << "Risk measure: "
        << (c.risk_measure_specified ? risk::to_string(c.risk_config.type) : "(method label default)")
        << "\n"
        << "CVaR beta: " << c.risk_config.cvarBeta << "\n"
        << "CVaR lambda: " << c.risk_config.cvarLambda << "\n"
        << "Resume existing: " << (c.resume_existing ? "true" : "false") << "\n"
        << "Shared splits: " << (c.shared_splits ? "true" : "false") << "\n"
        << "Split dir: " << c.split_dir.string() << "\n"
        << fpp_enhancement_config_summary(c);
    return out.str();
}

}  // namespace firebreak::experiments
