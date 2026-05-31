#include "experiments/BatchExperimentRunner.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "experiments/MethodDispatcher.hpp"
#include "experiments/SharedSplitUtils.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "io/ScenarioSplitUtils.hpp"

namespace firebreak::experiments {

namespace {

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

std::string sanitize_double(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    for (char& ch : text) {
        if (ch == '.') {
            ch = 'p';
        }
    }
    return text;
}

std::string method_slug(std::string method) {
    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char ch) {
        if (ch == '-') {
            return '_';
        }
        return static_cast<char>(std::tolower(ch));
    });
    return method;
}

std::string experiment_id_from_config(const BatchExperimentConfig& config, const std::filesystem::path& output_dir) {
    if (!config.experiment_name.empty()) {
        return config.experiment_name;
    }
    const auto name = output_dir.filename().string();
    if (!name.empty() && name != "." && name != "..") {
        return name;
    }
    return "batch_experiment";
}

unsigned int split_seed(
    unsigned int seed_base,
    std::size_t alpha_pos,
    std::size_t train_pos,
    std::size_t case_id) {
    return seed_base
        + static_cast<unsigned int>(case_id)
        + static_cast<unsigned int>(1000 * train_pos)
        + static_cast<unsigned int>(100000 * alpha_pos);
}

void save_batch_split(
    const std::filesystem::path& output_dir,
    const std::string& experiment_id,
    double alpha,
    std::size_t train_count,
    std::size_t test_count,
    std::size_t case_id,
    unsigned int seed,
    const io::ScenarioSplit& split) {
    std::ostringstream prefix;
    prefix << experiment_id
           << "_alpha" << sanitize_double(alpha)
           << "_train" << train_count
           << "_test" << test_count
           << "_case" << case_id
           << "_seed" << seed;
    const auto split_dir = output_dir / "splits";
    io::save_scenario_ids(split_dir / (prefix.str() + "_train.csv"), split.train_ids);
    io::save_scenario_ids(split_dir / (prefix.str() + "_test.csv"), split.test_ids);
}

std::string normalized_alpha(double alpha) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(8) << alpha;
    return out.str();
}

std::string resume_key(
    const std::string& experiment_id,
    const std::string& landscape,
    double alpha,
    std::size_t train_count,
    std::size_t test_count,
    std::size_t case_id,
    const std::string& method,
    const std::string& fpp_mode = "") {
    std::ostringstream out;
    out << experiment_id << "|"
        << landscape << "|"
        << normalized_alpha(alpha) << "|"
        << train_count << "|"
        << test_count << "|"
        << case_id << "|"
        << method << "|"
        << fpp_mode;
    return out.str();
}

std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (in_quotes) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                current.push_back(ch);
            }
        } else if (ch == '"') {
            in_quotes = true;
        } else if (ch == ',') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string get_csv_field(
    const std::vector<std::string>& fields,
    const std::unordered_map<std::string, std::size_t>& columns,
    const std::string& name) {
    const auto found = columns.find(name);
    if (found == columns.end() || found->second >= fields.size()) {
        return "";
    }
    return fields[found->second];
}

int parse_int_or_default(const std::string& value, int fallback) {
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

double parse_double_or_default(const std::string& value, double fallback) {
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stod(value);
    } catch (...) {
        return fallback;
    }
}

std::unordered_set<std::string> load_completed_resume_keys(const std::filesystem::path& output_csv) {
    std::unordered_set<std::string> keys;
    std::ifstream in(output_csv);
    if (!in) {
        return keys;
    }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        return keys;
    }
    const auto header = parse_csv_line(header_line);
    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < header.size(); ++i) {
        columns[header[i]] = i;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = parse_csv_line(line);
        const std::string experiment_id = get_csv_field(fields, columns, "experiment_id");
        const std::string landscape = get_csv_field(fields, columns, "landscape");
        const std::string method = get_csv_field(fields, columns, "method");
        const std::string fpp_mode = get_csv_field(fields, columns, "fpp_mode");
        const double alpha = parse_double_or_default(get_csv_field(fields, columns, "alpha"), -1.0);
        const int train_count = parse_int_or_default(get_csv_field(fields, columns, "train_scenario_count"), -1);
        const int test_count = parse_int_or_default(get_csv_field(fields, columns, "test_scenario_count"), -1);
        const int case_id = parse_int_or_default(get_csv_field(fields, columns, "case_id"), -1);
        if (experiment_id.empty() || landscape.empty() || method.empty() ||
            alpha < 0.0 || train_count < 0 || test_count < 0 || case_id < 0) {
            continue;
        }
        keys.insert(resume_key(
            experiment_id,
            landscape,
            alpha,
            static_cast<std::size_t>(train_count),
            static_cast<std::size_t>(test_count),
            static_cast<std::size_t>(case_id),
            method,
            fpp_mode));
    }
    return keys;
}

std::vector<std::string> modes_for_method(const BatchExperimentConfig& config, const std::string& method) {
    const auto fpp_variant = fpp_method_variant_settings(method);
    if (!fpp_variant.is_fpp_saa || config.fpp_modes.empty()) {
        return {""};
    }
    std::vector<std::string> modes;
    modes.reserve(config.fpp_modes.size());
    for (const auto& mode : config.fpp_modes) {
        modes.push_back(normalize_fpp_mode(mode));
    }
    return modes;
}

BatchExperimentConfig config_for_mode(const BatchExperimentConfig& config, const std::string& mode) {
    if (mode.empty()) {
        return config;
    }
    BatchExperimentConfig out = config;
    const auto settings = fpp_mode_settings(mode);
    out.fpp_formulation = settings.formulation;
    out.enable_greedy_warm_start = settings.enable_greedy_warm_start;
    out.enable_dominator_cuts = settings.enable_dominator_cuts;
    out.enable_separator_cuts = settings.enable_separator_cuts;
    out.enable_local_search = settings.enable_local_search;
    return out;
}

}  // namespace

int BatchExperimentRunner::run(const BatchExperimentConfig& raw_config) const {
    BatchExperimentConfig config = raw_config;
    config.warm_start_policy = normalize_warm_start_policy(config.warm_start_policy);
    config.fpp_formulation = normalize_fpp_formulation(config.fpp_formulation);
    for (auto& method : config.methods) {
        method = normalize_batch_method_name(method);
    }
    validate_batch_experiment_config(config);

    const auto forest_path = config.forest_path.empty()
        ? default_forest_path(config.landscape)
        : firebreak::io::resolve_input_path(config.forest_path.string());
    const auto results_path = config.results_path.empty()
        ? default_results_path(config.landscape)
        : firebreak::io::resolve_input_path(config.results_path.string());
    const auto output_dir = firebreak::io::resolve_output_path(config.output_dir.string());
    const auto output_csv = firebreak::io::resolve_output_path(config.output_csv.string());
    const auto shared_split_dir = config.shared_splits
        ? firebreak::io::resolve_output_path(config.split_dir.string())
        : std::filesystem::path{};
    const std::string experiment_id = experiment_id_from_config(config, output_dir);

    std::filesystem::create_directories(output_dir / "json");
    std::filesystem::create_directories(output_dir / "solutions");
    std::filesystem::create_directories(output_dir / "splits");
    if (config.shared_splits) {
        std::filesystem::create_directories(shared_split_dir);
    }

    const auto inventory = firebreak::io::detect_message_files(results_path);
    const auto available_ids = inventory.ids();
    for (const auto train_count : config.train_counts) {
        if (train_count + config.test_count > available_ids.size()) {
            throw std::runtime_error("Requested train/test split exceeds available scenarios.");
        }
    }

    MethodDispatcher dispatcher;
    auto completed_keys = config.resume_existing
        ? load_completed_resume_keys(output_csv)
        : std::unordered_set<std::string>{};
    std::size_t run_count = 0;
    std::size_t skipped_count = 0;
    std::size_t failure_count = 0;

    std::cout << "Batch experiment: " << experiment_id << "\n";
    std::cout << "Landscape: " << config.landscape << "\n";
    std::cout << "Output dir: " << firebreak::io::path_to_string(output_dir) << "\n";
    std::cout << "Output CSV: " << firebreak::io::path_to_string(output_csv) << "\n";
    if (config.shared_splits) {
        std::cout << "Shared split dir: " << firebreak::io::path_to_string(shared_split_dir) << "\n";
    }
    std::cout << fpp_enhancement_config_summary(config) << std::flush;

    for (std::size_t alpha_pos = 0; alpha_pos < config.alpha_values.size(); ++alpha_pos) {
        const double alpha = config.alpha_values[alpha_pos];
        for (std::size_t train_pos = 0; train_pos < config.train_counts.size(); ++train_pos) {
            const auto train_count = config.train_counts[train_pos];
            for (std::size_t case_id = 0; case_id < config.num_cases; ++case_id) {
                unsigned int seed = 0;
                io::ScenarioSplit split;
                if (config.shared_splits) {
                    const auto shared_split = load_or_create_shared_split(
                        shared_split_dir,
                        config.landscape,
                        available_ids,
                        config.seed_base,
                        train_count,
                        config.test_count,
                        case_id);
                    seed = shared_split.seed;
                    split = shared_split.split;
                    std::cout << "Using shared split:\n"
                              << "  train_count=" << train_count
                              << ", case_id=" << case_id
                              << ", alpha=" << alpha << "\n"
                              << "  split files: "
                              << firebreak::io::path_to_string(shared_split.paths.train_path)
                              << " ; "
                              << firebreak::io::path_to_string(shared_split.paths.test_path)
                              << "\n"
                              << "  split status: "
                              << (shared_split.reused_existing ? "reused existing" : "generated")
                              << "\n" << std::flush;
                } else {
                    seed = split_seed(config.seed_base, alpha_pos, train_pos, case_id);
                    split = io::generate_train_test_split(
                        available_ids,
                        seed,
                        train_count,
                        config.test_count);
                    save_batch_split(
                        output_dir,
                        experiment_id,
                        alpha,
                        train_count,
                        config.test_count,
                        case_id,
                        seed,
                        split);
                }

                for (const auto& method : config.methods) {
                    for (const auto& fpp_mode : modes_for_method(config, method)) {
                        const BatchExperimentConfig run_config = config_for_mode(config, fpp_mode);
                        const std::string result_fpp_mode =
                            fpp_method_variant_settings(method).is_fpp_saa
                                ? (fpp_mode.empty()
                                    ? fpp_mode_name_from_settings(
                                        run_config.fpp_formulation,
                                        run_config.enable_greedy_warm_start,
                                        run_config.enable_dominator_cuts,
                                        run_config.enable_separator_cuts,
                                        run_config.enable_local_search)
                                    : fpp_mode)
                                : "";
                        const std::string current_key = resume_key(
                            experiment_id,
                            config.landscape,
                            alpha,
                            train_count,
                            config.test_count,
                            case_id,
                            method,
                            result_fpp_mode);
                        if (config.resume_existing && completed_keys.find(current_key) != completed_keys.end()) {
                            ++skipped_count;
                            std::cout << "SKIPPED existing result: "
                                      << method
                                      << (result_fpp_mode.empty() ? "" : " mode=" + result_fpp_mode)
                                      << " alpha=" << alpha
                                      << " train=" << train_count
                                      << " test=" << config.test_count
                                      << " case=" << case_id
                                      << " seed=" << seed << "\n" << std::flush;
                            continue;
                        }

                        std::ostringstream run_id;
                        run_id << experiment_id
                               << "_alpha" << sanitize_double(alpha)
                               << "_train" << train_count
                               << "_test" << config.test_count
                               << "_case" << case_id
                               << "_" << method_slug(method);
                        if (!fpp_mode.empty()) {
                            run_id << "_" << fpp_mode;
                        }

                        MethodDispatchRequest request;
                        request.experiment_id = experiment_id;
                        request.case_id = static_cast<int>(case_id);
                        request.run_id = run_id.str();
                        request.method = method;
                        request.landscape = config.landscape;
                        request.forest_path = forest_path;
                        request.results_path = results_path;
                        request.output_dir = output_dir;
                        request.output_csv = output_csv;
                        request.alpha = alpha;
                        request.train_ids = split.train_ids;
                        request.test_ids = split.test_ids;
                        request.time_limit_seconds = run_config.time_limit_seconds;
                        request.mip_gap = run_config.mip_gap;
                        request.threads = run_config.threads;
                        request.warm_start_policy = run_config.warm_start_policy;
                        request.fpp_mode = result_fpp_mode;
                        request.fpp_formulation = run_config.fpp_formulation;
                        request.enable_dominator_cuts = run_config.enable_dominator_cuts;
                        request.enable_separator_cuts = run_config.enable_separator_cuts;
                        request.enable_greedy_warm_start = run_config.enable_greedy_warm_start;
                        request.enable_local_search = run_config.enable_local_search;
                        request.sep_at_root = run_config.sep_at_root;
                        request.sep_frequency_nodes = run_config.sep_frequency_nodes;
                        request.sep_max_scenarios_per_call = run_config.sep_max_scenarios_per_call;
                        request.sep_max_nodes_per_scenario = run_config.sep_max_nodes_per_scenario;
                        request.sep_max_cuts_per_call = run_config.sep_max_cuts_per_call;
                        request.sep_min_violation = run_config.sep_min_violation;
                        request.sep_max_cut_cardinality = run_config.sep_max_cut_cardinality;
                        request.candidate_pool_size_multiplier = run_config.candidate_pool_size_multiplier;
                        request.candidate_pool_min_size = run_config.candidate_pool_min_size;
                        request.enable_greedy_exact_marginal = run_config.enable_greedy_exact_marginal;
                        request.local_search_max_iterations = run_config.local_search_max_iterations;
                        request.local_search_time_limit_sec = run_config.local_search_time_limit_sec;
                        request.max_aggregate_dominator_cuts_per_scenario =
                            run_config.max_aggregate_dominator_cuts_per_scenario;
                        request.max_individual_dominator_cuts_per_scenario =
                            run_config.max_individual_dominator_cuts_per_scenario;
                        request.root_user_cut_max_rounds = run_config.root_user_cut_max_rounds;
                        request.root_user_cut_tolerance = run_config.root_user_cut_tolerance;
                        request.use_coverage_llbi = run_config.use_coverage_llbi;
                        request.use_path_llbi = run_config.use_path_llbi;
                        request.path_llbi_max_paths_per_node =
                            run_config.path_llbi_max_paths_per_node;
                        request.projected_llbi_options =
                            run_config.projected_llbi_options;
                        request.use_global_dominance_preprocessing =
                            run_config.use_global_dominance_preprocessing;
                        request.use_conditional_zero_benefit_fixing =
                            run_config.use_conditional_zero_benefit_fixing;
                        request.combinatorial_options =
                            run_config.combinatorial_options;
                        request.risk_config = run_config.risk_config;
                        request.risk_measure_specified = run_config.risk_measure_specified;
                        request.cvar_beta_specified = run_config.cvar_beta_specified;
                        request.cvar_lambda_specified = run_config.cvar_lambda_specified;

                        std::cout << "[" << (run_count + 1) << "] "
                                  << method
                                  << (result_fpp_mode.empty() ? "" : " mode=" + result_fpp_mode)
                                  << " alpha=" << alpha
                                  << " train=" << train_count
                                  << " test=" << config.test_count
                                  << " case=" << case_id
                                  << " seed=" << seed << "\n" << std::flush;

                        try {
                            const auto result = dispatcher.run_method(request);
                            completed_keys.insert(current_key);
                            std::cout << "  status=" << result.solver_status
                                      << " objective=" << result.objective_in_sample
                                      << " best_bound=" << result.best_bound
                                      << " mip_gap=";
                            if (std::isfinite(result.mip_gap)) {
                                std::cout << result.mip_gap;
                            } else {
                                std::cout << "NA";
                            }
                            std::cout
                                      << " validation=" << result.validation_status
                                      << " test_expected=" << result.test_expected_burned_area
                                      << " runtime=" << result.runtime_seconds << "\n" << std::flush;
                        } catch (const std::exception& exc) {
                            ++failure_count;
                            std::cerr << "  failed: " << exc.what() << "\n";
                            throw;
                        }
                        ++run_count;
                    }
                }
            }
        }
    }

    std::cout << "Batch completed. Method runs: " << run_count
              << ", skipped: " << skipped_count
              << ", failures: " << failure_count << "\n";
    return failure_count == 0 ? 0 : 2;
}

}  // namespace firebreak::experiments
