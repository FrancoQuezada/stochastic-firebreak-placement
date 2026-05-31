#include "experiments/FppDominancePreprocessingDiagnosticRunner.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "benders/FppStrengthening.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"

namespace firebreak::experiments {

namespace {

struct DominanceDiagnosticResult {
    std::string experiment_id;
    std::string case_id;
    unsigned int seed_base = 0;
    unsigned int seed = 0;
    std::string landscape;
    double alpha = 0.0;
    int train_count = 0;
    std::vector<int> train_ids;
    int compact_node_count = 0;
    int eligible_candidate_count_before = 0;
    int eligible_candidate_count_after = 0;
    bool global_dominance_enabled = true;
    int global_dominance_candidates_removed = 0;
    int global_dominance_equivalence_classes = 0;
    double global_dominance_precompute_time_sec = 0.0;
    std::vector<int> removed_candidate_compact_nodes;
    std::vector<int> removed_candidate_original_nodes;
    std::vector<int> kept_candidate_compact_nodes;
    std::vector<int> kept_candidate_original_nodes;
    std::vector<std::string> notes;
    std::string warning;
};

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

std::string join_ints(const std::vector<int>& values, char delimiter = ';') {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << delimiter;
        }
        out << values[i];
    }
    return out.str();
}

std::string join_strings(const std::vector<std::string>& values, char delimiter = ';') {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << delimiter;
        }
        out << values[i];
    }
    return out.str();
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}

std::string csv_escape(const std::string& value) {
    const bool needs_quotes =
        value.find(',') != std::string::npos ||
        value.find('"') != std::string::npos ||
        value.find('\n') != std::string::npos ||
        value.find('\r') != std::string::npos;
    if (!needs_quotes) {
        return value;
    }
    std::string escaped = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string format_double(double value) {
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            return "nan";
        }
        return value > 0.0 ? "inf" : "-inf";
    }
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

std::string json_int_array(const std::vector<int>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << values[i];
    }
    out << "]";
    return out.str();
}

void write_json(const std::filesystem::path& path, const DominanceDiagnosticResult& result) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Could not write dominance preprocessing JSON: " + path.string());
    }
    out << "{\n"
        << "  \"experiment_id\": \"" << json_escape(result.experiment_id) << "\",\n"
        << "  \"case_id\": \"" << json_escape(result.case_id) << "\",\n"
        << "  \"seed_base\": " << result.seed_base << ",\n"
        << "  \"seed\": " << result.seed << ",\n"
        << "  \"landscape\": \"" << json_escape(result.landscape) << "\",\n"
        << "  \"alpha\": " << format_double(result.alpha) << ",\n"
        << "  \"train_count\": " << result.train_count << ",\n"
        << "  \"train_ids\": " << json_int_array(result.train_ids) << ",\n"
        << "  \"compact_node_count\": " << result.compact_node_count << ",\n"
        << "  \"eligible_candidate_count_before\": " << result.eligible_candidate_count_before << ",\n"
        << "  \"eligible_candidate_count_after\": " << result.eligible_candidate_count_after << ",\n"
        << "  \"global_dominance_enabled\": " << (result.global_dominance_enabled ? "true" : "false") << ",\n"
        << "  \"global_dominance_candidates_removed\": " << result.global_dominance_candidates_removed << ",\n"
        << "  \"global_dominance_equivalence_classes\": " << result.global_dominance_equivalence_classes << ",\n"
        << "  \"global_dominance_precompute_time_sec\": "
        << format_double(result.global_dominance_precompute_time_sec) << ",\n"
        << "  \"removed_candidate_compact_nodes\": "
        << json_int_array(result.removed_candidate_compact_nodes) << ",\n"
        << "  \"removed_candidate_original_nodes\": "
        << json_int_array(result.removed_candidate_original_nodes) << ",\n"
        << "  \"kept_candidate_compact_nodes\": "
        << json_int_array(result.kept_candidate_compact_nodes) << ",\n"
        << "  \"kept_candidate_original_nodes\": "
        << json_int_array(result.kept_candidate_original_nodes) << ",\n"
        << "  \"notes\": \"" << json_escape(join_strings(result.notes)) << "\",\n"
        << "  \"warning\": \"" << json_escape(result.warning) << "\"\n"
        << "}\n";
}

void append_csv(const std::filesystem::path& path, const DominanceDiagnosticResult& result) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const bool write_header = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    std::ofstream out(path, std::ios::app);
    if (!out) {
        throw std::runtime_error("Could not write dominance preprocessing CSV: " + path.string());
    }
    if (write_header) {
        out << "experiment_id,case_id,seed_base,seed,landscape,alpha,train_count,train_ids,"
            << "compact_node_count,eligible_candidate_count_before,eligible_candidate_count_after,"
            << "global_dominance_enabled,global_dominance_candidates_removed,"
            << "global_dominance_equivalence_classes,global_dominance_precompute_time_sec,"
            << "removed_candidate_compact_nodes,removed_candidate_original_nodes,"
            << "kept_candidate_compact_nodes,kept_candidate_original_nodes,notes,warning\n";
    }
    out << csv_escape(result.experiment_id) << ','
        << csv_escape(result.case_id) << ','
        << result.seed_base << ','
        << result.seed << ','
        << csv_escape(result.landscape) << ','
        << format_double(result.alpha) << ','
        << result.train_count << ','
        << csv_escape(join_ints(result.train_ids)) << ','
        << result.compact_node_count << ','
        << result.eligible_candidate_count_before << ','
        << result.eligible_candidate_count_after << ','
        << (result.global_dominance_enabled ? "true" : "false") << ','
        << result.global_dominance_candidates_removed << ','
        << result.global_dominance_equivalence_classes << ','
        << format_double(result.global_dominance_precompute_time_sec) << ','
        << csv_escape(join_ints(result.removed_candidate_compact_nodes)) << ','
        << csv_escape(join_ints(result.removed_candidate_original_nodes)) << ','
        << csv_escape(join_ints(result.kept_candidate_compact_nodes)) << ','
        << csv_escape(join_ints(result.kept_candidate_original_nodes)) << ','
        << csv_escape(join_strings(result.notes)) << ','
        << csv_escape(result.warning) << '\n';
}

std::vector<int> compact_to_original_nodes(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& compact_nodes) {
    std::vector<int> original_nodes;
    original_nodes.reserve(compact_nodes.size());
    for (const int compact : compact_nodes) {
        original_nodes.push_back(opt.node_mapper.to_node(compact));
    }
    return original_nodes;
}

}  // namespace

int FppDominancePreprocessingDiagnosticRunner::run(
    const FppDominancePreprocessingDiagnosticOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
    }
    if (options.train_ids.empty()) {
        throw std::runtime_error("--train-ids is required.");
    }

    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());
    const auto output_json = options.output_json_path.empty()
        ? firebreak::io::resolve_output_path("results/diagnostics/fpp_global_dominance.json")
        : firebreak::io::resolve_output_path(options.output_json_path.string());
    const auto output_csv = options.output_csv_path.empty()
        ? firebreak::io::resolve_output_path("results/diagnostics/fpp_global_dominance.csv")
        : firebreak::io::resolve_output_path(options.output_csv_path.string());

    const auto inventory = firebreak::io::detect_message_files(results_path);
    firebreak::io::validate_scenario_ids(inventory, options.train_ids);

    firebreak::io::Cell2FireReader reader;
    std::vector<std::string> warnings;
    const auto instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        options.train_ids,
        warnings);

    opt::OptimizationInstanceBuilder builder;
    const auto opt = builder.build(instance, options.alpha, false);
    const auto dominance = benders::apply_fpp_global_dominance_preprocessing(opt, true);

    DominanceDiagnosticResult result;
    result.experiment_id = options.experiment_id;
    result.case_id = options.case_id;
    result.seed_base = options.seed_base;
    result.seed = options.seed;
    result.landscape = options.landscape;
    result.alpha = options.alpha;
    result.train_count = static_cast<int>(options.train_ids.size());
    result.train_ids = options.train_ids;
    result.compact_node_count = opt.node_mapper.size();
    result.eligible_candidate_count_before = static_cast<int>(opt.eligible_indices.size());
    result.eligible_candidate_count_after =
        static_cast<int>(dominance.reduced_instance.eligible_indices.size());
    result.global_dominance_enabled = dominance.enabled;
    result.global_dominance_candidates_removed = dominance.candidates_removed;
    result.global_dominance_equivalence_classes = dominance.equivalence_classes;
    result.global_dominance_precompute_time_sec = dominance.precompute_time_sec;
    result.removed_candidate_compact_nodes = dominance.removed_candidate_compact_nodes;
    result.removed_candidate_original_nodes =
        compact_to_original_nodes(opt, result.removed_candidate_compact_nodes);
    result.kept_candidate_compact_nodes = dominance.kept_candidate_compact_nodes;
    result.kept_candidate_original_nodes =
        compact_to_original_nodes(opt, result.kept_candidate_compact_nodes);
    result.notes = dominance.notes;
    if (!warnings.empty()) {
        result.warning = warnings.front();
    }

    write_json(output_json, result);
    append_csv(output_csv, result);

    std::cout << "FPP global dominance preprocessing completed: case=" << options.case_id
              << " alpha=" << options.alpha
              << " before=" << result.eligible_candidate_count_before
              << " removed=" << result.global_dominance_candidates_removed
              << " after=" << result.eligible_candidate_count_after
              << " json=" << firebreak::io::path_to_string(output_json)
              << "\n";
    return 0;
}

}  // namespace firebreak::experiments
