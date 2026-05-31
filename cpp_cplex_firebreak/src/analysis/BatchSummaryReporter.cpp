#include "analysis/BatchSummaryReporter.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "io/PathUtils.hpp"

namespace firebreak::analysis {

namespace {

using CsvRow = std::unordered_map<std::string, std::string>;

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

std::vector<CsvRow> read_csv(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Could not open CSV file: " + path.string());
    }
    std::string header_line;
    if (!std::getline(in, header_line)) {
        return {};
    }
    const auto header = parse_csv_line(header_line);
    std::vector<CsvRow> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = parse_csv_line(line);
        CsvRow row;
        for (std::size_t i = 0; i < header.size(); ++i) {
            row[header[i]] = i < fields.size() ? fields[i] : "";
        }
        rows.push_back(row);
    }
    return rows;
}

std::string value_or_empty(const CsvRow& row, const std::string& key) {
    const auto found = row.find(key);
    return found == row.end() ? "" : found->second;
}

double parse_double_or_zero(const std::string& value) {
    if (value.empty()) {
        return 0.0;
    }
    try {
        return std::stod(value);
    } catch (...) {
        return 0.0;
    }
}

std::string format_double(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

std::string join_set(const std::set<std::string>& values) {
    std::ostringstream out;
    bool first = true;
    for (const auto& value : values) {
        if (!first) {
            out << ", ";
        }
        out << value;
        first = false;
    }
    return out.str();
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[mid];
    }
    return 0.5 * (values[mid - 1] + values[mid]);
}

}  // namespace

void BatchSummaryReporter::write_report(
    const std::filesystem::path& batch_results_csv,
    const std::filesystem::path& summary_by_method_csv,
    const std::filesystem::path& pairwise_csv,
    const std::filesystem::path& runtime_summary_csv,
    const std::filesystem::path& output_report,
    const std::string& experiment_name) const {
    const auto batch_rows = read_csv(batch_results_csv);
    const auto summary_rows = read_csv(summary_by_method_csv);
    const auto pairwise_rows = read_csv(pairwise_csv);
    const auto runtime_rows = read_csv(runtime_summary_csv);

    std::set<std::string> methods;
    std::set<std::string> alphas;
    std::set<std::string> train_counts;
    std::set<std::string> case_keys;
    for (const auto& row : batch_rows) {
        methods.insert(value_or_empty(row, "method"));
        alphas.insert(value_or_empty(row, "alpha"));
        train_counts.insert(value_or_empty(row, "train_scenario_count"));
        case_keys.insert(
            value_or_empty(row, "alpha") + "|" +
            value_or_empty(row, "train_scenario_count") + "|" +
            value_or_empty(row, "case_id"));
    }

    int dpv_wins = 0;
    int fpp_wins = 0;
    int ties = 0;
    std::vector<double> relative_differences;
    for (const auto& row : pairwise_rows) {
        const std::string winner = value_or_empty(row, "winner");
        if (winner == "DPV-SAA") {
            ++dpv_wins;
        } else if (winner == "FPP-SAA") {
            ++fpp_wins;
        } else if (winner == "Tie") {
            ++ties;
        }
        relative_differences.push_back(parse_double_or_zero(value_or_empty(row, "relative_difference")));
    }

    double avg_relative = 0.0;
    for (const double value : relative_differences) {
        avg_relative += value;
    }
    if (!relative_differences.empty()) {
        avg_relative /= static_cast<double>(relative_differences.size());
    }

    std::map<std::string, std::vector<CsvRow>> summary_by_alpha_train;
    for (const auto& row : summary_rows) {
        const std::string key = value_or_empty(row, "alpha") + " / train " + value_or_empty(row, "train_count");
        summary_by_alpha_train[key].push_back(row);
    }

    std::map<std::string, CsvRow> graph_ratio_rows_by_case;
    for (const auto& row : batch_rows) {
        if (value_or_empty(row, "train_graph_classification_ratios").empty() &&
            value_or_empty(row, "test_graph_classification_ratios").empty()) {
            continue;
        }
        const std::string key =
            value_or_empty(row, "alpha") + "|" +
            value_or_empty(row, "train_scenario_count") + "|" +
            value_or_empty(row, "test_scenario_count") + "|" +
            value_or_empty(row, "case_id");
        graph_ratio_rows_by_case.emplace(key, row);
    }

    firebreak::io::ensure_parent_directory(output_report);
    std::ofstream out(output_report);
    if (!out) {
        throw std::runtime_error("Could not open summary report: " + output_report.string());
    }

    out << "Experiment: " << experiment_name << "\n";
    out << "Graph note: Cell2Fire scenarios are treated as directed propagation graphs.\n";
    out << "Total rows: " << batch_rows.size() << "\n";
    out << "Methods: " << join_set(methods) << "\n";
    out << "Alphas: " << join_set(alphas) << "\n";
    out << "Train counts: " << join_set(train_counts) << "\n";
    out << "Number of case groups: " << case_keys.size() << "\n\n";

    out << "Method Means By Alpha And Train Count\n";
    for (const auto& [key, rows] : summary_by_alpha_train) {
        out << "\n" << key << "\n";
        out << "method,mean_test_expected,mean_test_worst_10pct,mean_runtime,mean_mip_gap\n";
        for (const auto& row : rows) {
            out << value_or_empty(row, "method") << ","
                << value_or_empty(row, "mean_test_expected_burned_area") << ","
                << value_or_empty(row, "mean_test_worst_10pct_burned_area") << ","
                << value_or_empty(row, "mean_runtime_seconds") << ","
                << value_or_empty(row, "mean_mip_gap") << "\n";
        }
    }

    out << "\nFPP-SAA vs DPV-SAA Pairwise\n";
    out << "Comparable cases: " << pairwise_rows.size() << "\n";
    out << "DPV wins: " << dpv_wins << "\n";
    out << "FPP wins: " << fpp_wins << "\n";
    out << "Ties: " << ties << "\n";
    out << "Average relative difference: " << format_double(avg_relative) << "\n";
    out << "Median relative difference: " << format_double(median(relative_differences)) << "\n";

    if (!graph_ratio_rows_by_case.empty()) {
        out << "\nGraph Classification Ratios By Case\n";
        out << "Abbreviations: RT=rooted arborescence, ADAG=acyclic DAG not tree, "
            << "GDG=general directed graph, NFR=not fully reachable, EMPTY=empty or invalid.\n";
        out << "alpha,train_count,test_count,case_id,train_graph_ratios,test_graph_ratios\n";
        for (const auto& [unused_key, row] : graph_ratio_rows_by_case) {
            (void)unused_key;
            out << value_or_empty(row, "alpha") << ","
                << value_or_empty(row, "train_scenario_count") << ","
                << value_or_empty(row, "test_scenario_count") << ","
                << value_or_empty(row, "case_id") << ","
                << value_or_empty(row, "train_graph_classification_ratios") << ","
                << value_or_empty(row, "test_graph_classification_ratios") << "\n";
        }
    }

    out << "\nRuntime By Method\n";
    out << "method,alpha,train_count,count,mean_runtime,median_runtime,max_runtime,mean_mip_gap,optimal_count,non_optimal_count\n";
    for (const auto& row : runtime_rows) {
        out << value_or_empty(row, "method") << ","
            << value_or_empty(row, "alpha") << ","
            << value_or_empty(row, "train_count") << ","
            << value_or_empty(row, "count") << ","
            << value_or_empty(row, "mean_runtime_seconds") << ","
            << value_or_empty(row, "median_runtime_seconds") << ","
            << value_or_empty(row, "max_runtime_seconds") << ","
            << value_or_empty(row, "mean_mip_gap") << ","
            << value_or_empty(row, "number_optimal") << ","
            << value_or_empty(row, "number_non_optimal") << "\n";
    }
}

}  // namespace firebreak::analysis
